#ifndef EEZ_LVGL_UI_VARS_H
#define EEZ_LVGL_UI_VARS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// enum declarations



// Flow global variables

enum FlowGlobalVariables {
    FLOW_GLOBAL_VARIABLE_NONE
};

// Native global variables

extern int32_t get_var_slider_porcentaje();
extern void set_var_slider_porcentaje(int32_t value);
extern bool get_var_connec();
extern void set_var_connec(bool value);
extern bool get_var_re_scan();
extern void set_var_re_scan(bool value);
extern const char *get_var_text_area_ssid_value();
extern void set_var_text_area_ssid_value(const char *value);
extern const char *get_var_text_area_pass_value();
extern void set_var_text_area_pass_value(const char *value);
extern int32_t get_var_drop_down_suspender();
extern void set_var_drop_down_suspender(int32_t value);
extern int32_t get_var_drop_down_metodo();
extern void set_var_drop_down_metodo(int32_t value);
extern const char *get_var_ui_lab_ssid();
extern void set_var_ui_lab_ssid(const char *value);
extern const char *get_var_ui_lab_ip();
extern void set_var_ui_lab_ip(const char *value);
extern const char *get_var_ui_lab_dns();
extern void set_var_ui_lab_dns(const char *value);
extern const char *get_var_ui_lab_mac();
extern void set_var_ui_lab_mac(const char *value);
extern const char *get_var_label_dhms_1();
extern void set_var_label_dhms_1(const char *value);
extern const char *get_var_label_dhms_2();
extern void set_var_label_dhms_2(const char *value);
extern const char *get_var_label_dhms_wi_fi();
extern void set_var_label_dhms_wi_fi(const char *value);


#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_VARS_H*/