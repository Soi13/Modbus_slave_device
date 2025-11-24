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
#include "freertos/semphr.h"
#include "esp_modbus_common.h"
#include "esp_modbus_slave.h"
#include "mqtt_client.h"
#include "driver/i2c.h"

#define WIFI_SSID "Soi13"
#define WIFI_PASS ""

// Some variables regarding Modbus logic
#define MB_PORT 502
#define MB_SLAVE_ADDR 1
#define MB_REG_COUNT 10
#define TAG "MODBUS_SLAVE"

// MQTT Server/Broker credentials
#define MQTT_BROKER_URI "mqtt://192.168.1.64"
#define MQTT_USER "mqtt_user"
#define MQTT_PASSWORD ""
#define FILTER_PRESSURE_DIFF "homeassistant/sensor/pressure"
#define FILTER_AREA_AIR_TEMPERATURE "homeassistant/sensor/filter_area_temperature"

// Defining I2C parameters for SDP810-500
#define I2C_MASTER_SCL_IO           22
#define I2C_MASTER_SDA_IO           21
#define I2C_MASTER_PORT             I2C_NUM_0
#define I2C_MASTER_FREQ_HZ          100000
#define I2C_MASTER_TIMEOUT_MS       1000

#define SDP810_ADDR                 0x25 // Address of pressure sensor SDP810-500
#define CMD_CONT_MEAS_AVG           0x3615   // Continuous diff pressure, averaged

// Allocate holding registers
static uint16_t holding_regs[MB_REG_COUNT] = {0};

// Wifi event handler for displaying parameters of connection
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

// Initializing Wifi connection
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

// Sensirion SDP810-500 CRC8: polynomial 0x31, init 0xFF
uint8_t sensirion_crc8(const uint8_t *data, uint8_t count) {
    uint8_t crc = 0xFF;
    for (int i = 0; i < count; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x80)
                crc = (crc << 1) ^ 0x31;
            else
                crc <<= 1;
        }
    }
    return crc;
}

// For SDP810-500
esp_err_t sdp_write_command(uint16_t cmd) {
    uint8_t data[2] = { cmd >> 8, cmd & 0xFF };

    return i2c_master_write_to_device(
        I2C_MASTER_PORT,
        SDP810_ADDR,
        data, 2,
        I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS
    );
}

// For SDP810-500
esp_err_t sdp_read_measurement(int16_t *raw_dp, int16_t *raw_temp) {
    uint8_t rx[6];

    esp_err_t err = i2c_master_read_from_device(
        I2C_MASTER_PORT,
        SDP810_ADDR,
        rx, 6,
        I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS
    );
    if (err != ESP_OK) return err;

    // CRC check
    if (sensirion_crc8(rx, 2) != rx[2] ||
        sensirion_crc8(rx + 3, 2) != rx[5]) {
        return ESP_ERR_INVALID_CRC;
    }

    *raw_dp   = (int16_t)((rx[0] << 8) | rx[1]);
    *raw_temp = (int16_t)((rx[3] << 8) | rx[4]);

    return ESP_OK;
}

void i2c_master_init() {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };

    ESP_ERROR_CHECK(i2c_param_config(I2C_MASTER_PORT, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_MASTER_PORT, conf.mode, 0, 0, 0));
}

void sensor_mqtt_task(void *arg)
{
    while(1) {
        int16_t raw_dp = 0;
        int16_t raw_temp = 0;

        esp_err_t err = sdp_read_measurement(&raw_dp, &raw_temp);

        if (err == ESP_OK) {
            //ESP_LOGI(TAG, "DP = %d Pa, T = %d °C (raw dp=%d, temp=%d)", raw_dp / 60, raw_temp / 200, raw_dp, raw_temp);
            char pressure[16];
            char temperature[16];
            snprintf(pressure, sizeof(pressure), "%d", raw_dp / 60);
            snprintf(temperature, sizeof(temperature), "%d", raw_temp / 200);

            // Publish to Home Assistant topics
            esp_mqtt_client_publish(client, FILTER_PRESSURE_DIFF, pressure, 0, 1, 0);
            esp_mqtt_client_publish(client, FILTER_AREA_AIR_TEMPERATURE, temperature, 0, 1, 0);
        } else if (err == ESP_ERR_INVALID_CRC) {
            ESP_LOGW(TAG, "CRC error");
        } else {
            ESP_LOGE(TAG, "I2C read error: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void modbus_task(void *arg)
{
    while(1) {
        int16_t mb_raw_dp = 0;
        int16_t mb_raw_temp = 0;

        esp_err_t err = sdp_read_measurement(&mb_raw_dp, &mb_raw_temp);

        if (err == ESP_OK) {
            holding_regs[0] = mb_raw_dp / 60;
            holding_regs[1] = mb_raw_temp / 200;
            //ESP_LOGI(TAG, "DP = %d Pa, T = %d °C (raw dp=%d, temp=%d)", mb_raw_dp / 60, mb_raw_temp / 200, mb_raw_dp, mb_raw_temp);
        } else if (err == ESP_ERR_INVALID_CRC) {
            ESP_LOGW(TAG, "CRC error");
        } else {
            ESP_LOGE(TAG, "I2C read error: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(10)); 
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    wifi_init();
    vTaskDelay(pdMS_TO_TICKS(5000)); // Delay 5 seconds before starting MQTT connection, otherwise if run right after method wifi_init(); it can't connect since wifi doesn't have enough time to establish connection.
    mqtt_app();
    i2c_master_init();
    ESP_LOGI(TAG, "Starting SDP810 continuous measurement...");

    // Start continuous measurement
    ESP_ERROR_CHECK(sdp_write_command(CMD_CONT_MEAS_AVG));
    vTaskDelay(pdMS_TO_TICKS(50));

    // Modbus TCP Slave interface
    void *slave_interface = NULL;

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        ESP_LOGE(TAG, "Failed to get network interface handle");
        return;
    }

    mb_communication_info_t tcp_slave_config = {
        .tcp_opts.port = MB_PORT,                  // communication port number for Modbus slave
        .tcp_opts.mode = MB_TCP,                   // mode of communication for slave
        .tcp_opts.addr_type = MB_IPV4,             // type of addressing being used
        .tcp_opts.ip_addr_table = NULL,            // bind to any address
        .tcp_opts.ip_netif_ptr = netif,            // the pointer to netif inteface
        .tcp_opts.uid = MB_SLAVE_ADDR              // Modbus slave Unit Identifier
    };

    // Initialize TCP slave
    esp_err_t err = mbc_slave_create_tcp(&tcp_slave_config, &slave_interface);

    if (slave_interface == NULL || err != ESP_OK) {
        ESP_LOGE(TAG, "Modbus controller initialization fail.");
    }

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

    ESP_LOGI(TAG, "Modbus TCP slave started on port %d", MB_PORT);

    //Create tasks with different priorities
    xTaskCreate(sensor_mqtt_task, "sensor_mqtt_task", 4096, NULL, 4, NULL);
    xTaskCreate(modbus_task, "modbus_task", 4096, NULL, 6, NULL);  // Higher priority for Modbus
}