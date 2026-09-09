#include "bus_type.hpp"

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
SoftwareSerial mySerial;
#endif

BusType Bus;

// On ESP8266, maximum 512 icw SoftwareSerial, otherwise you run out of heap
#define RXBUFFERSIZE 512
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

int arbitration_client_ = -1;
int arbitration_address_ = -1;

void getArbitrationClient(int& clientFd, uint8_t& address) {
  ENH_MUTEX_LOCK();
  clientFd = arbitration_client_;
  address = arbitration_address_;
  ENH_MUTEX_UNLOCK();
}

void clearArbitrationClient() {
  ENH_MUTEX_LOCK();
  arbitration_client_ = -1;
  arbitration_address_ = -1;
  ENH_MUTEX_UNLOCK();
}

bool setArbitrationClient(int& clientFd, uint8_t& address) {
  bool result = true;
  ENH_MUTEX_LOCK();
  if (arbitration_client_ < 0) {
    arbitration_client_ = clientFd;
    arbitration_address_ = address;
  } else {
    result = false;
    clientFd = arbitration_client_;
    address = arbitration_address_;
  }
  ENH_MUTEX_UNLOCK();
  return result;
}

void arbitrationDone() { clearArbitrationClient(); }

int arbitrationRequested(uint8_t& address) {
  int clientFd = -1;
  getArbitrationClient(clientFd, address);
  return clientFd;
}

BusType::BusType()
    : nbr_restarts_1_(0),
      nbr_restarts_2_(0),
      nbr_arbitrations_(0),
      nbr_lost_1_(0),
      nbr_lost_2_(0),
      nbr_won_1_(0),
      nbr_won_2_(0),
      nbr_errors_(0),
      nbr_late_(0),
      client_fd_(-1) {}

BusType::~BusType() { end(); }

#if USE_ASYNCHRONOUS
void IRAM_ATTR BusType::receiveHandler() {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  vTaskNotifyGiveFromISR(Bus.serial_event_task_, &xHigherPriorityTaskWoken);
  portEND_SWITCHING_ISR(xHigherPriorityTaskWoken);
}

void BusType::readDataFromSoftwareSerial(void* args) {
  for (;;) {
    BaseType_t r = ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
    {
      // For SoftwareSerial;
      // The method "available" always evaluates all the interrupts received
      // The method "read" only evaluates the interrupts received if there is no
      // byte available
      int avail = mySerial.available();
      if (!avail && r == 1) {
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

        // We need to poll mySerial for availability of a byte. Testing has
        // shown that from 1 millisecond onward we need to check for incoming
        // data every 500 micros. We have to wait using vTaskDelay to allow the
        // processor to do other things, however that only allows millisecond
        // resolution. To work around, split the polling in two sections: 1)
        // Wait for 500 micros using busy wait with delayMicroseconds 2) Wait
        // the rest of the timeslice, which will be about 500 micros, using
        // vTaskDelay
        uint32_t begin = (uint32_t)(esp_timer_get_time());
        vTaskDelay(1);
        avail = mySerial.available();

        // How was the delay until now?
        uint32_t delayed = (uint32_t)(esp_timer_get_time()) - begin;

        // Loop till the maximum duration of 1 byte (4167 micros from begin)
        // and check every 500 micros, using combination of
        // esp_rom_delay_us(500) and vTaskDelay(pdMS_TO_TICKS(1)) . The
        // vTaskDelay will wait till the end of the current timeslice, which is
        // typically about 500 micros away, because the previous vTaskDelay
        // makes sure the code is already synced to this tick Assumption: time
        // needed for mySerial.available() is less than 500 micros.
        while (delayed < 4167 && !avail) {
          if (4167 - delayed > 1000) {  // Need to wait more than 1000 micros?
            esp_rom_delay_us(500);
            avail = mySerial.available();
            if (!avail) {
              vTaskDelay(1);
            }
          } else {  // Otherwise spend the remaining wait with delayMicroseconds
            uint32_t delay = 4167 - delayed < 500 ? 4167 - delayed : 500;
            esp_rom_delay_us(delay);
          }
          avail = mySerial.available();
          delayed = (uint32_t)(esp_timer_get_time()) - begin;
        }
      }
      if (avail) {
        int symbol = mySerial.read();
        Bus.receive(symbol, mySerial.readStartBitTimeStamp());
      }
    }
  }
  vTaskDelete(NULL);
}
#endif

void BusType::begin() {
#if USE_SOFTWARE_SERIAL
  BusSer.begin(2400, SERIAL_8N1, -1, UART_TX);  // used for writing
  mySerial.enableStartBitTimeStampRecording(true);
  mySerial.enableTx(false);
  mySerial.enableIntTx(false);
  mySerial.begin(2400, SWSERIAL_8N1, UART_RX, -1, false,
                 RXBUFFERSIZE);  // used for reading
#else
  BusSer.setRxBufferSize(RXBUFFERSIZE);
  BusSer.begin(2400, UART_DATA_8_BITS, UART_RX, UART_TX);  // used for writing
  BusSer.setRxFIFOFull(1);
#endif

#if USE_ASYNCHRONOUS
  queue_ = xQueueCreate(QUEUE_SIZE, sizeof(data));
  xTaskCreateUniversal(BusType::readDataFromSoftwareSerial, "_serialEventQueue",
                       SERIAL_EVENT_TASK_STACK_SIZE, this,
                       SERIAL_EVENT_TASK_PRIORITY, &serial_event_task_,
                       SERIAL_EVENT_TASK_RUNNING_CORE);
  mySerial.onReceive(BusType::receiveHandler);
#endif
}

void BusType::end() {
  BusSer.end();
#if USE_SOFTWARE_SERIAL
  mySerial.end();
#endif

#if USE_ASYNCHRONOUS
  vQueueDelete(queue_);
  queue_ = 0;

  vTaskDelete(serial_event_task_);
  serial_event_task_ = 0;
#endif
}

int BusType::availableForWrite() { return BusSer.availableForWrite(); }

size_t BusType::write(uint8_t symbol) { return BusSer.write(symbol); }

bool BusType::read(data& d) {
#if USE_ASYNCHRONOUS
  return xQueueReceive(queue_, &d, 0) == pdTRUE;
#else
#if USE_SOFTWARE_SERIAL
  if (mySerial.available()) {
    uint8_t symbol = mySerial.read();
    receive(symbol, mySerial.readStartBitTimeStamp());
  }
#else
  if (BusSer.available()) {
    uint8_t symbol = BusSer.read();
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
  return mySerial.available();
#else
  return BusSer.available();
#endif
}

void BusType::push(const data& d) {
#if USE_ASYNCHRONOUS
  xQueueSendToBack(queue_, &d, 0);
#else
  queue_.push(d);
#endif
}

void BusType::receive(uint8_t symbol, uint32_t startBitTime) {
  bus_state_.data(symbol);
  Arbitration::state state =
      arbitration_.data(bus_state_, symbol, startBitTime);
  switch (state) {
    case Arbitration::restart1:
      nbr_restarts_1_++;
      goto NONE;
    case Arbitration::restart2:
      nbr_restarts_2_++;
      goto NONE;
    case Arbitration::none:
    NONE:
      uint8_t address;
      client_fd_ = arbitrationRequested(address);
      if (client_fd_ >= 0) {
        switch (arbitration_.start(bus_state_, address, startBitTime)) {
          case Arbitration::started:
            nbr_arbitrations_++;
            DEBUG_LOG("BUS START SUCC 0x%02x %lu us\n", symbol,
                      bus_state_.microsSinceLastSyn());
            break;
          case Arbitration::late:
            nbr_late_++;
            [[fallthrough]];
          case Arbitration::not_started:
            DEBUG_LOG("BUS START WAIT 0x%02x %lu us\n", symbol,
                      bus_state_.microsSinceLastSyn());
        }
      }
      // send to everybody. ebusd needs the SYN to get in the right mood
      push({false, RECEIVED, symbol, -1, client_fd_});
      break;
    case Arbitration::arbitrating:
      DEBUG_LOG("BUS ARBITRATIN 0x%02x %lu us\n", symbol,
                bus_state_.microsSinceLastSyn());
      // do not send to arbitration client
      push({false, RECEIVED, symbol, client_fd_, client_fd_});
      break;
    case Arbitration::won1:
      nbr_won_1_++;
      goto WON;
    case Arbitration::won2:
      nbr_won_2_++;
    WON:
      arbitrationDone();
      DEBUG_LOG("BUS SEND WON   0x%02x %lu us\n", bus_state_.master_,
                bus_state_.microsSinceLastSyn());
      // send only to the arbitrating client
      push({true, STARTED, bus_state_.master_, client_fd_, client_fd_});
      // do not send to arbitrating client
      push({false, RECEIVED, symbol, client_fd_, client_fd_});
      client_fd_ = -1;
      break;
    case Arbitration::lost1:
      nbr_lost_1_++;
      goto LOST;
    case Arbitration::lost2:
      nbr_lost_2_++;
    LOST:
      arbitrationDone();
      DEBUG_LOG("BUS SEND LOST  0x%02x 0x%02x %lu us\n", bus_state_.master_,
                bus_state_.symbol_, bus_state_.microsSinceLastSyn());
      // send only to the arbitrating client
      push({true, FAILED, bus_state_.master_, client_fd_, client_fd_});
      // send to everybody
      push({false, RECEIVED, symbol, -1, client_fd_});
      client_fd_ = -1;
      break;
    case Arbitration::error:
      nbr_errors_++;
      arbitrationDone();
      // send only to the arbitrating client
      push({true, ERROR_EBUS, ERR_FRAMING, client_fd_, client_fd_});
      // send to everybody
      push({false, RECEIVED, symbol, -1, client_fd_});
      client_fd_ = -1;
      break;
  }
}
