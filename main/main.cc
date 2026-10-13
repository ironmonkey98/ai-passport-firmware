#include <esp_log.h>
#include <esp_err.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include <esp_mac.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdio>

#include "application.h"

#define TAG "main"

// CONFIG_CUSTOM_BASE_MAC replaces the factory MAC. It has to run before
// anything reads a MAC (Wi-Fi, Bluetooth, the Device-Id).
static void ApplyCustomBaseMac() {
    const char* text = CONFIG_CUSTOM_BASE_MAC;
    if (text[0] == '\0') {
        return;
    }
    unsigned v[6];
    char extra;
    if (sscanf(text, "%x:%x:%x:%x:%x:%x%c", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &extra) != 6) {
        ESP_LOGE(TAG, "Invalid CUSTOM_BASE_MAC '%s', using the factory MAC", text);
        return;
    }
    uint8_t mac[6];
    for (int i = 0; i < 6; i++) {
        mac[i] = static_cast<uint8_t>(v[i]);
    }
    esp_err_t err = esp_base_mac_addr_set(mac);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set base MAC %s: %s", text, esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "Base MAC set to %s", text);
}

extern "C" void app_main(void)
{
    // Initialize NVS flash for WiFi configuration
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS flash to fix corruption");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ApplyCustomBaseMac();

    // Initialize and run the application
    auto& app = Application::GetInstance();
    app.Initialize();
    app.Run();  // This function runs the main event loop and never returns
}
