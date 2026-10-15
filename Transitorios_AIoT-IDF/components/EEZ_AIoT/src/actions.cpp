#include "actions.h"
#include "ui.h"
#include "vars.h"
#include "screens.h"

// System Headers
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h> // For strlen

// Hardware Module
#include "IO_AIoT.h" 

// C Linkage Headers
extern "C" {
    #include "WiFi_AIoT.h"
}

static const char *TAG = "UI_ACTIONS";

enum ConnectionMethod {
    METHOD_WIFI_MULTI = 0,
    METHOD_BLUETOOTH  = 1,
    METHOD_BOTH       = 2
};

// -------------------------------------------------------------------------
// 1. HELPER FUNCTIONS
// -------------------------------------------------------------------------

static void helper_update_visuals() {
    bool is_wifi_connected = get_wifi_is_connected();
    set_var_connec(is_wifi_connected);

    if (is_wifi_connected) {
        if (objects.ui_lab_ssid) lv_label_set_text(objects.ui_lab_ssid, get_wifi_ssid());
        if (objects.ui_lab_ip)   lv_label_set_text(objects.ui_lab_ip, get_wifi_ip());
        if (objects.ui_lab_dns)  lv_label_set_text(objects.ui_lab_dns, get_wifi_dns());
        if (objects.ui_lab_mac)  lv_label_set_text(objects.ui_lab_mac, get_wifi_mac());
    } else {
        if (objects.ui_lab_ssid) lv_label_set_text(objects.ui_lab_ssid, "Disconnected");
        if (objects.ui_lab_ip)   lv_label_set_text(objects.ui_lab_ip, "0.0.0.0");
    }

    // --- COLOR SYNC LOGIC (Green=Connected / Red=Disconnected) ---
    lv_color_t target_color = (is_wifi_connected) ? lv_color_hex(0x008000) : lv_color_hex(0xFF0000);

    // 1. Button Tab 1
    if (objects.bt_conectado_main3_tab1) {
        lv_obj_set_style_bg_color(objects.bt_conectado_main3_tab1, target_color, LV_PART_MAIN);
    }
    // 2. Button Tab 2
    if (objects.bt_conectado_main3_tab2) {
        lv_obj_set_style_bg_color(objects.bt_conectado_main3_tab2, target_color, LV_PART_MAIN);
    }
    
    // 3. WiFi Timer Label/Button
    if (objects.bt_dhms_wi_fi) {
        lv_obj_set_style_bg_color(objects.bt_dhms_wi_fi, target_color, LV_PART_MAIN);
    }
}

static void helper_perform_connect() {
    char ssid_buffer[64] = {0};
    if (objects.text_area_ssid) { 
        lv_dropdown_get_selected_str(objects.text_area_ssid, ssid_buffer, sizeof(ssid_buffer));
        set_var_text_area_ssid_value(ssid_buffer);
    }
    const char *pass_ptr = "";
    if (objects.text_area_password) {
        pass_ptr = lv_textarea_get_text(objects.text_area_password);
        set_var_text_area_pass_value(pass_ptr);
    }
    int32_t method = 0; 
    if (objects.drop_down_1) { 
        method = lv_dropdown_get_selected(objects.drop_down_1);
        set_var_drop_down_metodo(method);
    }

    if (method == METHOD_WIFI_MULTI || method == METHOD_BOTH) {
        if (strlen(ssid_buffer) > 0) wifi_connect(ssid_buffer, (char*)pass_ptr);
    }
    vTaskDelay(pdMS_TO_TICKS(200)); 
    helper_update_visuals();
}

static void event_keyboard_ready_cb(lv_event_t * e) {
    helper_perform_connect();
}

// -------------------------------------------------------------------------
// 2. EEZ STUDIO ACTIONS (Event Handlers)
// -------------------------------------------------------------------------

void action_fn_connec_aio_t(lv_event_t * e) { helper_perform_connect(); }

void action_fn_connec(lv_event_t * e) {
    int32_t method = 0;
    if (objects.drop_down_1) method = lv_dropdown_get_selected(objects.drop_down_1);

    if (method == METHOD_WIFI_MULTI || method == METHOD_BOTH) {
        char *list = wifi_scan_networks_get_list();
        if (list && objects.text_area_ssid) {
            lv_dropdown_set_options(objects.text_area_ssid, list);
            free(list);
        }
    }
    static bool tk_linked = false;
    if (!tk_linked && objects.keyboard) {
        lv_obj_add_event_cb(objects.keyboard, event_keyboard_ready_cb, LV_EVENT_READY, NULL);
        tk_linked = true;
    }
    helper_update_visuals();
}

void action_fn_re_scan(lv_event_t * e) {
    set_var_re_scan(true);
    action_fn_connec(e);
    set_var_re_scan(false);
}

// EVENT HANDLER
void action_fn_update_suspension(lv_event_t * e) {
    lv_obj_t * dropdown = (lv_obj_t *)lv_event_get_target(e);
    
    if (dropdown) {
        int32_t index = lv_dropdown_get_selected(dropdown);
        set_var_drop_down_suspender(index);
        IO_Set_Tiempo_Suspension(index);
        ESP_LOGI(TAG, "EVENT: Suspension Index %d", (int)index);
    }
}

// -------------------------------------------------------------------------
// 4. ACTION FUNCTION (REQUIRED BY LINKER)
// -------------------------------------------------------------------------

/**
 * @brief This is the event handler for the WiFi Timer Button, as defined in actions.h.
 * It must exist to satisfy the linker. The periodic update logic is in ui_update_periodic_task.
 */
void action_fn_label_dhms_wi_fi(lv_event_t * e) {
    // We don't need logic here, as the clock updates periodically.
    // If you want the clock to instantly update when the button is pressed, 
    // you could call the update logic here as well.
}


// -------------------------------------------------------------------------
// 3. PERIODIC TASK (LOGIC UPDATED HERE)
// -------------------------------------------------------------------------
extern "C" void ui_update_periodic_task(void)
{
    static uint32_t last_clock_update = 0;
    static uint32_t last_wifi_update = 0;
    static bool initial_sync_done = false;

    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

    // --- A. STARTUP SYNC ---
    if (!initial_sync_done && objects.drop_down_suspender) {
        int32_t default_index = get_var_drop_down_suspender(); 
        lv_dropdown_set_selected(objects.drop_down_suspender, default_index);
        IO_Set_Tiempo_Suspension(default_index);
        initial_sync_done = true;
    }

    // --- B. HARDWARE SYNC ---
    int32_t target_index = get_var_drop_down_suspender();
    IO_Set_Tiempo_Suspension(target_index);

    // --- C. BACKUP POLLING ---
    if (objects.drop_down_suspender) {
        int32_t visual_index = lv_dropdown_get_selected(objects.drop_down_suspender);
        if (visual_index != target_index) {
            set_var_drop_down_suspender(visual_index); 
        }
    }

    // --- D. TIMERS (System & WiFi) ---
    // This section runs every 1 second (1000ms)
    if ((now - last_clock_update) >= 1000) {
        
        // 1. System Uptime (Standard Clock)
        char time_str[32];
        IO_Get_Uptime(time_str, sizeof(time_str));
        set_var_label_dhms_1(time_str);
        set_var_label_dhms_2(time_str);
        
        // Safety direct update
        if (objects.label_dhms_1) lv_label_set_text(objects.label_dhms_1, time_str);
        if (objects.label_dhms_2) lv_label_set_text(objects.label_dhms_2, time_str);

        // 2. WiFi Connection Timer (FINAL FIX)
        // Gets the time string (00:00:00 or current time)
        char wifi_time_str[32];
        WiFi_Get_Connection_Time_String(wifi_time_str, sizeof(wifi_time_str));
        
        // CRITICAL: Update the LVGL object directly to make it "walk" and reset to zero.
        if (objects.label_dhms_wi_fi) lv_label_set_text(objects.label_dhms_wi_fi, wifi_time_str);

        // Update the EEZ Studio variable (for consistency)
        set_var_label_dhms_wi_fi(wifi_time_str);

        last_clock_update = now;
    }

    if ((now - last_wifi_update) >= 500) {
        helper_update_visuals(); // Updates Colors (Green/Red)
        last_wifi_update = now;
    }

    IO_Set_Brillo_Manual(get_var_slider_porcentaje());
}