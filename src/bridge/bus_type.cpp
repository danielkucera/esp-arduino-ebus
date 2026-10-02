#if !defined(EBUS_INTERNAL)

#include "bridge/bus_type.hpp"

#include <driver/uart.h>
#include <esp_rom_sys.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <queue>

// For ESP's based on FreeRTOS we can optimize the arbitration timing.
// With SoftwareSerial we get notified with an callback that the
// signal has changed. SoftwareSerial itself can and does know the
// exact timing of the start bit. Use this for the timing of the
// arbitration. SoftwareSerial seems to have trouble with writing
// and reading at the same time. Hence use SoftwareSerial only for
// reading. For writing use HardwareSerial.
#if USE_SOFTWARE_SERIAL
#include <SoftwareSerial.h>
SoftwareSerial my_serial;
#endif

BusType bus;

// On ESP8266, maximum 512 icw SoftwareSerial, otherwise you run out of heap
#define RX_BUFFER_SIZE 512
#define QUEUE_SIZE 480

#define BAUD_RATE 2400
#define MAX_FRAMEBITS (1 + 8 + 1)
#define SERIAL_EVENT_TASK_STACK_SIZE 2048
#define SERIAL_EVENT_TASK_PRIORITY (configMAX_PRIORITIES - 1)
#define SERIAL_EVENT_TASK_RUNNING_CORE -1

// Locking
#if USE_ASYNCHRONOUS
SemaphoreHandle_t getMutex() {
  static SemaphoreHandle_t lock_ = NULL;
  if (lock_ == NULL) {
    lock_ = xSemaphoreCreateMutex();
    if (lock_ == NULL) {
      DEBUG_LOG("xSemaphoreCreateMutex failed");
      return NULL;
    }
  }
  return lock_;
}
#define ENH_MUTEX_LOCK() \
  do {                   \
  } while (xSemaphoreTake(getMutex(), portMAX_DELAY) != pdPASS)
#define ENH_MUTEX_UNLOCK() xSemaphoreGive(getMutex())
#else
#define ENH_MUTEX_LOCK()
#define ENH_MUTEX_UNLOCK()
#endif

int arbitration_client = -1;
int arbitration_address = -1;

void getArbitrationClient(int& client_fd, uint8_t& address) {
  ENH_MUTEX_LOCK();
  client_fd = arbitration_client;
  address = arbitration_address;
  ENH_MUTEX_UNLOCK();
}

void clearArbitrationClient() {
  ENH_MUTEX_LOCK();
  arbitration_client = -1;
  arbitration_address = -1;
  ENH_MUTEX_UNLOCK();
}

bool setArbitrationClient(int& client_fd, uint8_t& address) {
  bool result = true;
  ENH_MUTEX_LOCK();
  if (arbitration_client < 0) {
    arbitration_client = client_fd;
    arbitration_address = address;
  } else {
    result = false;
    client_fd = arbitration_client;
    address = arbitration_address;
  }
  ENH_MUTEX_UNLOCK();
  return result;
}

void arbitrationDone() { clearArbitrationClient(); }

int arbitrationRequested(uint8_t& address) {
  int client_fd = -1;
  getArbitrationClient(client_fd, address);
  return client_fd;
}

BusType::BusType()
    : nbr_restarts_1(0),
      nbr_restarts_2(0),
      nbr_arbitrations(0),
      nbr_lost_1(0),
      nbr_lost_2(0),
      nbr_won_1(0),
      nbr_won_2(0),
      nbr_errors(0),
      nbr_late(0),
      client_fd_(-1) {}

BusType::~BusType() { end(); }

#if USE_ASYNCHRONOUS
void IRAM_ATTR BusType::receiveHandler() {
  BaseType_t higher_priority_task_woken = pdFALSE;
  vTaskNotifyGiveFromISR(bus.serial_event_task_, &higher_priority_task_woken);
  portEND_SWITCHING_ISR(higher_priority_task_woken);
}

void BusType::readDataFromSoftwareSerial(void* args) {
  for (;;) {
    BaseType_t r = ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
    {
      // For SoftwareSerial;
      // The method "available" always evaluates all the interrupts received
      // The method "read" only evaluates the interrupts received if there is no
      // byte available
      int available = my_serial.available();
      if (!available && r == 1) {
        // avoid this busy wait: esp_rom_delay_us(1+ MAX_FRAMEBITS * 1000000 /
        // BAUD_RATE);

        // Need to wait for 1000000 / BAUD_RATE, rounded to the next upper
        // digit. delayMicroseconds is a busy wait, which blocks the CPU to do
        // other things and could be the reason that the Wifi connection is
        // blocked. Instead of a busy wait, do the majority of the waiting with
        // vTaskDelay. Because vTaskDelay is switching at Tick cycle, doing
        // vTaskDelay(1) can wait anywhere between 0 Tick and 1 Ticks. On esp32
        // Arduino  1 Tick is 1 MilliSecond, although it depends on
        // configuration.

        // Validate 1 Tick is 1 MilliSecond with a compile time assert
        static_assert(pdMS_TO_TICKS(1) == 1);
        // static_assert(sizeof(uint32_t) == sizeof(unsigned long));

        // We need to poll my_serial for availability of a byte. Testing has
        // shown that from 1 millisecond onward we need to check for incoming
        // data every 500 micros. We have to wait using vTaskDelay to allow the
        // processor to do other things, however that only allows millisecond
        // resolution. To work around, split the polling in two sections: 1)
        // Wait for 500 micros using busy wait with delayMicroseconds 2) Wait
        // the rest of the timeslice, which will be about 500 micros, using
        // vTaskDelay
        uint32_t begin = (uint32_t)(esp_timer_get_time());
        vTaskDelay(1);
        available = my_serial.available();

        // How was the delay until now?
        uint32_t delayed = (uint32_t)(esp_timer_get_time()) - begin;

        // Loop till the maximum duration of 1 byte (4167 micros from begin)
        // and check every 500 micros, using combination of
        // esp_rom_delay_us(500) and vTaskDelay(pdMS_TO_TICKS(1)) . The
        // vTaskDelay will wait till the end of the current timeslice, which is
        // typically about 500 micros away, because the previous vTaskDelay
        // makes sure the code is already synced to this tick Assumption: time
        // needed for my_serial.available() is less than 500 micros.
        while (delayed < 4167 && !available) {
          if (4167 - delayed > 1000) {  // Need to wait more than 1000 micros?
            esp_rom_delay_us(500);
            available = my_serial.available();
            if (!available) {
              vTaskDelay(1);
            }
          } else {  // Otherwise spend the remaining wait with delayMicroseconds
            uint32_t delay = 4167 - delayed < 500 ? 4167 - delayed : 500;
            esp_rom_delay_us(delay);
          }
          available = my_serial.available();
          delayed = (uint32_t)(esp_timer_get_time()) - begin;
        }
      }
      if (available) {
        int symbol = my_serial.read();
        bus.receive(symbol, my_serial.readStartBitTimeStamp());
      }
    }
  }
  vTaskDelete(NULL);
}
#endif

void BusType::begin() {
#if USE_SOFTWARE_SERIAL
  bus_ser.begin(2400, SERIAL_8N1, -1, UART_TX);  // used for writing
  my_serial.enableStartBitTimeStampRecording(true);
  my_serial.enableTx(false);
  my_serial.enableIntTx(false);
  my_serial.begin(2400, SWSERIAL_8N1, UART_RX, -1, false,
                  RX_BUFFER_SIZE);  // used for reading
#else
  bus_ser.setRxBufferSize(RX_BUFFER_SIZE);
  bus_ser.begin(2400, UART_DATA_8_BITS, UART_RX, UART_TX);  // used for writing
  bus_ser.setRxFIFOFull(1);
#endif

#if USE_ASYNCHRONOUS
  queue_ = xQueueCreate(QUEUE_SIZE, sizeof(Data));
  xTaskCreateUniversal(BusType::readDataFromSoftwareSerial, "_serialEventQueue",
                       SERIAL_EVENT_TASK_STACK_SIZE, this,
                       SERIAL_EVENT_TASK_PRIORITY, &serial_event_task_,
                       SERIAL_EVENT_TASK_RUNNING_CORE);
  my_serial.onReceive(BusType::receiveHandler);
#endif
}

void BusType::end() {
  bus_ser.end();
#if USE_SOFTWARE_SERIAL
  my_serial.end();
#endif

#if USE_ASYNCHRONOUS
  vQueueDelete(queue_);
  queue_ = 0;

  vTaskDelete(serial_event_task_);
  serial_event_task_ = 0;
#endif
}

int BusType::availableForWrite() { return bus_ser.availableForWrite(); }

size_t BusType::write(uint8_t symbol) { return bus_ser.write(symbol); }

bool BusType::read(Data& d) {
#if USE_ASYNCHRONOUS
  return xQueueReceive(queue_, &d, 0) == pdTRUE;
#else
#if USE_SOFTWARE_SERIAL
  if (my_serial.available()) {
    uint8_t symbol = my_serial.read();
    receive(symbol, my_serial.readStartBitTimeStamp());
  }
#else
  if (bus_ser.available()) {
    uint8_t symbol = bus_ser.read();
    receive(symbol, (uint32_t)(esp_timer_get_time()));
  }
#endif
  if (queue_.size() > 0) {
    d = queue_.front();
    queue_.pop();
    return true;
  }
  return false;
#endif
}

int BusType::available() {
#if USE_SOFTWARE_SERIAL
  return my_serial.available();
#else
  return bus_ser.available();
#endif
}

void BusType::push(const Data& d) {
#if USE_ASYNCHRONOUS
  xQueueSendToBack(queue_, &d, 0);
#else
  queue_.push(d);
#endif
}

void BusType::receive(uint8_t symbol, uint32_t start_bit_time) {
  bus_state_.data(symbol);
  Arbitration::State state =
      arbitration_.data(bus_state_, symbol, start_bit_time);
  switch (state) {
    case Arbitration::restart1:
      nbr_restarts_1++;
      goto NONE;
    case Arbitration::restart2:
      nbr_restarts_2++;
      goto NONE;
    case Arbitration::none:
    NONE:
      uint8_t address;
      client_fd_ = arbitrationRequested(address);
      if (client_fd_ >= 0) {
        switch (arbitration_.start(bus_state_, address, start_bit_time)) {
          case Arbitration::started:
            nbr_arbitrations++;
            DEBUG_LOG("BUS START SUCC 0x%02x %lu us\n", symbol,
                      bus_state_.microsSinceLastSyn());
            break;
          case Arbitration::late:
            nbr_late++;
            [[fallthrough]];
          case Arbitration::not_started:
            DEBUG_LOG("BUS START WAIT 0x%02x %lu us\n", symbol,
                      bus_state_.microsSinceLastSyn());
        }
      }
      // send to everybody. ebusd needs the SYN to get in the right mood
      push({false, received, symbol, -1, client_fd_});
      break;
    case Arbitration::arbitrating:
      DEBUG_LOG("BUS ARBITRATIN 0x%02x %lu us\n", symbol,
                bus_state_.microsSinceLastSyn());
      // do not send to arbitration client
      push({false, received, symbol, client_fd_, client_fd_});
      break;
    case Arbitration::won1:
      nbr_won_1++;
      goto WON;
    case Arbitration::won2:
      nbr_won_2++;
    WON:
      arbitrationDone();
      DEBUG_LOG("BUS SEND WON   0x%02x %lu us\n", bus_state_.master,
                bus_state_.microsSinceLastSyn());
      // send only to the arbitrating client
      push({true, started, bus_state_.master, client_fd_, client_fd_});
      // do not send to arbitrating client
      push({false, received, symbol, client_fd_, client_fd_});
      client_fd_ = -1;
      break;
    case Arbitration::lost1:
      nbr_lost_1++;
      goto LOST;
    case Arbitration::lost2:
      nbr_lost_2++;
    LOST:
      arbitrationDone();
      DEBUG_LOG("BUS SEND LOST  0x%02x 0x%02x %lu us\n", bus_state_.master,
                bus_state_.symbol, bus_state_.microsSinceLastSyn());
      // send only to the arbitrating client
      push({true, failed, bus_state_.master, client_fd_, client_fd_});
      // send to everybody
      push({false, received, symbol, -1, client_fd_});
      client_fd_ = -1;
      break;
    case Arbitration::error:
      nbr_errors++;
      arbitrationDone();
      // send only to the arbitrating client
      push({true, error_ebus, err_framing, client_fd_, client_fd_});
      // send to everybody
      push({false, received, symbol, -1, client_fd_});
      client_fd_ = -1;
      break;
  }
}

#endif
