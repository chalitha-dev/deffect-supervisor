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

// Helper function to reset the internal scanner tracking variables
static void clear_scanner_state(void) {
    memset(current_serial_number, 0, sizeof(current_serial_number));
    has_pending_scan = false;
    ESP_LOGI(TAG, "Scanner state explicitly cleared.");
}

void send_scan_result_to_api(const char* serial, const char* defect_code, const char* status) {
    esp_http_client_config_t config = {
        .url = "http://192.168.2.179:3000/api/v1/deffect/superdata", 
        .method = HTTP_METHOD_POST,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    char post_data[512];
    
    if (strcmp(status, "FAIL") == 0) {
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

    // References to your UI elements
    extern lv_obj_t * ui_lblPostStatus;
    extern lv_obj_t * ui_lblSelectedDeffectCode;
    extern lv_obj_t * ui_txtSerialNumber;

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP POST Success! Status Code = %d", status_code);
        
        if (status_code >= 200 && status_code < 300) {
            // 1. Clear selected defect code UI text first
            if (ui_lblSelectedDeffectCode != NULL) {
                lv_label_set_text(ui_lblSelectedDeffectCode, "");
            }

            // 2. Clear input text area
            if (ui_txtSerialNumber != NULL) {
                lv_textarea_set_text(ui_txtSerialNumber, "");
            }

            // 3. Update status label to green success alert with variables
            if (ui_lblPostStatus != NULL) {
                lv_obj_set_style_text_color(ui_lblPostStatus, lv_color_make(0, 180, 0), LV_PART_MAIN | LV_STATE_DEFAULT); 
                
                if (strcmp(status, "FAIL") == 0) {
                    lv_label_set_text_fmt(ui_lblPostStatus, "Success: %s Failed (%s)", serial, defect_code);
                } else {
                    lv_label_set_text_fmt(ui_lblPostStatus, "Success: %s Passed", serial);
                }
            }
        } else {
            // API responded but returned an error status code (e.g., 400, 500)
            if (ui_lblSelectedDeffectCode != NULL) {
                lv_label_set_text(ui_lblSelectedDeffectCode, "");
            }

            if (ui_lblPostStatus != NULL) {
                lv_obj_set_style_text_color(ui_lblPostStatus, lv_color_make(255, 0, 0), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_label_set_text_fmt(ui_lblPostStatus, "Error Status: %d", status_code);
            }
        }
    } else {
        // Network link down / Timeout exception
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));

        if (ui_lblSelectedDeffectCode != NULL) {
            lv_label_set_text(ui_lblSelectedDeffectCode, "");
        }

        if (ui_lblPostStatus != NULL) {
            lv_obj_set_style_text_color(ui_lblPostStatus, lv_color_make(255, 0, 0), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_fmt(ui_lblPostStatus, "Failed: %s", esp_err_to_name(err));
        }
    }
    esp_http_client_cleanup(client);
}

// ========================================================
// Defect Button Selection Callback
// ========================================================
void defect_button_click_cb(lv_event_t * e) {
    lv_obj_t * btn = lv_event_get_target(e);
    const char *code = (const char*)btn->user_data; 
    
    if (code != NULL) {
		lv_label_set_text(ui_lblPostStatus, "");
        strncpy(selected_defect_code, code, sizeof(selected_defect_code) - 1);
        selected_defect_code[sizeof(selected_defect_code) - 1] = '\0';
        ESP_LOGI(TAG, "Defect selected. Holding Code: %s", selected_defect_code);
    }
}

// ========================================================
// Fail Button Callback
// ========================================================
void fail_button_click_cb(lv_event_t * e) {
    extern lv_obj_t * ui_lblPostStatus;
    extern lv_obj_t * ui_lblSelectedDeffectCode;

    // 1. Check if Serial Number is Missing
    if (!has_pending_scan || strlen(current_serial_number) == 0) {
        ESP_LOGW(TAG, "Cannot Fail: No serial number is currently active.");
        
        if (ui_lblSelectedDeffectCode != NULL) {
            lv_label_set_text(ui_lblSelectedDeffectCode, "");
        }
        if (ui_lblPostStatus != NULL) {
            lv_obj_set_style_text_color(ui_lblPostStatus, lv_color_make(255, 0, 0), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text(ui_lblPostStatus, "Error: Missing Serial Number!");
        }
        return;
    }

    // 2. Check if Defect Code is Missing
    if (strlen(selected_defect_code) == 0) {
        ESP_LOGW(TAG, "Cannot Fail: Please pick a defect code component first.");
        
        if (ui_lblSelectedDeffectCode != NULL) {
            lv_label_set_text(ui_lblSelectedDeffectCode, "");
        }
        if (ui_lblPostStatus != NULL) {
            lv_obj_set_style_text_color(ui_lblPostStatus, lv_color_make(255, 0, 0), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text(ui_lblPostStatus, "Error: Missing Defect Code!");
        }
        return;
    }

    send_scan_result_to_api(current_serial_number, selected_defect_code, "FAIL");
    clear_scanner_state();
}

// ========================================================
// Finish Button Callback (Pass Execution Logic)
// ========================================================
void finish_button_click_cb(lv_event_t * e) {
    extern lv_obj_t * ui_lblPostStatus;
    extern lv_obj_t * ui_lblSelectedDeffectCode;

    // Check if Serial Number is Missing for Pass execution
    if (!has_pending_scan || strlen(current_serial_number) == 0) {
        ESP_LOGW(TAG, "Finish button touched, but no active serial exists to pass.");
        
        if (ui_lblSelectedDeffectCode != NULL) {
            lv_label_set_text(ui_lblSelectedDeffectCode, "");
        }
        if (ui_lblPostStatus != NULL) {
            lv_obj_set_style_text_color(ui_lblPostStatus, lv_color_make(255, 0, 0), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text(ui_lblPostStatus, "Error: Missing Serial Number!");
        }
        return;
    }

    ESP_LOGI(TAG, "Finish button touched. Posting current serial [%s] as PASS.", current_serial_number);
    send_scan_result_to_api(current_serial_number, "", "PASS");
    
    // Clear out serial number and defect codes entirely after submission
    clear_scanner_state();
}

// ========================================================
// Scanned Input Workflow Controller
// ========================================================
void on_serial_number_scanned(const char* new_serial) {
    // If a workflow is already pending, take the old one and register it as a PASS
    if (has_pending_scan && strlen(current_serial_number) > 0) {
        ESP_LOGI(TAG, "New scan detected. Auto-releasing previous serial [%s] as PASS.", current_serial_number);
        send_scan_result_to_api(current_serial_number, "", "PASS"); 
    }

    // Capture the newly scanned item into storage
    strncpy(current_serial_number, new_serial, sizeof(current_serial_number) - 1);
    current_serial_number[sizeof(current_serial_number) - 1] = '\0';
    
    // Reset selected defects for the incoming new item
    memset(selected_defect_code, 0, sizeof(selected_defect_code));
    has_pending_scan = true;

    ESP_LOGI(TAG, "Holding newly active serial key: %s", current_serial_number);
}

// ========================================================
// Keyboard Enter / Ready Callback
// ========================================================
void keyboard_ready_click_cb(lv_event_t * e) {
    lv_obj_t * kb = lv_event_get_target(e);
    lv_obj_t * ta = lv_keyboard_get_textarea(kb);
    
    if (ta != NULL) {
        const char * text = lv_textarea_get_text(ta);
        if (strlen(text) > 0) {
            ESP_LOGI(TAG, "Enter pressed! Activating Serial: %s", text);
            on_serial_number_scanned(text);
            lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        } else {
            // Screen handling if user clicks checkmark on an completely blank text block
            extern lv_obj_t * ui_lblPostStatus;
            extern lv_obj_t * ui_lblSelectedDeffectCode;
            
            if (ui_lblSelectedDeffectCode != NULL) {
                lv_label_set_text(ui_lblSelectedDeffectCode, "");
            }
            if (ui_lblPostStatus != NULL) {
                lv_obj_set_style_text_color(ui_lblPostStatus, lv_color_make(255, 0, 0), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_label_set_text(ui_lblPostStatus, "Error: Missing Serial Number!");
            }
            ESP_LOGW(TAG, "Enter pressed, but input field is completely empty!");
        }
    }
}