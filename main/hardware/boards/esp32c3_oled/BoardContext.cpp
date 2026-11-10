#include "BoardContext.h"
#include "esp_log.h"

void BoardContext::Init()
{
    auto init = initState_.TryBeginInit();
    if (!init)
    {
        ESP_LOGW(TAG, "Already initialized or initializing");
        return;
    }

    led_.Init();

    i2c_master_bus_config_t bus = {};
    bus.i2c_port = -1; // any free controller
    bus.sda_io_num = static_cast<gpio_num_t>(BoardConfig::OLED_SDA_PIN);
    bus.scl_io_num = static_cast<gpio_num_t>(BoardConfig::OLED_SCL_PIN);
    bus.clk_source = I2C_CLK_SRC_DEFAULT;
    bus.glitch_ignore_cnt = 7;
    bus.flags.enable_internal_pullup = true;

    // A board whose display does not come up still boots: the display then
    // ignores what is written to it, and the rest of the device is unaffected.
    if (i2c_new_master_bus(&bus, &oledBus_) == ESP_OK)
        display_.Init(oledBus_);
    else
        ESP_LOGE(TAG, "Could not create the OLED's I2C bus");

    init.SetReady();
    ESP_LOGI(TAG, "Initialized");
}
