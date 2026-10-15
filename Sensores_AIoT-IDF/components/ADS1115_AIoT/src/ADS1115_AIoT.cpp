/*
 * File: components/ADS1115_AIoT/src/ADS1115_AIoT.cpp
 * Description: ADS1115 16-bit external ADC driver (on top of I2C_AIoT).
 *              1. C++ class ADS1115_AIoT: the real logic.
 *              2. C bridge (extern "C"): the public API of ADS1115_AIoT.h.
 * Author: Ing. Ernesto José Guerrero González
 * Standards: ESP-IDF v5.5.1, C++. English comments.
 */
#include "ADS1115_AIoT.h"

#include <new>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "esp_log.h"

static const char *TAG = "ADS1115_AIoT";

/* Registers */
static constexpr uint8_t kRegConversion = 0x00;
static constexpr uint8_t kRegConfig     = 0x01;

/* Config register fields */
static constexpr uint16_t kCfgOsStart     = 0x8000;   // write: start single shot / read: 1 = idle
static constexpr uint16_t kCfgModeSingle  = 0x0100;   // 1 = single-shot / power-down
static constexpr uint16_t kCfgCompDisable = 0x0003;   // comparator off, ALERT pin high-Z

static constexpr uint32_t kI2cTimeoutMs   = 10;       // short: never block the sampling loop

/* Full scale in mV for each PGA code, and samples per second for each rate code */
static const float    kFullScaleMv[6] = { 6144.0f, 4096.0f, 2048.0f, 1024.0f, 512.0f, 256.0f };
static const uint16_t kRateSps[8]     = { 8, 16, 32, 64, 128, 250, 475, 860 };

// -----------------------------------------------------------------------------
// 1. C++ CLASS (real logic)
// -----------------------------------------------------------------------------
class ADS1115_AIoT {
private:
    I2C_AIoT_bus_t     _bus;
    uint8_t            _addr7;
    uint32_t           _speed_hz;
    I2C_AIoT_handle_t  _dev;                 // nullptr until begin()
    ADS1115_AIoT_pga_t _pga;
    ADS1115_AIoT_dr_t  _dr;
    bool               _continuous;
    SemaphoreHandle_t  _lock;

    // Config word: MUX = 1xx (AINx vs GND), PGA, MODE, DR, comparator off
    uint16_t configWord(ADS1115_AIoT_input_t input, bool single) const {
        uint16_t w = (uint16_t)((0x4U | (uint16_t)input) << 12);
        w |= (uint16_t)((uint16_t)_pga << 9);
        if (single) w |= kCfgModeSingle;
        w |= (uint16_t)((uint16_t)_dr << 5);
        w |= kCfgCompDisable;
        return w;
    }

    esp_err_t writeConfig(uint16_t value) {
        const uint8_t buf[3] = { kRegConfig, (uint8_t)(value >> 8), (uint8_t)(value & 0xFF) };
        return I2C_AIoT_write(_dev, buf, sizeof(buf));
    }

    esp_err_t readReg16(uint8_t reg, uint16_t *value) {
        uint8_t buf[2];
        esp_err_t err = I2C_AIoT_read_regs(_dev, reg, buf, sizeof(buf));
        if (err == ESP_OK) {
            *value = (uint16_t)(((uint16_t)buf[0] << 8) | buf[1]);
        }
        return err;
    }

    esp_err_t readConversionMv(float *mv) {
        uint16_t raw = 0;
        esp_err_t err = readReg16(kRegConversion, &raw);
        if (err == ESP_OK) {
            *mv = (float)(int16_t)raw * kFullScaleMv[_pga] / 32768.0f;
        }
        return err;
    }

    void releaseDevice() {
        if (_dev != nullptr) {
            I2C_AIoT_destroy(_dev);
            _dev = nullptr;
        }
    }

public:
    // Constructor: stores the configuration only
    ADS1115_AIoT(I2C_AIoT_bus_t bus, uint8_t addr7, uint32_t speed_hz)
        : _bus(bus), _addr7(addr7), _speed_hz(speed_hz), _dev(nullptr),
          _pga(ADS1115_AIOT_PGA_4096MV), _dr(ADS1115_AIOT_DR_860SPS),
          _continuous(false), _lock(xSemaphoreCreateMutex()) {}

    // Destructor: power down (single-shot mode, idle), release the device
    ~ADS1115_AIoT() {
        if (_dev != nullptr) {
            (void)writeConfig(configWord(ADS1115_AIOT_AIN0, true));
        }
        releaseDevice();
        if (_lock != nullptr) vSemaphoreDelete(_lock);
    }

    bool isValid() const { return _lock != nullptr; }

    esp_err_t setPga(ADS1115_AIoT_pga_t pga) {
        if ((int)pga < 0 || (int)pga > 5) return ESP_ERR_INVALID_ARG;
        xSemaphoreTake(_lock, portMAX_DELAY);
        _pga = pga;
        xSemaphoreGive(_lock);
        return ESP_OK;
    }

    esp_err_t setDataRate(ADS1115_AIoT_dr_t dr) {
        if ((int)dr < 0 || (int)dr > 7) return ESP_ERR_INVALID_ARG;
        xSemaphoreTake(_lock, portMAX_DELAY);
        _dr = dr;
        xSemaphoreGive(_lock);
        return ESP_OK;
    }

    // Hardware initialization: detect and leave in power-down
    esp_err_t begin() {
        xSemaphoreTake(_lock, portMAX_DELAY);
        if (_dev != nullptr) {
            xSemaphoreGive(_lock);
            return ESP_OK;
        }
        esp_err_t err = I2C_AIoT_probe(_bus, _addr7);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "No device at 0x%02X (%s)", _addr7, esp_err_to_name(err));
            xSemaphoreGive(_lock);
            return ESP_ERR_NOT_FOUND;
        }
        _dev = I2C_AIoT_create(_bus, _addr7, _speed_hz);
        if (_dev == nullptr) {
            xSemaphoreGive(_lock);
            return ESP_ERR_NO_MEM;
        }
        err = I2C_AIoT_init(_dev);
        if (err == ESP_OK) {
            I2C_AIoT_set_timeout(_dev, kI2cTimeoutMs);
            err = writeConfig(configWord(ADS1115_AIOT_AIN0, true));
        }
        // Read back: the configuration bits (OS excluded) must match
        uint16_t check = 0;
        if (err == ESP_OK) err = readReg16(kRegConfig, &check);
        if (err == ESP_OK && (check & 0x7FFF) != (configWord(ADS1115_AIOT_AIN0, true) & 0x7FFF)) {
            ESP_LOGE(TAG, "Device at 0x%02X is not an ADS1115 (config 0x%04X)", _addr7, check);
            err = ESP_ERR_INVALID_RESPONSE;
        }
        if (err != ESP_OK) {
            releaseDevice();
            xSemaphoreGive(_lock);
            return err;
        }
        _continuous = false;
        ESP_LOGI(TAG, "Ready at 0x%02X -> +/-%.3f V, %u SPS",
                 _addr7, (double)(kFullScaleMv[_pga] / 1000.0f), (unsigned)kRateSps[_dr]);
        xSemaphoreGive(_lock);
        return ESP_OK;
    }

    esp_err_t startContinuous(ADS1115_AIoT_input_t input) {
        if ((int)input < 0 || (int)input > 3) return ESP_ERR_INVALID_ARG;
        xSemaphoreTake(_lock, portMAX_DELAY);
        esp_err_t err = (_dev == nullptr) ? ESP_ERR_INVALID_STATE : writeConfig(configWord(input, false));
        _continuous = (err == ESP_OK);
        xSemaphoreGive(_lock);
        return err;
    }

    esp_err_t readMv(float *mv) {
        if (mv == nullptr) return ESP_ERR_INVALID_ARG;
        xSemaphoreTake(_lock, portMAX_DELAY);
        esp_err_t err = (_dev == nullptr || !_continuous) ? ESP_ERR_INVALID_STATE : readConversionMv(mv);
        xSemaphoreGive(_lock);
        return err;
    }

    // Single shot: start, poll the OS bit until idle, read the result
    esp_err_t readSingleMv(ADS1115_AIoT_input_t input, float *mv) {
        if (mv == nullptr || (int)input < 0 || (int)input > 3) return ESP_ERR_INVALID_ARG;
        xSemaphoreTake(_lock, portMAX_DELAY);
        if (_dev == nullptr) {
            xSemaphoreGive(_lock);
            return ESP_ERR_INVALID_STATE;
        }
        _continuous = false;
        esp_err_t err = writeConfig((uint16_t)(configWord(input, true) | kCfgOsStart));
        // Conversion time 1/DR (+10 % tolerance), wait at most twice that + 2 ms
        const int64_t deadline = esp_timer_get_time() + 2LL * (1000000LL / kRateSps[_dr]) + 2000LL;
        while (err == ESP_OK) {
            uint16_t cfg = 0;
            err = readReg16(kRegConfig, &cfg);
            if (err != ESP_OK || (cfg & kCfgOsStart) != 0) {
                break;                       // error, or conversion finished
            }
            if (esp_timer_get_time() > deadline) {
                err = ESP_ERR_TIMEOUT;
                break;
            }
            esp_rom_delay_us(200);
        }
        if (err == ESP_OK) err = readConversionMv(mv);
        xSemaphoreGive(_lock);
        return err;
    }
};

// Handle <-> object conversion
static inline ADS1115_AIoT *toObj(ADS1115_AIoT_handle_t handle) {
    return reinterpret_cast<ADS1115_AIoT *>(handle);
}

// -----------------------------------------------------------------------------
// 2. C BRIDGE (extern "C")
// -----------------------------------------------------------------------------
extern "C" {

ADS1115_AIoT_handle_t ADS1115_AIoT_create(I2C_AIoT_bus_t bus, uint8_t addr7, uint32_t speed_hz) {
    if (bus == nullptr || addr7 < ADS1115_AIOT_ADDR_GND || addr7 > ADS1115_AIOT_ADDR_SCL ||
        speed_hz == 0 || speed_hz > I2C_AIOT_SPEED_FAST) {
        ESP_LOGE(TAG, "Invalid arguments (addr 0x%02X, %u Hz)", addr7, (unsigned)speed_hz);
        return nullptr;
    }
    ADS1115_AIoT *obj = new (std::nothrow) ADS1115_AIoT(bus, addr7, speed_hz);
    if (obj == nullptr || !obj->isValid()) {
        ESP_LOGE(TAG, "No memory for ADS1115 object");
        delete obj;
        return nullptr;
    }
    return reinterpret_cast<ADS1115_AIoT_handle_t>(obj);
}

esp_err_t ADS1115_AIoT_set_pga(ADS1115_AIoT_handle_t handle, ADS1115_AIoT_pga_t pga) {
    ADS1115_AIoT *obj = toObj(handle);
    return obj ? obj->setPga(pga) : ESP_ERR_INVALID_ARG;
}

esp_err_t ADS1115_AIoT_set_data_rate(ADS1115_AIoT_handle_t handle, ADS1115_AIoT_dr_t dr) {
    ADS1115_AIoT *obj = toObj(handle);
    return obj ? obj->setDataRate(dr) : ESP_ERR_INVALID_ARG;
}

esp_err_t ADS1115_AIoT_init(ADS1115_AIoT_handle_t handle) {
    ADS1115_AIoT *obj = toObj(handle);
    return obj ? obj->begin() : ESP_ERR_INVALID_ARG;
}

void ADS1115_AIoT_destroy(ADS1115_AIoT_handle_t handle) {
    delete toObj(handle);   // delete nullptr is safe
}

esp_err_t ADS1115_AIoT_start_continuous(ADS1115_AIoT_handle_t handle, ADS1115_AIoT_input_t input) {
    ADS1115_AIoT *obj = toObj(handle);
    return obj ? obj->startContinuous(input) : ESP_ERR_INVALID_ARG;
}

esp_err_t ADS1115_AIoT_read_mv(ADS1115_AIoT_handle_t handle, float *mv) {
    ADS1115_AIoT *obj = toObj(handle);
    return obj ? obj->readMv(mv) : ESP_ERR_INVALID_ARG;
}

esp_err_t ADS1115_AIoT_read_single_mv(ADS1115_AIoT_handle_t handle, ADS1115_AIoT_input_t input, float *mv) {
    ADS1115_AIoT *obj = toObj(handle);
    return obj ? obj->readSingleMv(input, mv) : ESP_ERR_INVALID_ARG;
}

} // extern "C"
