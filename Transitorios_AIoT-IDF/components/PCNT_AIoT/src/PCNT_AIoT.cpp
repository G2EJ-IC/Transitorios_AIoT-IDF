/*
 * File: components/PCNT_AIoT/src/PCNT_AIoT.cpp
 * Description: Hardware pulse counter library for ESP-IDF (PCNT).
 *              1. C++ class PCNT_AIoT: the real logic.
 *              2. C bridge (extern "C"): the public API of PCNT_AIoT.h.
 * Author: Ing. Ernesto José Guerrero González
 * Standards: ESP-IDF v5.5.1, C++. English comments.
 */
#include "PCNT_AIoT.h"

#include <new>
#include "driver/pulse_cnt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "PCNT_AIoT";

/* Hardware counter limits: with accum_count the driver extends them in software */
static constexpr int kHighLimit = 32767;
static constexpr int kLowLimit  = -32768;

// -----------------------------------------------------------------------------
// 1. C++ CLASS (real logic)
// -----------------------------------------------------------------------------
class PCNT_AIoT {
private:
    int                   _gpio;
    uint32_t              _glitch_ns;
    float                 _k_ppl;            // pulses per liter, 0 = not set
    pcnt_unit_handle_t    _unit;             // nullptr until begin()
    pcnt_channel_handle_t _chan;
    bool                  _enabled;
    int                   _last_count;       // reference for the frequency
    int64_t               _last_time_us;
    float                 _last_hz;
    SemaphoreHandle_t     _lock;

    void releaseUnit() {
        if (_unit != nullptr) {
            if (_enabled) {
                pcnt_unit_stop(_unit);
                pcnt_unit_disable(_unit);
                _enabled = false;
            }
            if (_chan != nullptr) {
                pcnt_del_channel(_chan);
                _chan = nullptr;
            }
            pcnt_del_unit(_unit);
            _unit = nullptr;
        }
    }

public:
    // Constructor: stores the configuration only
    PCNT_AIoT(int gpio, uint32_t glitch_ns)
        : _gpio(gpio), _glitch_ns(glitch_ns), _k_ppl(0.0f), _unit(nullptr), _chan(nullptr),
          _enabled(false), _last_count(0), _last_time_us(0), _last_hz(0.0f),
          _lock(xSemaphoreCreateMutex()) {}

    // Destructor: releases only what this object created
    ~PCNT_AIoT() {
        releaseUnit();
        if (_lock != nullptr) vSemaphoreDelete(_lock);
    }

    bool isValid() const { return _lock != nullptr; }

    // Hardware initialization
    esp_err_t begin() {
        if (_unit != nullptr) {
            return ESP_OK;
        }

        pcnt_unit_config_t ucfg = {};
        ucfg.low_limit         = kLowLimit;
        ucfg.high_limit        = kHighLimit;
        ucfg.flags.accum_count = 1;          // extend the 16-bit counter in software
        esp_err_t err = pcnt_new_unit(&ucfg, &_unit);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "No free PCNT unit: %s", esp_err_to_name(err));
            _unit = nullptr;
            return err;
        }

        if (_glitch_ns > 0) {
            pcnt_glitch_filter_config_t fcfg = {};
            fcfg.max_glitch_ns = _glitch_ns;
            err = pcnt_unit_set_glitch_filter(_unit, &fcfg);
        }

        if (err == ESP_OK) {
            pcnt_chan_config_t ccfg = {};
            ccfg.edge_gpio_num  = _gpio;     // input with pull-up
            ccfg.level_gpio_num = -1;
            err = pcnt_new_channel(_unit, &ccfg, &_chan);
        }
        if (err == ESP_OK) {
            // Rising edge counts, falling edge is ignored
            err = pcnt_channel_set_edge_action(_chan, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                               PCNT_CHANNEL_EDGE_ACTION_HOLD);
        }
        if (err == ESP_OK) err = pcnt_unit_add_watch_point(_unit, kHighLimit);   // needed by accum_count
        if (err == ESP_OK) err = pcnt_unit_enable(_unit);
        if (err == ESP_OK) {
            _enabled = true;
            err = pcnt_unit_clear_count(_unit);
        }
        if (err == ESP_OK) err = pcnt_unit_start(_unit);

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "PCNT configuration error: %s", esp_err_to_name(err));
            releaseUnit();
            return err;
        }

        _last_count   = 0;
        _last_time_us = esp_timer_get_time();
        _last_hz      = 0.0f;
        ESP_LOGI(TAG, "Pulse counter started -> GPIO %d, glitch filter %u ns",
                 _gpio, (unsigned)_glitch_ns);
        return ESP_OK;
    }

    esp_err_t count(int64_t *out) {
        if (_unit == nullptr || out == nullptr) {
            return ESP_ERR_INVALID_STATE;
        }
        int value = 0;
        esp_err_t err = pcnt_unit_get_count(_unit, &value);
        if (err == ESP_OK) {
            *out = (int64_t)value;
        }
        return err;
    }

    esp_err_t clear() {
        if (_unit == nullptr) {
            return ESP_ERR_INVALID_STATE;
        }
        xSemaphoreTake(_lock, portMAX_DELAY);
        esp_err_t err = pcnt_unit_clear_count(_unit);
        _last_count   = 0;
        _last_time_us = esp_timer_get_time();
        _last_hz      = 0.0f;
        xSemaphoreGive(_lock);
        return err;
    }

    // Frequency from the pulses counted since the previous call
    esp_err_t frequency(float *hz) {
        if (_unit == nullptr || hz == nullptr) {
            return ESP_ERR_INVALID_STATE;
        }
        xSemaphoreTake(_lock, portMAX_DELAY);
        int value = 0;
        esp_err_t err = pcnt_unit_get_count(_unit, &value);
        if (err == ESP_OK) {
            const int64_t now_us = esp_timer_get_time();
            const int64_t dt_us  = now_us - _last_time_us;
            if (dt_us >= 1000) {             // at least 1 ms between readings
                _last_hz      = (float)(value - _last_count) * 1.0e6f / (float)dt_us;
                _last_count   = value;
                _last_time_us = now_us;
            }
            *hz = _last_hz;
        }
        xSemaphoreGive(_lock);
        return err;
    }

    esp_err_t setK(float k) {
        if (!(k > 0.0f)) {
            return ESP_ERR_INVALID_ARG;
        }
        _k_ppl = k;
        return ESP_OK;
    }

    esp_err_t flow(float *lps) {
        if (lps == nullptr) return ESP_ERR_INVALID_ARG;
        if (_k_ppl <= 0.0f) return ESP_ERR_INVALID_STATE;
        float hz = 0.0f;
        esp_err_t err = frequency(&hz);
        if (err == ESP_OK) {
            *lps = hz / _k_ppl;              // [pulses/s] / [pulses/L] = L/s
        }
        return err;
    }

    esp_err_t volume(float *liters) {
        if (liters == nullptr) return ESP_ERR_INVALID_ARG;
        if (_k_ppl <= 0.0f) return ESP_ERR_INVALID_STATE;
        int64_t c = 0;
        esp_err_t err = count(&c);
        if (err == ESP_OK) {
            *liters = (float)c / _k_ppl;
        }
        return err;
    }
};

// Handle <-> object conversion
static inline PCNT_AIoT *toObj(PCNT_AIoT_handle_t handle) {
    return reinterpret_cast<PCNT_AIoT *>(handle);
}

// -----------------------------------------------------------------------------
// 2. C BRIDGE (extern "C")
// -----------------------------------------------------------------------------
extern "C" {

PCNT_AIoT_handle_t PCNT_AIoT_create(int gpio, uint32_t glitch_ns) {
    if (gpio < 0 || glitch_ns > PCNT_AIOT_GLITCH_MAX_NS) {
        ESP_LOGE(TAG, "Invalid arguments (GPIO %d, glitch %u ns)", gpio, (unsigned)glitch_ns);
        return nullptr;
    }
    PCNT_AIoT *obj = new (std::nothrow) PCNT_AIoT(gpio, glitch_ns);
    if (obj == nullptr || !obj->isValid()) {
        ESP_LOGE(TAG, "No memory for PCNT object");
        delete obj;
        return nullptr;
    }
    return reinterpret_cast<PCNT_AIoT_handle_t>(obj);
}

esp_err_t PCNT_AIoT_init(PCNT_AIoT_handle_t handle) {
    PCNT_AIoT *obj = toObj(handle);
    return obj ? obj->begin() : ESP_ERR_INVALID_ARG;
}

void PCNT_AIoT_destroy(PCNT_AIoT_handle_t handle) {
    delete toObj(handle);   // delete nullptr is safe
}

esp_err_t PCNT_AIoT_get_count(PCNT_AIoT_handle_t handle, int64_t *count) {
    PCNT_AIoT *obj = toObj(handle);
    return obj ? obj->count(count) : ESP_ERR_INVALID_ARG;
}

esp_err_t PCNT_AIoT_clear(PCNT_AIoT_handle_t handle) {
    PCNT_AIoT *obj = toObj(handle);
    return obj ? obj->clear() : ESP_ERR_INVALID_ARG;
}

esp_err_t PCNT_AIoT_get_frequency(PCNT_AIoT_handle_t handle, float *hz) {
    PCNT_AIoT *obj = toObj(handle);
    return obj ? obj->frequency(hz) : ESP_ERR_INVALID_ARG;
}

esp_err_t PCNT_AIoT_set_k_factor(PCNT_AIoT_handle_t handle, float pulses_per_liter) {
    PCNT_AIoT *obj = toObj(handle);
    return obj ? obj->setK(pulses_per_liter) : ESP_ERR_INVALID_ARG;
}

esp_err_t PCNT_AIoT_get_flow(PCNT_AIoT_handle_t handle, float *liters_per_s) {
    PCNT_AIoT *obj = toObj(handle);
    return obj ? obj->flow(liters_per_s) : ESP_ERR_INVALID_ARG;
}

esp_err_t PCNT_AIoT_get_volume(PCNT_AIoT_handle_t handle, float *liters) {
    PCNT_AIoT *obj = toObj(handle);
    return obj ? obj->volume(liters) : ESP_ERR_INVALID_ARG;
}

} // extern "C"
