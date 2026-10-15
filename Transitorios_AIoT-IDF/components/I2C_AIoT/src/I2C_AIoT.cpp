/*
 * File: components/I2C_AIoT/src/I2C_AIoT.cpp
 * Description: I2C master library for ESP-IDF (driver/i2c_master.h).
 *              1. C++ classes I2C_AIoT_Bus and I2C_AIoT_Device: the real logic.
 *              2. C bridge (extern "C"): the public API of I2C_AIoT.h.
 * Author: Ing. Ernesto José Guerrero González
 * Standards: ESP-IDF v5.5.1, C++. English comments.
 */
#include "I2C_AIoT.h"

#include <new>
#include "driver/i2c_master.h"
#include "soc/soc_caps.h"
#include "esp_log.h"

static const char *TAG = "I2C_AIoT";

// -----------------------------------------------------------------------------
// 1. C++ CLASSES (real logic)
// -----------------------------------------------------------------------------

// I2C bus: one per port (SDA + SCL)
class I2C_AIoT_Bus {
private:
    i2c_port_num_t          _port;
    int                     _sda_pin;
    int                     _scl_pin;
    bool                    _internal_pullup;
    i2c_master_bus_handle_t _bus;            // nullptr until begin()

public:
    // Constructor: stores the configuration only
    I2C_AIoT_Bus(int port, int sda_pin, int scl_pin, bool internal_pullup)
        : _port((i2c_port_num_t)port), _sda_pin(sda_pin), _scl_pin(scl_pin),
          _internal_pullup(internal_pullup), _bus(nullptr) {}

    // Destructor: removes the bus (its devices must be destroyed before)
    ~I2C_AIoT_Bus() {
        if (_bus != nullptr) {
            esp_err_t err = i2c_del_master_bus(_bus);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "I2C%d delete error: %s (destroy the devices first)",
                         (int)_port, esp_err_to_name(err));
            }
        }
    }

    i2c_master_bus_handle_t raw() const { return _bus; }

    // Hardware initialization
    esp_err_t begin() {
        if (_bus != nullptr) {
            return ESP_OK;
        }

        // Zero-initialized configuration (avoids uninitialized fields)
        i2c_master_bus_config_t cfg = {};
        cfg.i2c_port                     = _port;
        cfg.sda_io_num                   = (gpio_num_t)_sda_pin;
        cfg.scl_io_num                   = (gpio_num_t)_scl_pin;
        cfg.clk_source                   = I2C_CLK_SRC_DEFAULT;
        cfg.glitch_ignore_cnt            = 7;
        cfg.intr_priority                = 0;      // driver default
        cfg.trans_queue_depth            = 0;      // synchronous transfers only
        cfg.flags.enable_internal_pullup = _internal_pullup ? 1 : 0;

        esp_err_t err = i2c_new_master_bus(&cfg, &_bus);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "I2C%d bus error: %s", (int)_port, esp_err_to_name(err));
            _bus = nullptr;
            return err;
        }
        ESP_LOGI(TAG, "I2C%d started -> SDA:%d SCL:%d pull-ups:%s",
                 (int)_port, _sda_pin, _scl_pin, _internal_pullup ? "ON" : "OFF");
        return ESP_OK;
    }

    // ACK check at one 7-bit address
    esp_err_t probe(uint8_t addr7) {
        if (_bus == nullptr || addr7 > 0x7F) {
            return ESP_ERR_INVALID_ARG;
        }
        return i2c_master_probe(_bus, addr7, I2C_AIOT_DEFAULT_TIMEOUT_MS);
    }

    // Scans 0x08..0x77 (0x00-0x07 and 0x78-0x7F are reserved addresses)
    int scan(uint8_t *found, int max_found) {
        if (_bus == nullptr) {
            return -1;
        }
        int count = 0;
        ESP_LOGI(TAG, "Scanning I2C%d...", (int)_port);
        for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
            if (i2c_master_probe(_bus, addr, I2C_AIOT_DEFAULT_TIMEOUT_MS) == ESP_OK) {
                ESP_LOGI(TAG, "  device found at 0x%02X", addr);
                if (found != nullptr && count < max_found) {
                    found[count] = addr;
                }
                count++;
            }
        }
        ESP_LOGI(TAG, "Scan finished: %d device(s)", count);
        return count;
    }
};

// I2C device: one per sensor on a bus
class I2C_AIoT_Device {
private:
    I2C_AIoT_Bus           *_parent;
    uint8_t                 _addr7;
    uint32_t                _speed_hz;
    int                     _timeout_ms;
    i2c_master_dev_handle_t _dev;            // nullptr until begin()

public:
    // Constructor: stores the configuration only
    I2C_AIoT_Device(I2C_AIoT_Bus *parent, uint8_t addr7, uint32_t speed_hz)
        : _parent(parent), _addr7(addr7), _speed_hz(speed_hz),
          _timeout_ms(I2C_AIOT_DEFAULT_TIMEOUT_MS), _dev(nullptr) {}

    // Destructor: detaches the device from its bus
    ~I2C_AIoT_Device() {
        if (_dev != nullptr) {
            i2c_master_bus_rm_device(_dev);
        }
    }

    // Attaches the device to the (already initialized) bus
    esp_err_t begin() {
        if (_dev != nullptr) {
            return ESP_OK;
        }
        if (_parent->raw() == nullptr) {
            ESP_LOGE(TAG, "Bus not initialized: call I2C_AIoT_bus_init() first");
            return ESP_ERR_INVALID_STATE;
        }

        i2c_device_config_t cfg = {};
        cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        cfg.device_address  = _addr7;
        cfg.scl_speed_hz    = _speed_hz;
        cfg.scl_wait_us     = 0;             // driver default

        esp_err_t err = i2c_master_bus_add_device(_parent->raw(), &cfg, &_dev);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Device 0x%02X error: %s", _addr7, esp_err_to_name(err));
            _dev = nullptr;
            return err;
        }
        ESP_LOGI(TAG, "Device 0x%02X attached @ %u Hz", _addr7, (unsigned)_speed_hz);
        return ESP_OK;
    }

    void setTimeout(uint32_t timeout_ms) { _timeout_ms = (int)timeout_ms; }

    esp_err_t write(const uint8_t *data, size_t len) {
        if (_dev == nullptr || data == nullptr || len == 0) {
            return ESP_ERR_INVALID_ARG;
        }
        return i2c_master_transmit(_dev, data, len, _timeout_ms);
    }

    esp_err_t read(uint8_t *data, size_t len) {
        if (_dev == nullptr || data == nullptr || len == 0) {
            return ESP_ERR_INVALID_ARG;
        }
        return i2c_master_receive(_dev, data, len, _timeout_ms);
    }

    // Write + repeated START + read (no STOP in between)
    esp_err_t writeRead(const uint8_t *tx, size_t tx_len, uint8_t *rx, size_t rx_len) {
        if (_dev == nullptr || tx == nullptr || tx_len == 0 || rx == nullptr || rx_len == 0) {
            return ESP_ERR_INVALID_ARG;
        }
        return i2c_master_transmit_receive(_dev, tx, tx_len, rx, rx_len, _timeout_ms);
    }

    esp_err_t writeReg(uint8_t reg, uint8_t value) {
        const uint8_t buf[2] = { reg, value };
        return write(buf, sizeof(buf));
    }

    esp_err_t readRegs(uint8_t reg, uint8_t *data, size_t len) {
        return writeRead(&reg, 1, data, len);
    }
};

// Handle <-> object conversion
static inline I2C_AIoT_Bus *toBus(I2C_AIoT_bus_t bus) {
    return reinterpret_cast<I2C_AIoT_Bus *>(bus);
}
static inline I2C_AIoT_Device *toDev(I2C_AIoT_handle_t handle) {
    return reinterpret_cast<I2C_AIoT_Device *>(handle);
}

// -----------------------------------------------------------------------------
// 2. C BRIDGE (extern "C")
// -----------------------------------------------------------------------------
extern "C" {

I2C_AIoT_bus_t I2C_AIoT_bus_create(int port, int sda_pin, int scl_pin, bool internal_pullup) {
    if (port < 0 || port >= (int)SOC_HP_I2C_NUM || sda_pin < 0 || scl_pin < 0 || sda_pin == scl_pin) {
        ESP_LOGE(TAG, "Invalid bus arguments (port %d, SDA %d, SCL %d)", port, sda_pin, scl_pin);
        return nullptr;
    }
    I2C_AIoT_Bus *obj = new (std::nothrow) I2C_AIoT_Bus(port, sda_pin, scl_pin, internal_pullup);
    if (obj == nullptr) {
        ESP_LOGE(TAG, "No memory for bus object");
    }
    return reinterpret_cast<I2C_AIoT_bus_t>(obj);
}

esp_err_t I2C_AIoT_bus_init(I2C_AIoT_bus_t bus) {
    I2C_AIoT_Bus *obj = toBus(bus);
    return obj ? obj->begin() : ESP_ERR_INVALID_ARG;
}

esp_err_t I2C_AIoT_probe(I2C_AIoT_bus_t bus, uint8_t addr7) {
    I2C_AIoT_Bus *obj = toBus(bus);
    return obj ? obj->probe(addr7) : ESP_ERR_INVALID_ARG;
}

int I2C_AIoT_scan(I2C_AIoT_bus_t bus, uint8_t *found, int max_found) {
    I2C_AIoT_Bus *obj = toBus(bus);
    return obj ? obj->scan(found, max_found) : -1;
}

void I2C_AIoT_bus_destroy(I2C_AIoT_bus_t bus) {
    delete toBus(bus);   // delete nullptr is safe
}

I2C_AIoT_handle_t I2C_AIoT_create(I2C_AIoT_bus_t bus, uint8_t addr7, uint32_t speed_hz) {
    if (bus == nullptr || addr7 > 0x7F || speed_hz == 0) {
        ESP_LOGE(TAG, "Invalid device arguments (addr 0x%02X, %u Hz)", addr7, (unsigned)speed_hz);
        return nullptr;
    }
    I2C_AIoT_Device *obj = new (std::nothrow) I2C_AIoT_Device(toBus(bus), addr7, speed_hz);
    if (obj == nullptr) {
        ESP_LOGE(TAG, "No memory for device object");
    }
    return reinterpret_cast<I2C_AIoT_handle_t>(obj);
}

esp_err_t I2C_AIoT_init(I2C_AIoT_handle_t handle) {
    I2C_AIoT_Device *obj = toDev(handle);
    return obj ? obj->begin() : ESP_ERR_INVALID_ARG;
}

void I2C_AIoT_destroy(I2C_AIoT_handle_t handle) {
    delete toDev(handle);   // delete nullptr is safe
}

void I2C_AIoT_set_timeout(I2C_AIoT_handle_t handle, uint32_t timeout_ms) {
    I2C_AIoT_Device *obj = toDev(handle);
    if (obj) obj->setTimeout(timeout_ms);
}

esp_err_t I2C_AIoT_write(I2C_AIoT_handle_t handle, const uint8_t *data, size_t len) {
    I2C_AIoT_Device *obj = toDev(handle);
    return obj ? obj->write(data, len) : ESP_ERR_INVALID_ARG;
}

esp_err_t I2C_AIoT_read(I2C_AIoT_handle_t handle, uint8_t *data, size_t len) {
    I2C_AIoT_Device *obj = toDev(handle);
    return obj ? obj->read(data, len) : ESP_ERR_INVALID_ARG;
}

esp_err_t I2C_AIoT_write_read(I2C_AIoT_handle_t handle,
                              const uint8_t *tx, size_t tx_len,
                              uint8_t *rx, size_t rx_len) {
    I2C_AIoT_Device *obj = toDev(handle);
    return obj ? obj->writeRead(tx, tx_len, rx, rx_len) : ESP_ERR_INVALID_ARG;
}

esp_err_t I2C_AIoT_write_reg(I2C_AIoT_handle_t handle, uint8_t reg, uint8_t value) {
    I2C_AIoT_Device *obj = toDev(handle);
    return obj ? obj->writeReg(reg, value) : ESP_ERR_INVALID_ARG;
}

esp_err_t I2C_AIoT_read_reg(I2C_AIoT_handle_t handle, uint8_t reg, uint8_t *value) {
    I2C_AIoT_Device *obj = toDev(handle);
    return obj ? obj->readRegs(reg, value, 1) : ESP_ERR_INVALID_ARG;
}

esp_err_t I2C_AIoT_read_regs(I2C_AIoT_handle_t handle, uint8_t reg, uint8_t *data, size_t len) {
    I2C_AIoT_Device *obj = toDev(handle);
    return obj ? obj->readRegs(reg, data, len) : ESP_ERR_INVALID_ARG;
}

} // extern "C"
