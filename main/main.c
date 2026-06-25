#include <stdio.h>
#include <string.h> 
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"

// Driver
#include "driver/i2c.h"
#include "esp_lcd_touch_gt911.h"

// Wi-Fi and NVS Libraries
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"

// UI Header Mapping
#include "ui/ui.h"
#include "../components/espressif__esp_lcd_touch/display.h"

#include "driver/uart.h"

#define EX_UART_NUM UART_NUM_0 
#define BUF_SIZE (1024)

// Core function references
void generate_dynamic_defect_buttons(void);
extern void fail_button_click_cb(lv_event_t * e);
extern void on_serial_number_scanned(const char* new_serial);

static const char *APP_TAG = "main_app";

#define WIFI_SSID      "3S AP Uni"
#define WIFI_PASS      "3s290420097fab"

static void uart_monitor_rx_task(void *pvParameters) {
    uint8_t* dtmp = (uint8_t*) malloc(BUF_SIZE);
    char serial_buffer[64];
    int buffer_idx = 0;

    memset(serial_buffer, 0, sizeof(serial_buffer));

    while (1) {
        // Read data from the UART port byte by byte
        int len = uart_read_bytes(EX_UART_NUM, dtmp, 1, 20 / portTICK_PERIOD_MS);
        if (len > 0) {
            char incoming_char = dtmp[0];

            // If the character is a Enter/Newline key (\r or \n) -> Submit the Serial Number!
            if (incoming_char == '\r' || incoming_char == '\n') {
                if (buffer_idx > 0) {
                    serial_buffer[buffer_idx] = '\0'; // Null terminate string

                    ESP_LOGI("UART_INPUT", "Received Serial via Monitor: %s", serial_buffer);

                    // Update the text box on your screen dynamically so you see it visually
                    extern lv_obj_t * ui_txtSerialNumber;
                    if (ui_txtSerialNumber != NULL) {
                        lv_textarea_set_text(ui_txtSerialNumber, serial_buffer);
                    }

                    // Feed the barcode serial number directly into your backend logic pipeline
                    on_serial_number_scanned(serial_buffer);

                    // Reset buffer for the next scan sequence
                    buffer_idx = 0;
                    memset(serial_buffer, 0, sizeof(serial_buffer));
                }
            } 
            // Save characters into the buffer (ignoring backspaces or array bounds overflow)
            else if (buffer_idx < sizeof(serial_buffer) - 1 && incoming_char >= 32) {
                serial_buffer[buffer_idx++] = incoming_char;
            }
        }
    }
    free(dtmp);
    vTaskDelete(NULL);
}

void init_serial_input(void) {
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    // Install UART driver using default settings
    uart_driver_install(EX_UART_NUM, BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(EX_UART_NUM, &uart_config);

    // Spawn the listening thread
    xTaskCreate(uart_monitor_rx_task, "uart_monitor_rx_task", 3072, NULL, 10, NULL);
    ESP_LOGI("UART_INPUT", "Serial Monitor listening task active. Type your S/N and press Enter!");
}

static void ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        char ip_str[16];
        esp_ip4addr_ntoa(&event->ip_info.ip, ip_str, sizeof(ip_str));
        ESP_LOGI("WIFI", "Got IP Address: %s", ip_str);
        
        // ==========================================================
        // SAFE AUTO-TRIGGER ON NETWORK UP / RESET
        // ==========================================================
        // Wi-Fi is up and working. Safely trigger API call to generate buttons.
        generate_dynamic_defect_buttons();

        if (ui_lblIPAddress != NULL) {
            lv_label_set_text(ui_lblIPAddress, ip_str);
        }
    }
}

void wifi_init_and_get_mac(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, 
                                                        IP_EVENT_STA_GOT_IP, 
                                                        &ip_event_handler, 
                                                        NULL, 
                                                        NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    uint8_t mac[6];
    esp_wifi_get_mac(ESP_IF_WIFI_STA, mac); 
    ESP_LOGI(APP_TAG, "ESP32 MAC Address: %02X:%02X:%02X:%02X:%02X:%02X", 
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(APP_TAG, "Connecting to Wi-Fi SSID: %s...", WIFI_SSID);
    esp_wifi_connect();
}

void app_main(void) {
    wifi_init_and_get_mac();
    display();
    ui_init();
    
    // REMOVED: generate_dynamic_defect_buttons() was removed from here.
    // It is handled asynchronously inside ip_event_handler to prevent network exceptions on boot.
    
    extern lv_obj_t * ui_Keyboard4; 
    extern lv_obj_t * ui_txtSerialNumber; 
    extern void keyboard_ready_click_cb(lv_event_t * e); 

    if (ui_Keyboard4 != NULL && ui_txtSerialNumber != NULL) {
        // Link typing output target field
        lv_keyboard_set_textarea(ui_Keyboard4, ui_txtSerialNumber);
        
        // Ensure keyboard starts hidden
        lv_obj_add_flag(ui_Keyboard4, LV_OBJ_FLAG_HIDDEN);
        
        // WATCH FOR THE ENTER/CHECKMARK (✓) BUTTON EVENT
        lv_obj_add_event_cb(ui_Keyboard4, keyboard_ready_click_cb, LV_EVENT_READY, NULL);
        
        ESP_LOGI(APP_TAG, "Keyboard submission event links configured perfectly.");
    }
    
    init_serial_input();

    // Map the external fail workflow directly to the corresponding layout object instance
    extern lv_obj_t * ui_btnFail; 
    if (ui_btnFail != NULL) {
        lv_obj_add_event_cb(ui_btnFail, fail_button_click_cb, LV_EVENT_CLICKED, NULL);
        ESP_LOGI(APP_TAG, "Hook connected perfectly for layout Failure operations.");
    }
}