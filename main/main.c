#include <stdio.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_modbus_common.h"
#include "esp_modbus_slave.h"

#define WIFI_SSID "Soi13"
#define WIFI_PASS ""

#define MB_PORT 502
#define MB_SLAVE_ADDR 1
#define MB_REG_COUNT 10
#define TAG "MODBUS_SLAVE"

// Holding registers example
enum {
    MB_REG_TEMPERATURE = 0,
    MB_REG_HUMIDITY,
    MB_REG_MAX
};

static void wifi_init(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS
        }
    };

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();

    ESP_LOGI(TAG, "Wi-Fi started, connecting to %s...", WIFI_SSID);
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    wifi_init();

    // Modbus TCP Slave interface
    void *slave_interface = NULL;

    mb_communication_info_t tcp_slave_config = {
        .tcp_opts.port = MB_PORT,                // communication port number for Modbus slave
        .tcp_opts.mode = MB_TCP,                            // mode of communication for slave
        .tcp_opts.addr_type = MB_IPV4,                      // type of addressing being used
        .tcp_opts.ip_addr_table = NULL,                     // Bind to any address
        .tcp_opts.ip_netif_ptr = (void*)get_example_netif(),// the pointer to netif inteface
        .tcp_opts.uid = MB_SLAVE_ADDR                       // Modbus slave Unit Identifier
    };

    // Initialize TCP slave
    esp_err_t err = mbc_slave_create_tcp(&tcp_slave_config, &slave_interface);

    if (slave_interface == NULL || err != ESP_OK) {
        ESP_LOGE(TAG, "Modbus controller initialization fail.");
    }

    // Allocate holding registers
    uint16_t holding_regs[MB_REG_COUNT] = {0};

    // Assign registers to the slave interface
    ESP_ERROR_CHECK(mbc_slave_set_descriptor(slave_interface, (void *)holding_regs));

    // Start Modbus slave task
    ESP_ERROR_CHECK(mbc_slave_start(slave_interface));

    ESP_LOGI("MODBUS", "Modbus TCP slave started on port %d", MB_PORT);

    while (1) {
        holding_regs[0] = 225; // 22.5°C
        holding_regs[1] = 550; // 55.0%
        ESP_LOGI("MODBUS", "Updated registers: Temp=%.1f Hum=%.1f",
                 holding_regs[0] / 10.0,
                 holding_regs[1] / 10.0);

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}