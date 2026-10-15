/*
 * File: components/SPI_AIoT/src/SPI_AIoT.cpp
 * Description: SPI master library for ESP-IDF.
 *              1. C++ class SPI_AIoT_Device: the real logic.
 *              2. C bridge (extern "C"): the public API of SPI_AIoT.h.
 * Author: Ing. Ernesto José Guerrero González
 * Standards: ESP-IDF v5.5.1, C++. English comments.
 */
#include "SPI_AIoT.h"

#include <cstring>
#include <new>
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "SPI_AIoT";

// Only SPI2 and SPI3 belong to the application on the ESP32-S3 (SPI1 = flash)
static bool isValidHost(int host) {
    return (host == SPI2_HOST || host == SPI3_HOST);
}

// -----------------------------------------------------------------------------
// 1. C++ CLASS (real logic)
// -----------------------------------------------------------------------------

// SPI device: one per chip (own CS pin, clock and mode) on a shared bus
class SPI_AIoT_Device {
private:
    spi_host_device_t   _host;
    int                 _cs_pin;
    uint32_t            _clock_hz;
    uint8_t             _mode;
    spi_device_handle_t _dev;                // nullptr until begin()
    SemaphoreHandle_t   _lock;               // recursive: serializes this device
    bool                _bus_locked;

public:
    // Constructor: stores the configuration only
    SPI_AIoT_Device(int host, int cs_pin, uint32_t clock_hz, uint8_t mode)
        : _host((spi_host_device_t)host), _cs_pin(cs_pin), _clock_hz(clock_hz),
            _mode(mode), _dev(nullptr), _lock(xSemaphoreCreateRecursiveMutex()),
            _bus_locked(false) {}

    // Destructor: releases and detaches only what this object took
    ~SPI_AIoT_Device() {
        if (_dev != nullptr) {
            if (_bus_locked) {
                spi_device_release_bus(_dev);
            }
            spi_bus_remove_device(_dev);
        }
        if (_lock != nullptr) {
            vSemaphoreDelete(_lock);
        }
    }

    bool isValid() const { return _lock != nullptr; }

    // Attaches the device to its (already initialized) bus
    esp_err_t begin() {
        if (_dev != nullptr) {
            return ESP_OK;
        }

        spi_device_interface_config_t cfg = {};
        cfg.mode           = _mode;
        cfg.clock_speed_hz = (int)_clock_hz;
        cfg.spics_io_num   = _cs_pin;
        cfg.queue_size     = 1;

        esp_err_t err = spi_bus_add_device(_host, &cfg, &_dev);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Add device error (is the bus initialized?): %s", esp_err_to_name(err));
            _dev = nullptr;
            return err;
        }
        ESP_LOGI(TAG, "Device on SPI%d -> CS:%d @ %u Hz, mode %u",
                    (int)_host + 1, _cs_pin, (unsigned)_clock_hz, (unsigned)_mode);
        return ESP_OK;
    }

    // Full-duplex transfer (polling: lowest latency for short frames)
    esp_err_t transfer(const uint8_t *tx, uint8_t *rx, size_t len) {
        if (_dev == nullptr || len == 0) {
            return ESP_ERR_INVALID_ARG;
        }
        spi_transaction_t t = {};
        t.length    = len * 8;                       // bits clocked out
        t.rxlength  = (rx != nullptr) ? len * 8 : 0;
        t.tx_buffer = tx;                            // nullptr = send zeros
        t.rx_buffer = rx;

        xSemaphoreTakeRecursive(_lock, portMAX_DELAY);
        esp_err_t err = spi_device_polling_transmit(_dev, &t);
        xSemaphoreGiveRecursive(_lock);
        return err;
    }

    // Receive while sending a fill byte
    esp_err_t read(uint8_t *rx, size_t len, uint8_t fill) {
        if (rx == nullptr || len == 0) {
            return ESP_ERR_INVALID_ARG;
        }
        if (fill == 0x00) {
            return transfer(nullptr, rx, len);
        }
        uint8_t *tx = static_cast<uint8_t *>(heap_caps_malloc(len, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
        if (tx == nullptr) {
            return ESP_ERR_NO_MEM;
        }
        memset(tx, fill, len);
        esp_err_t err = transfer(tx, rx, len);
        heap_caps_free(tx);
        return err;
    }

    // Command + answer in ONE CS frame: [tx ... | zeros ...] -> keep the tail
    esp_err_t writeThenRead(const uint8_t *tx, size_t tx_len, uint8_t *rx, size_t rx_len) {
        if (tx == nullptr || tx_len == 0 || rx == nullptr || rx_len == 0) {
            return ESP_ERR_INVALID_ARG;
        }
        const size_t total = tx_len + rx_len;
        uint8_t *buf_tx = static_cast<uint8_t *>(heap_caps_calloc(1, total, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
        uint8_t *buf_rx = static_cast<uint8_t *>(heap_caps_malloc(total, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
        if (buf_tx == nullptr || buf_rx == nullptr) {
            heap_caps_free(buf_tx);
            heap_caps_free(buf_rx);
            return ESP_ERR_NO_MEM;
        }
        memcpy(buf_tx, tx, tx_len);
        esp_err_t err = transfer(buf_tx, buf_rx, total);
        if (err == ESP_OK) {
            memcpy(rx, buf_rx + tx_len, rx_len);
        }
        heap_caps_free(buf_tx);
        heap_caps_free(buf_rx);
        return err;
    }

    // Exclusive bus access for several transfers in a row
    esp_err_t lock(uint32_t timeout_ms) {
        if (_dev == nullptr) {
            return ESP_ERR_INVALID_STATE;
        }
        const TickType_t wait = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
        if (xSemaphoreTakeRecursive(_lock, wait) != pdTRUE) {
            return ESP_ERR_TIMEOUT;
        }
        esp_err_t err = spi_device_acquire_bus(_dev, portMAX_DELAY);
        if (err != ESP_OK) {
            xSemaphoreGiveRecursive(_lock);
            return err;
        }
        _bus_locked = true;
        return ESP_OK;
    }

    void unlock() {
        if (_dev == nullptr || !_bus_locked) {
            return;
        }
        _bus_locked = false;
        spi_device_release_bus(_dev);
        xSemaphoreGiveRecursive(_lock);
    }
};

// Handle <-> object conversion
static inline SPI_AIoT_Device *toObj(SPI_AIoT_handle_t handle) {
    return reinterpret_cast<SPI_AIoT_Device *>(handle);
}

// -----------------------------------------------------------------------------
// 2. C BRIDGE (extern "C")
// -----------------------------------------------------------------------------
extern "C" {

esp_err_t SPI_AIoT_bus_init(int host, int mosi_pin, int miso_pin, int sclk_pin, int max_transfer_bytes) {
    if (!isValidHost(host) || sclk_pin < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    spi_bus_config_t cfg = {};
    cfg.mosi_io_num     = mosi_pin;
    cfg.miso_io_num     = miso_pin;
    cfg.sclk_io_num     = sclk_pin;
    cfg.quadwp_io_num   = -1;
    cfg.quadhd_io_num   = -1;
    cfg.max_transfer_sz = (max_transfer_bytes > 0) ? max_transfer_bytes : 0;

    esp_err_t err = spi_bus_initialize((spi_host_device_t)host, &cfg, SPI_DMA_CH_AUTO);
    if (err == ESP_ERR_INVALID_STATE) {
        // Already initialized (e.g. by the XPT2046 touch): share it
        ESP_LOGI(TAG, "SPI%d already initialized, sharing the existing bus", host + 1);
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI%d bus error: %s", host + 1, esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "SPI%d started -> MOSI:%d MISO:%d SCLK:%d", host + 1, mosi_pin, miso_pin, sclk_pin);
    return ESP_OK;
}

esp_err_t SPI_AIoT_bus_free(int host) {
    return isValidHost(host) ? spi_bus_free((spi_host_device_t)host) : ESP_ERR_INVALID_ARG;
}

SPI_AIoT_handle_t SPI_AIoT_create(int host, int cs_pin, uint32_t clock_hz, uint8_t mode) {
    if (!isValidHost(host) || clock_hz == 0 || mode > 3) {
        ESP_LOGE(TAG, "Invalid device arguments (host %d, %u Hz, mode %u)",
                    host, (unsigned)clock_hz, (unsigned)mode);
        return nullptr;
    }
    SPI_AIoT_Device *obj = new (std::nothrow) SPI_AIoT_Device(host, cs_pin, clock_hz, mode);
    if (obj == nullptr || !obj->isValid()) {
        ESP_LOGE(TAG, "No memory for device object");
        delete obj;
        return nullptr;
    }
    return reinterpret_cast<SPI_AIoT_handle_t>(obj);
}

esp_err_t SPI_AIoT_init(SPI_AIoT_handle_t handle) {
    SPI_AIoT_Device *obj = toObj(handle);
    return obj ? obj->begin() : ESP_ERR_INVALID_ARG;
}

void SPI_AIoT_destroy(SPI_AIoT_handle_t handle) {
    delete toObj(handle);   // delete nullptr is safe
}

esp_err_t SPI_AIoT_transfer(SPI_AIoT_handle_t handle, const uint8_t *tx, uint8_t *rx, size_t len) {
    SPI_AIoT_Device *obj = toObj(handle);
    return obj ? obj->transfer(tx, rx, len) : ESP_ERR_INVALID_ARG;
}

esp_err_t SPI_AIoT_write(SPI_AIoT_handle_t handle, const uint8_t *tx, size_t len) {
    SPI_AIoT_Device *obj = toObj(handle);
    return (obj && tx) ? obj->transfer(tx, nullptr, len) : ESP_ERR_INVALID_ARG;
}

esp_err_t SPI_AIoT_read(SPI_AIoT_handle_t handle, uint8_t *rx, size_t len, uint8_t fill) {
    SPI_AIoT_Device *obj = toObj(handle);
    return obj ? obj->read(rx, len, fill) : ESP_ERR_INVALID_ARG;
}

esp_err_t SPI_AIoT_write_then_read(SPI_AIoT_handle_t handle,
                                   const uint8_t *tx, size_t tx_len,
                                   uint8_t *rx, size_t rx_len) {
    SPI_AIoT_Device *obj = toObj(handle);
    return obj ? obj->writeThenRead(tx, tx_len, rx, rx_len) : ESP_ERR_INVALID_ARG;
}

esp_err_t SPI_AIoT_lock(SPI_AIoT_handle_t handle, uint32_t timeout_ms) {
    SPI_AIoT_Device *obj = toObj(handle);
    return obj ? obj->lock(timeout_ms) : ESP_ERR_INVALID_ARG;
}

void SPI_AIoT_unlock(SPI_AIoT_handle_t handle) {
    SPI_AIoT_Device *obj = toObj(handle);
    if (obj) obj->unlock();
}

} // extern "C"
