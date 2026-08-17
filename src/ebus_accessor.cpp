#include "ebus_accessor.hpp"

#if defined(EBUS_INTERNAL)
#include "logger.hpp"

static ebus::EbusConfig s_config;
static ebus::Controller s_controller;

ebus::EbusConfig& getEbusConfig() { return s_config; }
ebus::Controller& getEbusController() { return s_controller; }

void configureEbus(const ebus::EbusConfig& cfg) {
  s_config = cfg;
  if (!s_controller.configure(s_config)) {
    logger.error(
        "eBUS: Configuration rejected! Check runtime params vs library "
        "limits.");
  }
}

void startEbus() { s_controller.start(); }
void stopEbus() { s_controller.stop(); }

#if defined(EBUS_SIMULATION)
TaskHandle_t sim_task_handle = nullptr;

TaskHandle_t simTaskHandle() { return sim_task_handle; }

void startEbusSimulation() {
  if (getEbusController().isConfigured()) {
    auto& vbus = getEbusController().getVirtualBus();
    // We mimic a Vaillant device reactions
    vbus.addSlaveReaction(0x01, "08070400", "0ab54d4f434b0001020304", 0, 0);
    vbus.addSlaveReaction(0x01, "08b5090124", "09003231313230383030", 0, 0);
    vbus.addSlaveReaction(0x01, "08b5090125", "09313030303930373030", 0, 0);
    vbus.addSlaveReaction(0x01, "08b5090126", "09303036303035313337", 0, 0);
    vbus.addSlaveReaction(0x01, "08b5090127", "094e3800000000000000", 0, 0);
    vbus.addSlaveReaction(0x01, "08b509030d0800", "039e0100", 0, 0);
    vbus.addSlaveReaction(0x01, "08b509030d1600", "03170700", 0, 0);
  }

  xTaskCreate(
      [](void*) {
        // Wait for controller to be fully initialized and running
        while (!getEbusController().isRunning()) {
          vTaskDelay(pdMS_TO_TICKS(100));
        }

        // Periodic injections to simulate device updates without master
        // requests
        uint32_t count17 = 0;
        uint32_t count25 = 0;
        TickType_t xLastWakeTime = xTaskGetTickCount();
        const TickType_t xFrequency = pdMS_TO_TICKS(1000);  // 1 second interval
        auto& vbus = getEbusController().getVirtualBus();

        for (;;) {
          vTaskDelayUntil(&xLastWakeTime, xFrequency);

          if (getEbusController().isRunning()) {
            if (++count17 >= 17) {
              count17 = 0;
              // Example: Broadcasting of an outside temperature of 9.25°C
              // * Master 0x10 -> Broadcast (0xfe),
              // * Vaillant Service (0xb5 0x16 0x03 0x01),
              // * Data: 9.25°C - DATA2B -> 0x40, 0x09
              vbus.injectMasterMessage(0x10, "feb51603014009");
            }

            if (++count25 >= 25) {
              count25 = 0;
              // Example: Broadcasting of a brine inlet temperature of 31.44°C
              // * Master 0x10 -> Slave (0x08)
              // * Vaillant Service (0xb5 0x09 0x03 0x29 0x0f 0x00),
              // * Data: 31.44°C - DATA2C -> 0xf7, 0x01
              vbus.injectMasterSlaveMessage(0x10, "08b50903290f00",
                                            "050f00f70100");
            }
          }
        }
      },
      "sim", 2048, nullptr, 1, &sim_task_handle);
}
#endif

#endif