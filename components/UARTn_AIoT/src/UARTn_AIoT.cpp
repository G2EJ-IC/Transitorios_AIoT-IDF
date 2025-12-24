#include "UARTn_AIoT.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <cstring>
#include <cstdlib>

static const char *TAG = "UARTn_CLASS";

// -----------------------------------------------------------------------------
// 1. DEFINICIÓN DE LA CLASE C++ (Lógica Real)
// -----------------------------------------------------------------------------
class UARTn_AIoT {
private:
    uart_port_t _uart_num;
    int _tx_pin;
    int _rx_pin;
    int _baud_rate;
    static const int BUF_SIZE = 1024;

public:
    // Constructor
    UARTn_AIoT(int uart_num, int tx_pin, int rx_pin, int baud_rate) 
        : _uart_num((uart_port_t)uart_num), _tx_pin(tx_pin), _rx_pin(rx_pin), _baud_rate(baud_rate) {}

    // Inicialización del Hardware
    esp_err_t begin() {
        // [CORRECCIÓN] Inicializamos la estructura con ceros para evitar advertencias del compilador
        uart_config_t uart_config = {}; 
        
        uart_config.baud_rate = _baud_rate;
        uart_config.data_bits = UART_DATA_8_BITS;
        uart_config.parity = UART_PARITY_DISABLE;
        uart_config.stop_bits = UART_STOP_BITS_1;
        uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
        uart_config.rx_flow_ctrl_thresh = 122; // Valor por defecto seguro
        uart_config.source_clk = UART_SCLK_DEFAULT;

        // 1. Instalamos el driver (Buffer RX: 2048, TX: 0)
        // Usamos un buffer amplio para no perder datos del SIM800L
        esp_err_t err = uart_driver_install(_uart_num, BUF_SIZE * 2, 0, 0, NULL, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error instalando driver UART%d: %s", _uart_num, esp_err_to_name(err));
            return err;
        }

        // 2. Configuramos parámetros
        err = uart_param_config(_uart_num, &uart_config);
        if (err != ESP_OK) {
             ESP_LOGE(TAG, "Error config parametros: %s", esp_err_to_name(err));
             return err;
        }

        // 3. Asignamos pines
        err = uart_set_pin(_uart_num, _tx_pin, _rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        if (err != ESP_OK) {
             ESP_LOGE(TAG, "Error asignando pines: %s", esp_err_to_name(err));
             return err;
        }

        ESP_LOGI(TAG, "UART%d Iniciado -> TX:%d RX:%d @ %d baud", _uart_num, _tx_pin, _rx_pin, _baud_rate);
        return ESP_OK;
    }

    // Método Write
    void write(const char* text) {
        if (text) {
            uart_write_bytes(_uart_num, text, strlen(text));
        }
    }

    // Método Read Until (Simplificado para robustez)
    int readUntil(char terminator, char* buffer, int max_len) {
        size_t length = 0;
        uart_get_buffered_data_len(_uart_num, &length);
        
        if (length > 0) {
            // Leemos todo lo disponible
            uint8_t* temp_buf = (uint8_t*)malloc(length + 1);
            if (!temp_buf) return 0; // Error de memoria

            int read_len = uart_read_bytes(_uart_num, temp_buf, length, pdMS_TO_TICKS(20));
            temp_buf[read_len] = '\0'; // Null termination

            // Buscamos el terminador
            char* found = strchr((char*)temp_buf, terminator);
            int result_len = 0;

            if (found) {
                // Calcular longitud hasta el terminador
                result_len = found - (char*)temp_buf;
                
                // Si el mensaje es más grande que el buffer del usuario, cortamos
                if (result_len >= max_len) result_len = max_len - 1;
                
                strncpy(buffer, (char*)temp_buf, result_len);
                buffer[result_len] = '\0'; // Asegurar string válido
            } else {
                // Si no hay terminador, copiamos todo lo que hay (modo raw)
                if ((int)length >= max_len) length = max_len - 1;
                strncpy(buffer, (char*)temp_buf, length);
                buffer[length] = '\0';
                result_len = length;
            }

            free(temp_buf);
            return result_len;
        }
        return 0;
    }

    // Método Flush: Limpia basura del buffer
    void flush() {
        uart_flush_input(_uart_num);
    }

    // Destructor
    ~UARTn_AIoT() {
        uart_driver_delete(_uart_num);
        ESP_LOGW(TAG, "UART Driver Eliminado");
    }
};

// -----------------------------------------------------------------------------
// 2. IMPLEMENTACIÓN DEL PUENTE C (Extern "C")
// -----------------------------------------------------------------------------

extern "C" {

    UARTn_handle_t UARTn_create(int uart_num, int tx_pin, int rx_pin, int baud_rate) {
        return (UARTn_handle_t) new UARTn_AIoT(uart_num, tx_pin, rx_pin, baud_rate);
    }

    esp_err_t UARTn_init(UARTn_handle_t handle) {
        UARTn_AIoT* obj = (UARTn_AIoT*)handle;
        return obj ? obj->begin() : ESP_FAIL;
    }

    void UARTn_write(UARTn_handle_t handle, const char *msg) {
        UARTn_AIoT* obj = (UARTn_AIoT*)handle;
        if (obj) obj->write(msg);
    }

    int UARTn_read_until(UARTn_handle_t handle, char terminator, char *buffer, int max_len) {
        UARTn_AIoT* obj = (UARTn_AIoT*)handle;
        return obj ? obj->readUntil(terminator, buffer, max_len) : 0;
    }

    void UARTn_flush(UARTn_handle_t handle) {
        UARTn_AIoT* obj = (UARTn_AIoT*)handle;
        if (obj) obj->flush();
    }

    void UARTn_destroy(UARTn_handle_t handle) {
        UARTn_AIoT* obj = (UARTn_AIoT*)handle;
        if (obj) {
            delete obj;
        }
    }
}