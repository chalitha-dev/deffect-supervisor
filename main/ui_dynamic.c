#include "lvgl.h"
#include "ui/ui.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static const char *TAG = "UI_DYNAMIC";
#define DEFECT_API_URL "http://192.168.2.179:3000/api/v1/deffect/codes"
#define DATABASE_SYNC_URL "http://192.168.2.179:3000/api/v1/deffect/sync-counts"

void generate_dynamic_defect_buttons(void);
extern void defect_button_click_cb(lv_event_t * e);

static void build_defect_buttons_ui(void); 
static void ui_update_async_cb(void * p);

typedef struct {
    char code[32];
    char name[64];
    uint32_t click_count; 
    bool is_selected; 
} dynamic_defect_t;

static dynamic_defect_t *api_defects = NULL;
static int api_defect_count = 0;

static TaskHandle_t fetch_task_handle = NULL;
static TaskHandle_t sync_task_handle = NULL;

typedef struct {
    char *buffer;
    int data_len;
    int buffer_size;
} http_response_buffer_t;

static int natural_code_compare(const char *s1, const char *s2) {
    while (*s1 && *s2 && !isdigit((unsigned char)*s1) && !isdigit((unsigned char)*s2)) {
        if (*s1 != *s2) return (int)((unsigned char)*s1 - (unsigned char)*s2);
        s1++; s2++;
    }
    if (isdigit((unsigned char)*s1) && isdigit((unsigned char)*s2)) {
        int val1 = atoi(s1);
        int val2 = atoi(s2);
        if (val1 != val2) return val1 - val2;
        while (isdigit((unsigned char)*s1)) s1++;
        while (isdigit((unsigned char)*s2)) s2++;
    }
    return strcmp(s1, s2);
}

// FIXED: Removed click_count comparison to maintain a permanent general ascending order
static int compare_defects(const void *a, const void *b) {
    const dynamic_defect_t *defectA = (const dynamic_defect_t *)a;
    const dynamic_defect_t *defectB = (const dynamic_defect_t *)b;
    return natural_code_compare(defectA->code, defectB->code);
}

static void custom_defect_button_handler(lv_event_t * e) {
    lv_obj_t * btn = lv_event_get_target(e);
    char * target_code = (char *)btn->user_data;

    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        if (target_code != NULL && api_defects != NULL) {
            
            // Now update selection properties globally inside the data array
            for (int i = 0; i < api_defect_count; i++) {
                if (strcmp(api_defects[i].code, target_code) == 0) {
                    api_defects[i].click_count++;
                    api_defects[i].is_selected = true;

                    // FIXED: Update the external visual tracking label to format: [code] name
                    if (ui_lblSelectedDeffectCode != NULL) {
                        lv_label_set_text_fmt(ui_lblSelectedDeffectCode, "[%s] %s", api_defects[i].code, api_defects[i].name);
                    }
                } else {
                    api_defects[i].is_selected = false;
                }
            }
            
            // Run the external callback logic
            defect_button_click_cb(e);
            
            // Array layout order does not change anymore, but we refresh to apply the visual CHECKED state
            lv_async_call(ui_update_async_cb, NULL);
        }
    }
}

static void custom_ui_refresh_handler(lv_event_t * e) {
    ESP_LOGI(TAG, "Refresh Intercepted! Clearing local counts...");
    if (api_defects != NULL) {
        for (int i = 0; i < api_defect_count; i++) {
            api_defects[i].click_count = 0;
            api_defects[i].is_selected = false;
        }
    }
    if (ui_ButtonContainer != NULL) {
        lv_obj_clean(ui_ButtonContainer);
    }
    generate_dynamic_defect_buttons();
}

static esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    http_response_buffer_t *res_buf = (http_response_buffer_t *)evt->user_data;
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (evt->data_len > 0 && res_buf != NULL) {
                if (res_buf->data_len + evt->data_len >= res_buf->buffer_size) {
                    int new_size = res_buf->buffer_size + evt->data_len + 4096;
                    char *new_buf = realloc(res_buf->buffer, new_size);
                    if (new_buf == NULL) return ESP_FAIL;
                    res_buf->buffer = new_buf;
                    res_buf->buffer_size = new_size;
                }
                memcpy(res_buf->buffer + res_buf->data_len, evt->data, evt->data_len);
                res_buf->data_len += evt->data_len;
                res_buf->buffer[res_buf->data_len] = '\0';
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

static void build_defect_buttons_ui(void) {
    if (ui_ButtonContainer == NULL || api_defects == NULL || api_defect_count == 0) return;

    lv_obj_clear_flag(ui_ButtonContainer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clean(ui_ButtonContainer);
    
    lv_obj_set_style_pad_row(ui_ButtonContainer, 6, 0); 
    lv_obj_set_style_pad_column(ui_ButtonContainer, 6, 0); 

    lv_obj_set_layout(ui_ButtonContainer, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(ui_ButtonContainer, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(ui_ButtonContainer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_set_scroll_dir(ui_ButtonContainer, LV_DIR_VER);
    lv_obj_add_flag(ui_ButtonContainer, LV_OBJ_FLAG_SCROLLABLE); 
    lv_obj_set_scrollbar_mode(ui_ButtonContainer, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_clear_flag(ui_ButtonContainer, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);

    lv_obj_t *focused_btn = NULL;

    for (int i = 0; i < api_defect_count; i++) {
        lv_obj_t * btn = lv_btn_create(ui_ButtonContainer);
        
        // FIXED: Extended Grid structure from 10 columns to 12 columns (~7% width leaves room for paddings)
        lv_obj_set_width(btn, lv_pct(7));
        lv_obj_set_height(btn, 45); 
        btn->user_data = (void*)api_defects[i].code; 

        lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE | LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE); 

        // Default Unchecked State Styles (Green background)
        lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

        // Checked State Styles (Yellow background)
        lv_obj_set_style_bg_color(btn, lv_color_hex(0xDFD903), LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_CHECKED);

        // Retain Checked toggle states on redraws
        if (api_defects[i].is_selected) {
            lv_obj_add_state(btn, LV_STATE_CHECKED);
            focused_btn = btn; 
        }

        // Displays ONLY the Defect Code inside the grid button
        lv_obj_t * label = lv_label_create(btn);
        lv_label_set_text(label, api_defects[i].code);
        lv_obj_center(label);

        #if defined(LV_FONT_MONTSERRAT_16)
            lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
        #elif defined(LV_FONT_MONTSERRAT_14)
            lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        #endif

        lv_obj_add_event_cb(btn, custom_defect_button_handler, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_update_layout(ui_ButtonContainer);
    lv_obj_invalidate(ui_ButtonContainer);

    if (focused_btn != NULL) {
        lv_obj_scroll_to_view(focused_btn, LV_ANIM_OFF);
    } else {
        lv_obj_scroll_to_y(ui_ButtonContainer, 0, LV_ANIM_OFF);
    }
}

static void ui_update_async_cb(void * p) {
    build_defect_buttons_ui();
}

static void hourly_database_sync_task(void *pvParameters) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(3600000)); 
        
        if (api_defects == NULL || api_defect_count == 0) continue;

        ESP_LOGI(TAG, "Initiating hourly database synchronization pipeline...");

        cJSON *root = cJSON_CreateObject();
        cJSON *data_arr = cJSON_CreateArray();
        cJSON_AddItemToObject(root, "sync_data", data_arr);

        for (int i = 0; i < api_defect_count; i++) {
            cJSON *item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "DeffectCode", api_defects[i].code);
            cJSON_AddNumberToObject(item, "TotalClicks", api_defects[i].click_count);
            cJSON_AddItemToArray(data_arr, item);
        }

        char *post_string = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);

        if (post_string != NULL) {
            esp_http_client_config_t config = {
                .url = DATABASE_SYNC_URL,
                .method = HTTP_METHOD_POST,
                .timeout_ms = 5000,
            };
            
            esp_http_client_handle_t client = esp_http_client_init(&config);
            if (client != NULL) {
                esp_http_client_set_post_field(client, post_string, strlen(post_string));
                esp_http_client_set_header(client, "Content-Type", "application/json");

                esp_err_t err = esp_http_client_perform(client);
                if (err == ESP_OK) {
                    int status = esp_http_client_get_status_code(client);
                    ESP_LOGI(TAG, "Sync server response code achieved: %d", status);
                } else {
                    ESP_LOGE(TAG, "Sync operation performance failure across link pipeline.");
                }
                esp_http_client_cleanup(client);
            }
            free(post_string);
        }
    }
}

static void fetch_defects_task(void *pvParameters) {
    bool fetch_success = false;
    http_response_buffer_t res_buf = { .buffer_size = 8192, .data_len = 0, .buffer = malloc(8192) };
    if (res_buf.buffer == NULL) { fetch_task_handle = NULL; vTaskDelete(NULL); return; }

    while (!fetch_success) {
        res_buf.data_len = 0; res_buf.buffer[0] = '\0';
        esp_http_client_config_t config = {
            .url = DEFECT_API_URL, .method = HTTP_METHOD_GET,
            .event_handler = _http_event_handler, .user_data = &res_buf,
            .timeout_ms = 7000, .keep_alive_enable = true,
        };
        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client == NULL) { vTaskDelay(pdMS_TO_TICKS(5000)); continue; }

        if (esp_http_client_perform(client) == ESP_OK) {
            if (esp_http_client_get_status_code(client) == 200 && res_buf.data_len > 0) {
                cJSON *root = cJSON_Parse(res_buf.buffer);
                if (root != NULL) {
                    cJSON *data_array = cJSON_GetObjectItemCaseSensitive(root, "data");
                    if (data_array != NULL && cJSON_IsArray(data_array)) {
                        api_defect_count = cJSON_GetArraySize(data_array);
                        if (api_defect_count > 0) {
                            if (api_defects != NULL) free(api_defects);
                            api_defects = malloc(sizeof(dynamic_defect_t) * api_defect_count);
                            for (int i = 0; i < api_defect_count; i++) {
                                cJSON *item = cJSON_GetArrayItem(data_array, i);
                                cJSON *code = cJSON_GetObjectItemCaseSensitive(item, "DeffectCode");
                                cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "DeffectName");
                                cJSON *clicks = cJSON_GetObjectItemCaseSensitive(item, "total_clicks");
                                
                                if (code && name && code->valuestring && name->valuestring) {
                                    strncpy(api_defects[i].code, code->valuestring, sizeof(api_defects[i].code) - 1);
                                    api_defects[i].code[sizeof(api_defects[i].code) - 1] = '\0';
                                    strncpy(api_defects[i].name, name->valuestring, sizeof(api_defects[i].name) - 1);
                                    api_defects[i].name[sizeof(api_defects[i].name) - 1] = '\0';
                                    api_defects[i].is_selected = false;
                                    
                                    if (clicks && clicks->valuestring) {
                                        api_defects[i].click_count = (uint32_t)atoi(clicks->valuestring);
                                    } else {
                                        api_defects[i].click_count = 0;
                                    }
                                }
                            }
                            qsort(api_defects, api_defect_count, sizeof(dynamic_defect_t), compare_defects);
                            fetch_success = true; 
                        }
                    }
                    cJSON_Delete(root);
                }
            }
        }
        esp_http_client_cleanup(client);
        if (!fetch_success) vTaskDelay(pdMS_TO_TICKS(5000));
    }
    free(res_buf.buffer);
    lv_async_call(ui_update_async_cb, NULL);
    fetch_task_handle = NULL;
    vTaskDelete(NULL); 
}

void generate_dynamic_defect_buttons(void) {
    if (ui_Screen1 != NULL) {
        lv_obj_clear_flag(ui_Screen1, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(ui_Screen1, LV_DIR_NONE);
    }

    if (ui_btnRefresh != NULL) {
        lv_obj_add_flag(ui_btnRefresh, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_event_cb(ui_btnRefresh, custom_ui_refresh_handler);
        lv_obj_add_event_cb(ui_btnRefresh, custom_ui_refresh_handler, LV_EVENT_CLICKED, NULL);
    }

    if (fetch_task_handle != NULL) { vTaskDelete(fetch_task_handle); fetch_task_handle = NULL; }
    xTaskCreate(fetch_defects_task, "fetch_defects_task", 8192, NULL, 5, &fetch_task_handle);

    if (sync_task_handle == NULL) {
        xTaskCreate(hourly_database_sync_task, "hourly_database_sync_task", 8192, NULL, 4, &sync_task_handle);
    }
}