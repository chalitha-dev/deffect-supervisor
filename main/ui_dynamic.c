#include "lvgl.h"
#include "ui/ui.h"
#include "esp_log.h"

// Explicit reference mapping to scan_logic setup
extern void defect_button_click_cb(lv_event_t * e);

typedef struct {
    int id;
    const char* code;
    const char* name;
} defect_t;

static const defect_t mock_api_defects[] = {
    {1, "ERR_01", "Surface Scratch"},
    {2, "ERR_02", "Dimension Mismatch"},
    {3, "ERR_03", "Cracked Housing"},
    {4, "ERR_04", "Discoloration"},
    {5, "ERR_05", "Loose Component"}
};

static const int mock_defect_count = sizeof(mock_api_defects) / sizeof(mock_api_defects[0]);

void generate_dynamic_defect_buttons(void) {
    if (ui_ButtonContainer == NULL) return; 

    lv_obj_clean(ui_ButtonContainer);
    lv_obj_set_style_pad_row(ui_ButtonContainer, 10, 0); 

    for (int i = 0; i < mock_defect_count; i++) {
        lv_obj_t * btn = lv_btn_create(ui_ButtonContainer);
        lv_obj_set_width(btn, lv_pct(95));
        lv_obj_set_height(btn, 50);
        
        btn->user_data = (void*)(intptr_t)mock_api_defects[i].id;

        lv_obj_t * label = lv_label_create(btn);
        lv_label_set_text_fmt(label, "[%s] %s", mock_api_defects[i].code, mock_api_defects[i].name);
        lv_obj_center(label);

        lv_obj_add_event_cb(btn, defect_button_click_cb, LV_EVENT_CLICKED, NULL);
    }
}