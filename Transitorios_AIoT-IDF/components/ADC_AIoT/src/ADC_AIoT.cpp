/*
 * File: components/ADC_AIoT/src/ADC_AIoT.cpp
 * Description: High-rate ADC library for ESP-IDF (continuous mode + DMA).
 *              1. C++ class ADC_AIoT: the real logic.
 *              2. C bridge (extern "C"): the public API of ADC_AIoT.h.
 * Author: Ing. Ernesto José Guerrero González
 * Standards: ESP-IDF v5.5.1, C++. English comments.
 */
#include "ADC_AIoT.h"

#include <cstring>
#include <new>
#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "ADC_AIoT";

/* DMA sizing: one frame = 256 results of 4 bytes; the pool holds 8 frames */
static constexpr uint32_t kResultBytes = SOC_ADC_DIGI_RESULT_BYTES;   // 4 on the S3
static constexpr uint32_t kFrameBytes  = 256 * kResultBytes;
static constexpr uint32_t kPoolBytes   = 8 * kFrameBytes;

/* Approximate full scale (mV) used only if calibration is not available */
static const int32_t kFullScaleMv[4] = { 950, 1250, 1750, 3100 };

// -----------------------------------------------------------------------------
// 1. C++ CLASS (real logic)
// -----------------------------------------------------------------------------
class ADC_AIoT {
private:
    struct Channel {
        uint8_t           gpio;
        adc_channel_t     channel;
        adc_atten_t       atten;
        adc_cali_handle_t cali;              // nullptr if calibration failed
    };

    uint32_t                _rate_hz;
    Channel                 _ch[ADC_AIOT_MAX_CHANNELS];
    int8_t                  _index_of[ADC_AIOT_MAX_CHANNELS];   // ADC channel -> index
    uint8_t                 _count;
    adc_continuous_handle_t _adc;            // nullptr until begin()
    uint8_t                *_raw;            // DMA read buffer (one frame)
    bool                    _running;
    volatile uint32_t       _overruns;
    SemaphoreHandle_t       _lock;

    // DMA pool overflow (ISR context): count it, no yield needed
    static bool onPoolOverflow(adc_continuous_handle_t, const adc_continuous_evt_data_t *, void *user) {
        ADC_AIoT *self = static_cast<ADC_AIoT *>(user);
        self->_overruns = self->_overruns + 1;
        return false;
    }

    int32_t toMillivolts(const Channel &c, int raw) const {
        int mv = 0;
        if (c.cali != nullptr && adc_cali_raw_to_voltage(c.cali, raw, &mv) == ESP_OK) {
            return mv;
        }
        return (int32_t)raw * kFullScaleMv[(int)c.atten] / 4095;   // rough fallback
    }

    void releaseDriver() {
        if (_adc != nullptr) {
            if (_running) {
                adc_continuous_stop(_adc);
                _running = false;
            }
            adc_continuous_deinit(_adc);
            _adc = nullptr;
        }
        for (uint8_t i = 0; i < _count; i++) {
            if (_ch[i].cali != nullptr) {
                adc_cali_delete_scheme_curve_fitting(_ch[i].cali);
                _ch[i].cali = nullptr;
            }
        }
    }

public:
    // Constructor: stores the configuration only
    explicit ADC_AIoT(uint32_t rate_hz)
        : _rate_hz(rate_hz), _count(0), _adc(nullptr), _raw(nullptr),
          _running(false), _overruns(0), _lock(xSemaphoreCreateMutex()) {
        memset(_ch, 0, sizeof(_ch));
        memset(_index_of, -1, sizeof(_index_of));
    }

    // Destructor: releases only what this object created
    ~ADC_AIoT() {
        releaseDriver();
        if (_raw != nullptr) heap_caps_free(_raw);
        if (_lock != nullptr) vSemaphoreDelete(_lock);
    }

    bool isValid() const { return _lock != nullptr; }

    esp_err_t addChannel(int gpio, ADC_AIoT_atten_t atten) {
        if (_adc != nullptr) {
            ESP_LOGE(TAG, "Add channels before ADC_AIoT_init()");
            return ESP_ERR_INVALID_STATE;
        }
        if (_count >= ADC_AIOT_MAX_CHANNELS) {
            return ESP_ERR_NO_MEM;
        }
        if ((int)atten < 0 || (int)atten > 3) {
            return ESP_ERR_INVALID_ARG;
        }
        adc_unit_t unit;
        adc_channel_t channel;
        if (adc_continuous_io_to_channel(gpio, &unit, &channel) != ESP_OK || unit != ADC_UNIT_1) {
            ESP_LOGE(TAG, "GPIO %d is not an ADC1 pin (use GPIO 1..10)", gpio);
            return ESP_ERR_INVALID_ARG;
        }
        if (_index_of[channel] >= 0) {
            ESP_LOGE(TAG, "GPIO %d already added", gpio);
            return ESP_ERR_INVALID_ARG;
        }
        _ch[_count].gpio    = (uint8_t)gpio;
        _ch[_count].channel = channel;
        _ch[_count].atten   = (adc_atten_t)atten;
        _ch[_count].cali    = nullptr;
        _index_of[channel]  = (int8_t)_count;
        _count++;
        return ESP_OK;
    }

    // Hardware initialization: DMA driver + calibration per channel
    esp_err_t begin() {
        if (_adc != nullptr) {
            return ESP_OK;
        }
        if (_count == 0) {
            ESP_LOGE(TAG, "No channels: call ADC_AIoT_add_channel() first");
            return ESP_ERR_INVALID_STATE;
        }
        if (_raw == nullptr) {
            _raw = static_cast<uint8_t *>(heap_caps_malloc(kFrameBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
            if (_raw == nullptr) {
                return ESP_ERR_NO_MEM;
            }
        }

        adc_continuous_handle_cfg_t hcfg = {};
        hcfg.max_store_buf_size = kPoolBytes;
        hcfg.conv_frame_size    = kFrameBytes;
        esp_err_t err = adc_continuous_new_handle(&hcfg, &_adc);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ADC driver error: %s", esp_err_to_name(err));
            _adc = nullptr;
            return err;
        }

        adc_digi_pattern_config_t pattern[ADC_AIOT_MAX_CHANNELS] = {};
        for (uint8_t i = 0; i < _count; i++) {
            pattern[i].atten     = (uint8_t)_ch[i].atten;
            pattern[i].channel   = (uint8_t)_ch[i].channel;
            pattern[i].unit      = (uint8_t)ADC_UNIT_1;
            pattern[i].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;
        }

        adc_continuous_config_t cfg = {};
        cfg.pattern_num    = _count;
        cfg.adc_pattern    = pattern;
        cfg.sample_freq_hz = _rate_hz;
        cfg.conv_mode      = ADC_CONV_SINGLE_UNIT_1;
        cfg.format         = ADC_DIGI_OUTPUT_FORMAT_TYPE2;
        err = adc_continuous_config(_adc, &cfg);

        if (err == ESP_OK) {
            adc_continuous_evt_cbs_t cbs = {};
            cbs.on_pool_ovf = &ADC_AIoT::onPoolOverflow;
            err = adc_continuous_register_event_callbacks(_adc, &cbs, this);
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ADC configuration error: %s", esp_err_to_name(err));
            releaseDriver();
            return err;
        }

        // Calibration (curve fitting on the ESP32-S3); failure is not fatal
        for (uint8_t i = 0; i < _count; i++) {
            adc_cali_curve_fitting_config_t ccfg = {};
            ccfg.unit_id  = ADC_UNIT_1;
            ccfg.chan     = _ch[i].channel;
            ccfg.atten    = _ch[i].atten;
            ccfg.bitwidth = ADC_BITWIDTH_DEFAULT;
            if (adc_cali_create_scheme_curve_fitting(&ccfg, &_ch[i].cali) != ESP_OK) {
                _ch[i].cali = nullptr;
                ESP_LOGW(TAG, "GPIO %u: no calibration, approximate mV", (unsigned)_ch[i].gpio);
            }
        }

        ESP_LOGI(TAG, "ADC1 ready -> %u channel(s), %u Hz total, %u Hz per channel",
                 (unsigned)_count, (unsigned)_rate_hz, (unsigned)(_rate_hz / _count));
        return ESP_OK;
    }

    esp_err_t start() {
        if (_adc == nullptr) return ESP_ERR_INVALID_STATE;
        if (_running) return ESP_OK;
        esp_err_t err = adc_continuous_start(_adc);
        _running = (err == ESP_OK);
        return err;
    }

    esp_err_t stop() {
        if (_adc == nullptr) return ESP_ERR_INVALID_STATE;
        if (!_running) return ESP_OK;
        esp_err_t err = adc_continuous_stop(_adc);
        if (err == ESP_OK) _running = false;
        return err;
    }

    // Reads the DMA pool and converts every result into a sample
    int read(ADC_AIoT_sample_t *samples, size_t max_samples, uint32_t timeout_ms) {
        if (_adc == nullptr || !_running || samples == nullptr || max_samples == 0) {
            return -1;
        }
        uint32_t want = (uint32_t)max_samples * kResultBytes;
        if (want > kFrameBytes) want = kFrameBytes;

        xSemaphoreTake(_lock, portMAX_DELAY);
        uint32_t got = 0;
        esp_err_t err = adc_continuous_read(_adc, _raw, want, &got, timeout_ms);
        int n = 0;
        if (err == ESP_OK) {
            for (uint32_t i = 0; i + kResultBytes <= got && (size_t)n < max_samples; i += kResultBytes) {
                const adc_digi_output_data_t *p = reinterpret_cast<const adc_digi_output_data_t *>(&_raw[i]);
                const uint32_t chan = p->type2.channel;
                if (p->type2.unit != 0 || chan >= ADC_AIOT_MAX_CHANNELS || _index_of[chan] < 0) {
                    continue;                // invalid or foreign result
                }
                const Channel &c = _ch[_index_of[chan]];
                samples[n].index = (uint8_t)_index_of[chan];
                samples[n].gpio  = c.gpio;
                samples[n].raw   = (uint16_t)p->type2.data;
                samples[n].mv    = toMillivolts(c, (int)p->type2.data);
                n++;
            }
        } else if (err != ESP_ERR_TIMEOUT) {
            n = -1;
        }
        xSemaphoreGive(_lock);
        return n;
    }

    uint32_t ratePerChannel() const { return (_count > 0) ? (_rate_hz / _count) : 0; }
    uint32_t overruns() const { return _overruns; }
};

// Handle <-> object conversion
static inline ADC_AIoT *toObj(ADC_AIoT_handle_t handle) {
    return reinterpret_cast<ADC_AIoT *>(handle);
}

// -----------------------------------------------------------------------------
// 2. C BRIDGE (extern "C")
// -----------------------------------------------------------------------------
extern "C" {

ADC_AIoT_handle_t ADC_AIoT_create(uint32_t sample_rate_hz) {
    if (sample_rate_hz < ADC_AIOT_RATE_MIN_HZ || sample_rate_hz > ADC_AIOT_RATE_MAX_HZ) {
        ESP_LOGE(TAG, "Sample rate %u Hz out of range (%d..%d)",
                 (unsigned)sample_rate_hz, ADC_AIOT_RATE_MIN_HZ, ADC_AIOT_RATE_MAX_HZ);
        return nullptr;
    }
    ADC_AIoT *obj = new (std::nothrow) ADC_AIoT(sample_rate_hz);
    if (obj == nullptr || !obj->isValid()) {
        ESP_LOGE(TAG, "No memory for ADC object");
        delete obj;
        return nullptr;
    }
    return reinterpret_cast<ADC_AIoT_handle_t>(obj);
}

esp_err_t ADC_AIoT_add_channel(ADC_AIoT_handle_t handle, int gpio, ADC_AIoT_atten_t atten) {
    ADC_AIoT *obj = toObj(handle);
    return obj ? obj->addChannel(gpio, atten) : ESP_ERR_INVALID_ARG;
}

esp_err_t ADC_AIoT_init(ADC_AIoT_handle_t handle) {
    ADC_AIoT *obj = toObj(handle);
    return obj ? obj->begin() : ESP_ERR_INVALID_ARG;
}

void ADC_AIoT_destroy(ADC_AIoT_handle_t handle) {
    delete toObj(handle);   // delete nullptr is safe
}

esp_err_t ADC_AIoT_start(ADC_AIoT_handle_t handle) {
    ADC_AIoT *obj = toObj(handle);
    return obj ? obj->start() : ESP_ERR_INVALID_ARG;
}

esp_err_t ADC_AIoT_stop(ADC_AIoT_handle_t handle) {
    ADC_AIoT *obj = toObj(handle);
    return obj ? obj->stop() : ESP_ERR_INVALID_ARG;
}

int ADC_AIoT_read(ADC_AIoT_handle_t handle, ADC_AIoT_sample_t *samples, size_t max_samples, uint32_t timeout_ms) {
    ADC_AIoT *obj = toObj(handle);
    return obj ? obj->read(samples, max_samples, timeout_ms) : -1;
}

uint32_t ADC_AIoT_get_rate_per_channel(ADC_AIoT_handle_t handle) {
    ADC_AIoT *obj = toObj(handle);
    return obj ? obj->ratePerChannel() : 0;
}

uint32_t ADC_AIoT_get_overruns(ADC_AIoT_handle_t handle) {
    ADC_AIoT *obj = toObj(handle);
    return obj ? obj->overruns() : 0;
}

} // extern "C"
