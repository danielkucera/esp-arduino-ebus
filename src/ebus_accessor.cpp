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
esp_timer_handle_t sim_timer_handle = nullptr;

esp_timer_handle_t simTimerHandle() { return sim_timer_handle; }

void startEbusSimulation() {
  if (getEbusController().isConfigured()) {
    auto& vbus = getEbusController().getVirtualBus();

    // passive commands
    // Scan
    vbus.addSlaveReaction(0x01, "08070400", "0ab54d4f434b0001020304", 0, 0);
    // Scan vaillant specific
    vbus.addSlaveReaction(0x01, "08b5090124", "09003231313230383030", 0, 0);
    vbus.addSlaveReaction(0x01, "08b5090125", "09313030303930373030", 0, 0);
    vbus.addSlaveReaction(0x01, "08b5090126", "09303036303035313337", 0, 0);
    vbus.addSlaveReaction(0x01, "08b5090127", "094e3800000000000000", 0, 0);
    // Brine/Outlet_Temperature
    vbus.addSlaveReaction(0x01, "08b509030d0800", "039e0100", 0, 0);
    // Brine/Pressure
    vbus.addSlaveReaction(0x01, "08b509030d1600", "03170700", 0, 0);
    // Heatpump/Compressor
    vbus.addSlaveReaction(0x01, "08b509030d1d00", "0101", 0, 0);
  }

  // active commands
  esp_timer_create_args_t args = {};
  args.callback = [](void*) {
    if (!getEbusController().isRunning()) return;

    static uint32_t count17 = 0;
    static uint32_t count25 = 0;
    auto& vbus = getEbusController().getVirtualBus();

    if (++count17 >= 17) {
      count17 = 0;
      // Outside_Temperature
      vbus.injectMasterMessage(0x10, "feb51603014009");
    }

    if (++count25 >= 25) {
      count25 = 0;
      // Brine/Inlet_Temperature
      vbus.injectMasterSlaveMessage(0x10, "08b50903290f00", "050f00f70100");
    }
  };
  args.arg = nullptr;
  args.dispatch_method = ESP_TIMER_TASK;
  args.name = "sim_timer";

  esp_timer_create(&args, &sim_timer_handle);
  esp_timer_start_periodic(sim_timer_handle, 1000000);
}
#endif

#endif