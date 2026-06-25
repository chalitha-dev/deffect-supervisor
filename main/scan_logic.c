#include "esp_http_client.h"
#include "esp_log.h"
#include "lvgl.h"
#include "ui/ui.h"
#include <string.h>

static const char *TAG = "SCAN_LOGIC";

#define DEVICE_ID    "ESP-001"
#define SUPER_ID     "1139"
#define DEPARTMENT   "FBT"


static char current_serial_number[64] = "";
static char selected_defect_code[32] = "";
static bool has_pending_scan = false;

void send_scan_result_to_api(const char* serial, const char* defect_code, const char* status) {
    esp_http_client_config_t config = {
        .url = "http://192.168.2.179:3000/api/v1/deffect/superdata", 
        .method = HTTP_METHOD_POST,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    char post_data[512];
    
    if (strcmp(status, "FAIL") == 0) {
        // Removed defect_id completely from the JSON structure
        snprintf(post_data, sizeof(post_data), 
                 "{"
                 "\"device_id\":\"%s\","
                 "\"supervisor\":\"%s\","
                 "\"serial\":\"%s\","
                 "\"dept\":\"%s\","
                 "\"status\":\"FAIL\","
                 "\"defect_code\":\"%s\""
                 "}", 
                 DEVICE_ID, SUPER_ID, serial, DEPARTMENT, defect_code);
    } else {
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
    const char *code = (const char*)btn->user_data; // Directly using user_data as string pointer
    
    if (code != NULL) {
        strncpy(selected_defect_code, code, sizeof(selected_defect_code) - 1);
        selected_defect_code[sizeof(selected_defect_code) - 1] = '\0';
        
        ESP_LOGI(TAG, "Defect selected. Holding Code: %s", selected_defect_code);
    }
}

// ========================================================
// Fail Button Callback
// ========================================================
void fail_button_click_cb(lv_event_t * e) {
    if (!has_pending_scan || strlen(current_serial_number) == 0) {
        ESP_LOGW(TAG, "Cannot Fail: No serial number is currently active.");
        return;
    }

    if (strlen(selected_defect_code) == 0) {
        ESP_LOGW(TAG, "Cannot Fail: Please pick a defect code component first.");
        return;
    }

    // Pass only code downstream
    send_scan_result_to_api(current_serial_number, selected_defect_code, "FAIL");

    // Clear state completely
    memset(current_serial_number, 0, sizeof(current_serial_number));
    memset(selected_defect_code, 0, sizeof(selected_defect_code));
    has_pending_scan = false;
}

// ========================================================
// Scanned Input Workflow Controller
// ========================================================
void on_serial_number_scanned(const char* new_serial) {
    if (has_pending_scan && strlen(current_serial_number) > 0) {
        ESP_LOGI(TAG, "Continuous scan. Auto-releasing previous serial [%s] as PASS.", current_serial_number);
        send_scan_result_to_api(current_serial_number, "", "PASS"); 
    }

    strncpy(current_serial_number, new_serial, sizeof(current_serial_number) - 1);
    current_serial_number[sizeof(current_serial_number) - 1] = '\0';
    
    memset(selected_defect_code, 0, sizeof(selected_defect_code));
    has_pending_scan = true;

    ESP_LOGI(TAG, "Holding newly active serial key: %s", current_serial_number);
}

void keyboard_ready_click_cb(lv_event_t * e) {
    lv_obj_t * kb = lv_event_get_target(e);
    lv_obj_t * ta = lv_keyboard_get_textarea(kb);
    
    if (ta != NULL) {
        const char * text = lv_textarea_get_text(ta);
        if (strlen(text) > 0) {
            ESP_LOGI("SCAN_LOGIC", "Enter pressed! Activating Serial: %s", text);
            on_serial_number_scanned(text);
            lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        } else {
            ESP_LOGW("SCAN_LOGIC", "Enter pressed, but input field is completely empty!");
        }
    }
}