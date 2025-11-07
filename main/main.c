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
#include "mqtt_client.h"

#define WIFI_SSID "Soi13"
#define WIFI_PASS ""

//Some variables regarding Modbus logic
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

//MQTT Server/Broker credentials
#define MQTT_BROKER_URI "mqtt://192.168.1.64"
#define MQTT_USER "mqtt_user"
#define MQTT_PASSWORD ""
#define FILTER_PRESSURE_DIFF "homeassistant/sensor/pressure"

//Wifi event handler for displaying parameters of connection
static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Wi-Fi disconnected, retrying...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Wi-Fi connected");
        ESP_LOGI(TAG, "IP Address: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Subnet Mask: " IPSTR, IP2STR(&event->ip_info.netmask));
        ESP_LOGI(TAG, "Gateway: " IPSTR, IP2STR(&event->ip_info.gw));
    }
}

//Initializing Wifi connection
static void wifi_init(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
}

static esp_mqtt_client_handle_t client = NULL;

static void mqtt_app(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        .credentials.username = MQTT_USER,
        .credentials.authentication.password = MQTT_PASSWORD,
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_start(client);
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    wifi_init();
    vTaskDelay(pdMS_TO_TICKS(5000)); //Delay 5 seconds before starting MQTT connection, otherwise if run right after method wifi_init(); it can't connect since wifi doesn't have enough time to establish connection.
    mqtt_app();

    // Modbus TCP Slave interface
    void *slave_interface = NULL;

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        ESP_LOGE(TAG, "Failed to get network interface handle");
        return;
    }

    mb_communication_info_t tcp_slave_config = {
        .tcp_opts.port = MB_PORT,                // communication port number for Modbus slave
        .tcp_opts.mode = MB_TCP,                            // mode of communication for slave
        .tcp_opts.addr_type = MB_IPV4,                      // type of addressing being used
        .tcp_opts.ip_addr_table = NULL,                     // Bind to any address
        .tcp_opts.ip_netif_ptr = netif,// the pointer to netif inteface
        .tcp_opts.uid = MB_SLAVE_ADDR                       // Modbus slave Unit Identifier
    };

    // Initialize TCP slave
    esp_err_t err = mbc_slave_create_tcp(&tcp_slave_config, &slave_interface);

    if (slave_interface == NULL || err != ESP_OK) {
        ESP_LOGE(TAG, "Modbus controller initialization fail.");
    }

    // Allocate holding registers
    uint16_t holding_regs[MB_REG_COUNT] = {0};

    mb_register_area_descriptor_t holding_area = {
        .type = MB_PARAM_HOLDING,
        .start_offset = 0,                // Modbus address 40001
        .address = (void*)holding_regs,    // Pointer to local array
        .size = sizeof(holding_regs)       // Size in bytes
    };

    // Assign registers to the slave interface
    ESP_ERROR_CHECK(mbc_slave_set_descriptor(slave_interface, holding_area));

    // Start Modbus slave task
    ESP_ERROR_CHECK(mbc_slave_start(slave_interface));

    ESP_LOGI("MODBUS", "Modbus TCP slave started on port %d", MB_PORT);

    while (1) {
        holding_regs[0] = 225; // 22.5°C
        holding_regs[1] = 550; // 55.0%
        ESP_LOGI("MODBUS", "Updated registers: Temp=%.1f Hum=%.1f",
                 holding_regs[0] / 10.0,
                 holding_regs[1] / 10.0);

        char pressure[16];
        snprintf(pressure, sizeof(pressure), "%d", holding_regs[0]);

        // Publish to Home Assistant topics
        esp_mqtt_client_publish(client, FILTER_PRESSURE_DIFF, pressure, 0, 1, 0);

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}