/*
 * File: components/MPU6050_AIoT/src/MPU6050_AIoT.cpp
 * Description: MPU-6050 6-axis IMU driver for ESP-IDF (on top of I2C_AIoT).
 *              1. C++ class MPU6050_AIoT: the real logic.
 *              2. C bridge (extern "C"): the public API of MPU6050_AIoT.h.
 * Author: Ing. Ernesto José Guerrero González
 * Standards: ESP-IDF v5.5.1, C++. English comments.
 */
#include "MPU6050_AIoT.h"

#include <new>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "MPU6050_AIoT";

/* Register map (only the registers used here) */
static constexpr uint8_t kRegSmplrtDiv   = 0x19;
static constexpr uint8_t kRegConfig      = 0x1A;
static constexpr uint8_t kRegGyroConfig  = 0x1B;
static constexpr uint8_t kRegAccelConfig = 0x1C;
static constexpr uint8_t kRegAccelXoutH  = 0x3B;   // 14 bytes: accel, temp, gyro
static constexpr uint8_t kRegSignalReset = 0x68;
static constexpr uint8_t kRegPwrMgmt1    = 0x6B;
static constexpr uint8_t kRegWhoAmI      = 0x75;

static constexpr uint8_t kPwrReset       = 0x80;   // device reset
static constexpr uint8_t kPwrClockPllX   = 0x01;   // awake, PLL with gyro X reference
static constexpr uint8_t kPwrSleep       = 0x40;

static constexpr uint32_t kI2cTimeoutMs  = 10;     // short: never block the sampling loop

static constexpr float kGravity   = 9.80665f;               // m/s2 per g
static constexpr float kDegToRad  = 0.017453292519943295f;  // pi / 180

/* Sensitivity (LSB per unit) for each full-scale code */
static const float kAccelLsbPerG[4]   = { 16384.0f, 8192.0f, 4096.0f, 2048.0f };
static const float kGyroLsbPerDps[4]  = { 131.0f, 65.5f, 32.8f, 16.4f };

// Waits at least one RTOS tick (pdMS_TO_TICKS(1) is 0 when the tick is 100 Hz)
static inline void delayMs(uint32_t ms) {
    TickType_t t = pdMS_TO_TICKS(ms);
    vTaskDelay((t == 0) ? 1 : t);
}

// Big-endian register pair to signed 16-bit value
static inline int16_t be16(const uint8_t *p) {
    return (int16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

// -----------------------------------------------------------------------------
// 1. C++ CLASS (real logic)
// -----------------------------------------------------------------------------
class MPU6050_AIoT {
private:
    I2C_AIoT_bus_t          _bus;
    uint8_t                 _addr7;
    uint32_t                _speed_hz;
    I2C_AIoT_handle_t       _dev;            // nullptr until begin()
    MPU6050_AIoT_accel_fs_t _accel_fs;
    MPU6050_AIoT_gyro_fs_t  _gyro_fs;
    MPU6050_AIoT_dlpf_t     _dlpf;
    uint8_t                 _who_am_i;
    bool                    _mpu6500_family; // different temperature formula
    float                   _gyro_bias[3];   // raw counts
    SemaphoreHandle_t       _lock;

    // Writes the range / filter registers (device must exist)
    esp_err_t applyConfig() {
        // DLPF 260 Hz runs the gyro at 8 kHz: divide by 8 to keep 1 kHz
        const uint8_t div = (_dlpf == MPU6050_AIOT_DLPF_260HZ) ? 7 : 0;
        esp_err_t err = I2C_AIoT_write_reg(_dev, kRegSmplrtDiv, div);
        if (err == ESP_OK) err = I2C_AIoT_write_reg(_dev, kRegConfig, (uint8_t)_dlpf);
        if (err == ESP_OK) err = I2C_AIoT_write_reg(_dev, kRegGyroConfig, (uint8_t)(_gyro_fs << 3));
        if (err == ESP_OK) err = I2C_AIoT_write_reg(_dev, kRegAccelConfig, (uint8_t)(_accel_fs << 3));
        return err;
    }

    // Burst read of the 7 raw values (ax, ay, az, t, gx, gy, gz)
    esp_err_t readRaw(int16_t raw[7]) {
        uint8_t buf[14];
        esp_err_t err = I2C_AIoT_read_regs(_dev, kRegAccelXoutH, buf, sizeof(buf));
        if (err != ESP_OK) {
            return err;
        }
        for (int i = 0; i < 7; i++) {
            raw[i] = be16(&buf[2 * i]);
        }
        return ESP_OK;
    }

    void releaseDevice() {
        if (_dev != nullptr) {
            I2C_AIoT_destroy(_dev);
            _dev = nullptr;
        }
    }

public:
    // Constructor: stores the configuration only
    MPU6050_AIoT(I2C_AIoT_bus_t bus, uint8_t addr7, uint32_t speed_hz)
        : _bus(bus), _addr7(addr7), _speed_hz(speed_hz), _dev(nullptr),
          _accel_fs(MPU6050_AIOT_ACCEL_2G), _gyro_fs(MPU6050_AIOT_GYRO_250DPS),
          _dlpf(MPU6050_AIOT_DLPF_184HZ), _who_am_i(0), _mpu6500_family(false),
          _gyro_bias{ 0.0f, 0.0f, 0.0f }, _lock(xSemaphoreCreateMutex()) {}

    // Destructor: sleep mode, then release the I2C device
    ~MPU6050_AIoT() {
        if (_dev != nullptr) {
            (void)I2C_AIoT_write_reg(_dev, kRegPwrMgmt1, kPwrSleep);
        }
        releaseDevice();
        if (_lock != nullptr) vSemaphoreDelete(_lock);
    }

    bool isValid() const { return _lock != nullptr; }

    esp_err_t setRanges(MPU6050_AIoT_accel_fs_t accel_fs, MPU6050_AIoT_gyro_fs_t gyro_fs) {
        if ((int)accel_fs < 0 || (int)accel_fs > 3 || (int)gyro_fs < 0 || (int)gyro_fs > 3) {
            return ESP_ERR_INVALID_ARG;
        }
        xSemaphoreTake(_lock, portMAX_DELAY);
        _accel_fs = accel_fs;
        _gyro_fs  = gyro_fs;
        for (int i = 0; i < 3; i++) {
            _gyro_bias[i] = 0.0f;            // bias in counts depends on the range
        }
        esp_err_t err = (_dev != nullptr) ? applyConfig() : ESP_OK;
        xSemaphoreGive(_lock);
        return err;
    }

    esp_err_t setDlpf(MPU6050_AIoT_dlpf_t dlpf) {
        if ((int)dlpf < 0 || (int)dlpf > 6) {
            return ESP_ERR_INVALID_ARG;
        }
        xSemaphoreTake(_lock, portMAX_DELAY);
        _dlpf = dlpf;
        esp_err_t err = (_dev != nullptr) ? applyConfig() : ESP_OK;
        xSemaphoreGive(_lock);
        return err;
    }

    // Hardware initialization: detect, reset, wake up, configure
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
            err = I2C_AIoT_read_reg(_dev, kRegWhoAmI, &_who_am_i);
        }
        if (err == ESP_OK && (_who_am_i == 0x00 || _who_am_i == 0xFF)) {
            ESP_LOGE(TAG, "Invalid WHO_AM_I 0x%02X (check wiring / pull-ups)", _who_am_i);
            err = ESP_ERR_INVALID_RESPONSE;
        }
        if (err != ESP_OK) {
            releaseDevice();
            xSemaphoreGive(_lock);
            return err;
        }

        if (_who_am_i == 0x68) {
            ESP_LOGI(TAG, "MPU-6050 detected at 0x%02X", _addr7);
        } else if (_who_am_i == 0x70 || _who_am_i == 0x71 || _who_am_i == 0x73) {
            _mpu6500_family = true;
            ESP_LOGW(TAG, "MPU-6500/9250 family (WHO_AM_I 0x%02X): register compatible", _who_am_i);
        } else {
            ESP_LOGW(TAG, "Unknown WHO_AM_I 0x%02X (clone?): handled as MPU-6050", _who_am_i);
        }

        // Reset, wake up with the gyro PLL clock, reset the signal paths
        err = I2C_AIoT_write_reg(_dev, kRegPwrMgmt1, kPwrReset);
        if (err == ESP_OK) {
            delayMs(100);
            err = I2C_AIoT_write_reg(_dev, kRegPwrMgmt1, kPwrClockPllX);
        }
        if (err == ESP_OK) {
            delayMs(10);
            err = I2C_AIoT_write_reg(_dev, kRegSignalReset, 0x07);
        }
        if (err == ESP_OK) {
            delayMs(100);
            err = applyConfig();
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Configuration failed: %s", esp_err_to_name(err));
            releaseDevice();
            xSemaphoreGive(_lock);
            return err;
        }

        static const int kAccelG[4] = { 2, 4, 8, 16 };
        static const int kGyroDps[4] = { 250, 500, 1000, 2000 };
        ESP_LOGI(TAG, "Ready -> +/-%d g, +/-%d dps, DLPF code %d, 1 kHz internal rate",
                 kAccelG[_accel_fs], kGyroDps[_gyro_fs], (int)_dlpf);
        xSemaphoreGive(_lock);
        return ESP_OK;
    }

    esp_err_t read(MPU6050_AIoT_data_t *out) {
        if (out == nullptr) {
            return ESP_ERR_INVALID_ARG;
        }
        xSemaphoreTake(_lock, portMAX_DELAY);
        if (_dev == nullptr) {
            xSemaphoreGive(_lock);
            return ESP_ERR_INVALID_STATE;
        }
        int16_t raw[7];
        esp_err_t err = readRaw(raw);
        if (err == ESP_OK) {
            const float a = kGravity / kAccelLsbPerG[_accel_fs];
            const float g = kDegToRad / kGyroLsbPerDps[_gyro_fs];
            out->ax = (float)raw[0] * a;
            out->ay = (float)raw[1] * a;
            out->az = (float)raw[2] * a;
            out->temp_c = _mpu6500_family ? ((float)raw[3] / 333.87f + 21.0f)
                                          : ((float)raw[3] / 340.0f + 36.53f);
            out->gx = ((float)raw[4] - _gyro_bias[0]) * g;
            out->gy = ((float)raw[5] - _gyro_bias[1]) * g;
            out->gz = ((float)raw[6] - _gyro_bias[2]) * g;
        }
        xSemaphoreGive(_lock);
        return err;
    }

    // Averages the gyro output at rest to obtain its offset
    esp_err_t calibrateGyro(uint16_t samples) {
        if (samples == 0) {
            return ESP_ERR_INVALID_ARG;
        }
        xSemaphoreTake(_lock, portMAX_DELAY);
        if (_dev == nullptr) {
            xSemaphoreGive(_lock);
            return ESP_ERR_INVALID_STATE;
        }
        float sum[3] = { 0.0f, 0.0f, 0.0f };
        uint16_t ok = 0;
        for (uint16_t i = 0; i < samples; i++) {
            int16_t raw[7];
            if (readRaw(raw) == ESP_OK) {
                sum[0] += (float)raw[4];
                sum[1] += (float)raw[5];
                sum[2] += (float)raw[6];
                ok++;
            }
            delayMs(1);
        }
        esp_err_t err = ESP_FAIL;
        if (ok > samples / 2) {
            for (int i = 0; i < 3; i++) {
                _gyro_bias[i] = sum[i] / (float)ok;
            }
            ESP_LOGI(TAG, "Gyro bias (counts): %.1f %.1f %.1f (%u samples)",
                     (double)_gyro_bias[0], (double)_gyro_bias[1], (double)_gyro_bias[2], (unsigned)ok);
            err = ESP_OK;
        } else {
            ESP_LOGE(TAG, "Gyro calibration failed (%u of %u readings)", (unsigned)ok, (unsigned)samples);
        }
        xSemaphoreGive(_lock);
        return err;
    }

    uint8_t whoAmI() const { return _who_am_i; }
};

// Handle <-> object conversion
static inline MPU6050_AIoT *toObj(MPU6050_AIoT_handle_t handle) {
    return reinterpret_cast<MPU6050_AIoT *>(handle);
}

// -----------------------------------------------------------------------------
// 2. C BRIDGE (extern "C")
// -----------------------------------------------------------------------------
extern "C" {

MPU6050_AIoT_handle_t MPU6050_AIoT_create(I2C_AIoT_bus_t bus, uint8_t addr7, uint32_t speed_hz) {
    if (bus == nullptr || (addr7 != MPU6050_AIOT_ADDR_LOW && addr7 != MPU6050_AIOT_ADDR_HIGH) ||
        speed_hz == 0 || speed_hz > I2C_AIOT_SPEED_FAST) {
        ESP_LOGE(TAG, "Invalid arguments (addr 0x%02X, %u Hz)", addr7, (unsigned)speed_hz);
        return nullptr;
    }
    MPU6050_AIoT *obj = new (std::nothrow) MPU6050_AIoT(bus, addr7, speed_hz);
    if (obj == nullptr || !obj->isValid()) {
        ESP_LOGE(TAG, "No memory for IMU object");
        delete obj;
        return nullptr;
    }
    return reinterpret_cast<MPU6050_AIoT_handle_t>(obj);
}

esp_err_t MPU6050_AIoT_set_ranges(MPU6050_AIoT_handle_t handle,
                                  MPU6050_AIoT_accel_fs_t accel_fs,
                                  MPU6050_AIoT_gyro_fs_t gyro_fs) {
    MPU6050_AIoT *obj = toObj(handle);
    return obj ? obj->setRanges(accel_fs, gyro_fs) : ESP_ERR_INVALID_ARG;
}

esp_err_t MPU6050_AIoT_set_dlpf(MPU6050_AIoT_handle_t handle, MPU6050_AIoT_dlpf_t dlpf) {
    MPU6050_AIoT *obj = toObj(handle);
    return obj ? obj->setDlpf(dlpf) : ESP_ERR_INVALID_ARG;
}

esp_err_t MPU6050_AIoT_init(MPU6050_AIoT_handle_t handle) {
    MPU6050_AIoT *obj = toObj(handle);
    return obj ? obj->begin() : ESP_ERR_INVALID_ARG;
}

void MPU6050_AIoT_destroy(MPU6050_AIoT_handle_t handle) {
    delete toObj(handle);   // delete nullptr is safe
}

esp_err_t MPU6050_AIoT_read(MPU6050_AIoT_handle_t handle, MPU6050_AIoT_data_t *data) {
    MPU6050_AIoT *obj = toObj(handle);
    return obj ? obj->read(data) : ESP_ERR_INVALID_ARG;
}

esp_err_t MPU6050_AIoT_calibrate_gyro(MPU6050_AIoT_handle_t handle, uint16_t samples) {
    MPU6050_AIoT *obj = toObj(handle);
    return obj ? obj->calibrateGyro(samples) : ESP_ERR_INVALID_ARG;
}

uint8_t MPU6050_AIoT_get_who_am_i(MPU6050_AIoT_handle_t handle) {
    MPU6050_AIoT *obj = toObj(handle);
    return obj ? obj->whoAmI() : 0;
}

} // extern "C"
