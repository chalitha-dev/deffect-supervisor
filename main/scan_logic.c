#include "esp_http_client.h"
#include "esp_log.h"
#include "lvgl.h"
#include "ui/ui.h"
#include <string.h>

static const char *TAG = "SCAN_LOGIC";

// Hardcoded System/Environment Definitions (Matching your backend requirements)
#define DEVICE_ID    "ESP-001"
#define SUPER_ID     "1139"
#define DEPARTMENT   "FBT"

// Centralized state tracking
static char current_serial_number[64] = "";
static int selected_defect_id = -1; 
static bool has_pending_scan = false;

// Function to send matching payload to the API
void send_scan_result_to_api(const char* serial, int defect_id, const char* status) {
    esp_http_client_config_t config = {
        .url = "http://192.168.2.179:3000/api/v1/deffect/superdata", 
        .method = HTTP_METHOD_POST,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // Create JSON body matching your target payload structure exactly
    char post_data[512];
    
    if (strcmp(status, "FAIL") == 0) {
        // If it's a failure, we include the status as "FAIL" and append the defect logic/ID
        snprintf(post_data, sizeof(post_data), 
                 "{"
                 "\"device_id\":\"%s\","
                 "\"supervisor\":\"%s\","
                 "\"serial\":\"%s\","
                 "\"dept\":\"%s\","
                 "\"status\":\"FAIL\","
                 "\"defect_id\":%d"
                 "}", 
                 DEVICE_ID, SUPER_ID, serial, DEPARTMENT, defect_id);
    } else {
        // Normal progression scan (Auto pass or continued scanning)
        snprintf(post_data, sizeof(post_data), 
                 "{"
                 "\"device_id\":\"%s\","
                 "\"supervisor\":\"%s\","
                 "\"serial\":\"%s\","
                 "\"dept\":\"%s\","
                 "\"status\":\"PASS\""
                 "}", 
                 DEVICE_ID, SUPER_ID, serial, DEPARTMENT);
    }

    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    esp_http_client_set_header(client, "Content-Type", "application/json");

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP POST Success! Status Code = %d", esp_http_client_get_status_code(client));
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

// ========================================================
// Defect Button Selection Callback
// ========================================================
void defect_button_click_cb(lv_event_t * e) {
    lv_obj_t * btn = lv_event_get_target(e);
    selected_defect_id = (int)(intptr_t)btn->user_data;
    ESP_LOGI(TAG, "Defect selected. Holding Defect ID: %d", selected_defect_id);
}

// ========================================================
// Fail Button Callback
// ========================================================
void fail_button_click_cb(lv_event_t * e) {
    if (!has_pending_scan || strlen(current_serial_number) == 0) {
        ESP_LOGW(TAG, "Cannot Fail: No serial number is currently active.");
        return;
    }

    if (selected_defect_id == -1) {
        ESP_LOGW(TAG, "Cannot Fail: Please pick a defect code component first.");
        return;
    }

    // Submit an explicit failure status
    send_scan_result_to_api(current_serial_number, selected_defect_id, "FAIL");

    // Clear state
    memset(current_serial_number, 0, sizeof(current_serial_number));
    selected_defect_id = -1;
    has_pending_scan = false;
}

// ========================================================
// Scanned Input Workflow Controller
// ========================================================
void on_serial_number_scanned(const char* new_serial) {
    // Decision Hold Rule: If a scan already exists, release it passively as a PASS
    if (has_pending_scan && strlen(current_serial_number) > 0) {
        ESP_LOGI(TAG, "Continuous scan. Auto-releasing previous serial [%s] as PASS.", current_serial_number);
        send_scan_result_to_api(current_serial_number, -1, "PASS"); 
    }

    // Hold the new item in state
    strncpy(current_serial_number, new_serial, sizeof(current_serial_number) - 1);
    current_serial_number[sizeof(current_serial_number) - 1] = '\0';
    
    selected_defect_id = -1; // Ready for clear operations
    has_pending_scan = true;

    ESP_LOGI(TAG, "Holding newly active serial key: %s", current_serial_number);
}

void keyboard_ready_click_cb(lv_event_t * e) {
    lv_obj_t * kb = lv_event_get_target(e);
    
    // Get the target text area connected to this keyboard
    lv_obj_t * ta = lv_keyboard_get_textarea(kb);
    
    if (ta != NULL) {
        const char * text = lv_textarea_get_text(ta);
        
        if (strlen(text) > 0) {
            ESP_LOGI("SCAN_LOGIC", "Enter pressed! Activating Serial: %s", text);
            
            // Send text directly into your state processing pipeline
            on_serial_number_scanned(text);
            
            // Automatically hide the keyboard after hitting enter/ready
            lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        } else {
            ESP_LOGW("SCAN_LOGIC", "Enter pressed, but input field is completely empty!");
        }
    }
}