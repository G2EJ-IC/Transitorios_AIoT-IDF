/*
 * File: components/UARTn_AIoT/src/UARTn_AIoT.cpp
 * Description: UART library for ESP-IDF.
 *              1. C++ class UARTn_AIoT: the real logic.
 *              2. C bridge (extern "C"): the public API of UARTn_AIoT.h.
 * Author: Ing. Ernesto José Guerrero González
 * Standards: ESP-IDF v5.5.1, C++. English comments.
 *
 * Changes versus the previous version:
 *  - read_until() no longer discards the bytes received after the terminator
 *    and waits for the line up to the configured timeout.
 *  - init() undoes the driver installation if a later step fails, and can be
 *    called twice; the destructor only deletes a driver it installed.
 *  - Thread safe (mutex), binary write, read_bytes, available, baud change.
 *  - new (std::nothrow): no abort if memory runs out; arguments validated.
 *  - Optional RS-485 half-duplex mode (DE/RE pin driven by the UART hardware).
 */
#include "UARTn_AIoT.h"

#include <cstring>
#include <new>
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

static const char *TAG = "UARTn_AIoT";

// -----------------------------------------------------------------------------
// 1. C++ CLASS (real logic)
// -----------------------------------------------------------------------------
class UARTn_AIoT {
private:
    uart_port_t       _uart_num;
    int               _tx_pin;
    int               _rx_pin;
    int               _baud_rate;
    int               _de_pin;                              // RS-485 DE/RE pin, -1 = normal UART
    uint32_t          _timeout_ms;
    bool              _installed;
    SemaphoreHandle_t _lock;                                // serializes tasks
    char              _line[UARTN_AIOT_LINE_BUF_SIZE];      // partial line kept between calls
    int               _line_len;

    // Milliseconds to ticks (at least 1 tick when ms > 0)
    static TickType_t msToTicks(uint32_t ms) {
        TickType_t t = pdMS_TO_TICKS(ms);
        return (ms > 0 && t == 0) ? 1 : t;
    }

    // Copies the accumulated line to the user buffer and resets the accumulator
    int deliverLine(char *buffer, int max_len) {
        int n = (_line_len < (max_len - 1)) ? _line_len : (max_len - 1);
        memcpy(buffer, _line, (size_t)n);
        buffer[n] = '\0';
        // Keep the part that did not fit (very long lines are delivered in pieces)
        int rest = _line_len - n;
        if (rest > 0) {
            memmove(_line, _line + n, (size_t)rest);
        }
        _line_len = rest;
        return n;
    }

public:
    // Constructor: stores the configuration only
    UARTn_AIoT(int uart_num, int tx_pin, int rx_pin, int baud_rate)
        : _uart_num((uart_port_t)uart_num),
          _tx_pin((tx_pin < 0) ? UART_PIN_NO_CHANGE : tx_pin),
          _rx_pin((rx_pin < 0) ? UART_PIN_NO_CHANGE : rx_pin),
          _baud_rate(baud_rate),
          _de_pin(-1),
          _timeout_ms(UARTN_AIOT_DEFAULT_TIMEOUT_MS),
          _installed(false),
          _lock(xSemaphoreCreateMutex()),
          _line_len(0) {}

    // Destructor: deletes only what this object created
    ~UARTn_AIoT() {
        if (_installed) {
            uart_driver_delete(_uart_num);
            ESP_LOGI(TAG, "UART%d driver deleted", (int)_uart_num);
        }
        if (_lock != nullptr) {
            vSemaphoreDelete(_lock);
        }
    }

    bool isValid() const { return _lock != nullptr; }

    // Hardware initialization
    esp_err_t begin() {
        if (_installed) {
            return ESP_OK;
        }
        if (_uart_num == UART_NUM_0) {
            ESP_LOGW(TAG, "UART0 is the system console: logs and data will mix");
        }
        if (uart_is_driver_installed(_uart_num)) {
            ESP_LOGE(TAG, "UART%d is already used by another driver", (int)_uart_num);
            return ESP_ERR_INVALID_STATE;
        }

        // Zero-initialized configuration (avoids uninitialized fields)
        uart_config_t cfg = {};
        cfg.baud_rate           = _baud_rate;
        cfg.data_bits           = UART_DATA_8_BITS;
        cfg.parity              = UART_PARITY_DISABLE;
        cfg.stop_bits           = UART_STOP_BITS_1;
        cfg.flow_ctrl           = UART_HW_FLOWCTRL_DISABLE;
        cfg.rx_flow_ctrl_thresh = 122;
        cfg.source_clk          = UART_SCLK_DEFAULT;

        // 1. Driver: large RX buffer so no data from the modem/sensor is lost
        esp_err_t err = uart_driver_install(_uart_num, UARTN_AIOT_RX_BUF_SIZE, 0, 0, nullptr, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "UART%d driver install error: %s", (int)_uart_num, esp_err_to_name(err));
            return err;
        }

        // 2. Parameters, 3. pins and 4. mode (undo the installation if something fails)
        //    In RS-485 mode the RTS signal of the UART drives the DE/RE pin.
        const int rts_pin = (_de_pin >= 0) ? _de_pin : UART_PIN_NO_CHANGE;
        err = uart_param_config(_uart_num, &cfg);
        if (err == ESP_OK) {
            err = uart_set_pin(_uart_num, _tx_pin, _rx_pin, rts_pin, UART_PIN_NO_CHANGE);
        }
        if (err == ESP_OK && _de_pin >= 0) {
            err = uart_set_mode(_uart_num, UART_MODE_RS485_HALF_DUPLEX);
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "UART%d configuration error: %s", (int)_uart_num, esp_err_to_name(err));
            uart_driver_delete(_uart_num);
            return err;
        }

        _installed = true;
        _line_len  = 0;
        if (_de_pin >= 0) {
            ESP_LOGI(TAG, "UART%d started (RS-485) -> TX:%d RX:%d DE:%d @ %d baud",
                     (int)_uart_num, _tx_pin, _rx_pin, _de_pin, _baud_rate);
        } else {
            ESP_LOGI(TAG, "UART%d started -> TX:%d RX:%d @ %d baud",
                     (int)_uart_num, _tx_pin, _rx_pin, _baud_rate);
        }
        return ESP_OK;
    }

    // RS-485 half-duplex: must be selected before begin()
    esp_err_t setRs485(int de_pin) {
        if (de_pin < 0) {
            return ESP_ERR_INVALID_ARG;
        }
        if (_installed) {
            ESP_LOGE(TAG, "Call UARTn_set_rs485() before UARTn_init()");
            return ESP_ERR_INVALID_STATE;
        }
        _de_pin = de_pin;
        return ESP_OK;
    }

    void setTimeout(uint32_t timeout_ms) { _timeout_ms = timeout_ms; }

    esp_err_t setBaudRate(int baud_rate) {
        if (baud_rate <= 0) {
            return ESP_ERR_INVALID_ARG;
        }
        _baud_rate = baud_rate;
        return _installed ? uart_set_baudrate(_uart_num, (uint32_t)baud_rate) : ESP_OK;
    }

    // Binary-safe write
    int writeBytes(const uint8_t *data, size_t len) {
        if (!_installed || (data == nullptr && len > 0)) {
            return -1;
        }
        if (len == 0) {
            return 0;
        }
        xSemaphoreTake(_lock, portMAX_DELAY);
        int written = uart_write_bytes(_uart_num, data, len);
        xSemaphoreGive(_lock);
        return written;
    }

    // String write
    void write(const char *text) {
        if (text != nullptr) {
            (void)writeBytes(reinterpret_cast<const uint8_t *>(text), strlen(text));
        }
    }

    // Line reader: see the contract in UARTn_AIoT.h
    int readUntil(char terminator, char *buffer, int max_len) {
        if (!_installed || buffer == nullptr || max_len <= 0) {
            return -1;
        }
        buffer[0] = '\0';
        if (max_len == 1) {
            return 0;
        }

        xSemaphoreTake(_lock, portMAX_DELAY);

        const TickType_t start   = xTaskGetTickCount();
        const TickType_t timeout = msToTicks(_timeout_ms);
        int result = 0;

        for (;;) {
            // Line already as long as the user buffer allows: deliver a piece
            if (_line_len >= (max_len - 1) || _line_len >= UARTN_AIOT_LINE_BUF_SIZE) {
                result = deliverLine(buffer, max_len);
                break;
            }

            TickType_t elapsed = xTaskGetTickCount() - start;
            TickType_t wait    = (elapsed < timeout) ? (timeout - elapsed) : 0;

            uint8_t c = 0;
            int n = uart_read_bytes(_uart_num, &c, 1, wait);
            if (n < 0) {
                result = -1;                 // driver error
                break;
            }
            if (n == 0) {
                result = 0;                  // timeout: partial line is kept
                break;
            }
            if ((char)c == terminator) {
                if (_line_len == 0) {
                    continue;                // empty line: skip it and keep waiting
                }
                result = deliverLine(buffer, max_len);
                break;
            }
            _line[_line_len++] = (char)c;
        }

        xSemaphoreGive(_lock);
        return result;
    }

    // Raw read with timeout
    int readBytes(uint8_t *data, size_t len) {
        if (!_installed || (data == nullptr && len > 0)) {
            return -1;
        }
        if (len == 0) {
            return 0;
        }
        xSemaphoreTake(_lock, portMAX_DELAY);
        int n = uart_read_bytes(_uart_num, data, len, msToTicks(_timeout_ms));
        xSemaphoreGive(_lock);
        return n;
    }

    size_t available() {
        size_t len = 0;
        if (_installed) {
            uart_get_buffered_data_len(_uart_num, &len);
        }
        return len;
    }

    // Discards the driver buffer and the partial line
    void flush() {
        if (_installed) {
            xSemaphoreTake(_lock, portMAX_DELAY);
            uart_flush_input(_uart_num);
            _line_len = 0;
            xSemaphoreGive(_lock);
        }
    }
};

// Handle <-> object conversion
static inline UARTn_AIoT *toObj(UARTn_handle_t handle) {
    return reinterpret_cast<UARTn_AIoT *>(handle);
}

// -----------------------------------------------------------------------------
// 2. C BRIDGE (extern "C")
// -----------------------------------------------------------------------------
extern "C" {

UARTn_handle_t UARTn_create(int uart_num, int tx_pin, int rx_pin, int baud_rate) {
    if (uart_num < 0 || uart_num >= UART_NUM_MAX || baud_rate <= 0) {
        ESP_LOGE(TAG, "Invalid arguments (uart %d, baud %d)", uart_num, baud_rate);
        return nullptr;
    }
    UARTn_AIoT *obj = new (std::nothrow) UARTn_AIoT(uart_num, tx_pin, rx_pin, baud_rate);
    if (obj == nullptr || !obj->isValid()) {
        ESP_LOGE(TAG, "No memory for UART object");
        delete obj;
        return nullptr;
    }
    return reinterpret_cast<UARTn_handle_t>(obj);
}

esp_err_t UARTn_init(UARTn_handle_t handle) {
    UARTn_AIoT *obj = toObj(handle);
    return obj ? obj->begin() : ESP_ERR_INVALID_ARG;
}

void UARTn_destroy(UARTn_handle_t handle) {
    delete toObj(handle);   // delete nullptr is safe
}

void UARTn_set_timeout(UARTn_handle_t handle, uint32_t timeout_ms) {
    UARTn_AIoT *obj = toObj(handle);
    if (obj) obj->setTimeout(timeout_ms);
}

esp_err_t UARTn_set_baud_rate(UARTn_handle_t handle, int baud_rate) {
    UARTn_AIoT *obj = toObj(handle);
    return obj ? obj->setBaudRate(baud_rate) : ESP_ERR_INVALID_ARG;
}

esp_err_t UARTn_set_rs485(UARTn_handle_t handle, int de_pin) {
    UARTn_AIoT *obj = toObj(handle);
    return obj ? obj->setRs485(de_pin) : ESP_ERR_INVALID_ARG;
}

void UARTn_write(UARTn_handle_t handle, const char *msg) {
    UARTn_AIoT *obj = toObj(handle);
    if (obj) obj->write(msg);
}

int UARTn_write_bytes(UARTn_handle_t handle, const uint8_t *data, size_t len) {
    UARTn_AIoT *obj = toObj(handle);
    return obj ? obj->writeBytes(data, len) : -1;
}

int UARTn_read_until(UARTn_handle_t handle, char terminator, char *buffer, int max_len) {
    UARTn_AIoT *obj = toObj(handle);
    return obj ? obj->readUntil(terminator, buffer, max_len) : -1;
}

int UARTn_read_bytes(UARTn_handle_t handle, uint8_t *data, size_t len) {
    UARTn_AIoT *obj = toObj(handle);
    return obj ? obj->readBytes(data, len) : -1;
}

size_t UARTn_available(UARTn_handle_t handle) {
    UARTn_AIoT *obj = toObj(handle);
    return obj ? obj->available() : 0;
}

void UARTn_flush(UARTn_handle_t handle) {
    UARTn_AIoT *obj = toObj(handle);
    if (obj) obj->flush();
}

} // extern "C"
