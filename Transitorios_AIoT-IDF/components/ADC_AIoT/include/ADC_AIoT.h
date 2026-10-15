/*
 * File: components/ADC_AIoT/include/ADC_AIoT.h
 * Description: High-rate ADC library for ESP-IDF (continuous mode with DMA and
 *              voltage calibration), intended for pressure transducers.
 *              Same philosophy as UARTn_AIoT: the logic lives in the C++
 *              class ADC_AIoT (src/ADC_AIoT.cpp); this header exposes a C API
 *              with an opaque handle.
 *              Life cycle: create -> add_channel -> init -> start -> read ->
 *                          stop -> destroy.
 * Author: Ing. Ernesto José Guerrero González
 * Standards: ESP-IDF v5.5.1, C11 / C++. English comments.
 *
 * Example (two pressure transducers, 2 kHz per channel):
 *   ADC_AIoT_handle_t adc = ADC_AIoT_create(4000);        // 4000 conv/s total
 *   ADC_AIoT_add_channel(adc, 4, ADC_AIOT_ATTEN_12DB);    // index 0 -> GPIO 4
 *   ADC_AIoT_add_channel(adc, 5, ADC_AIOT_ATTEN_12DB);    // index 1 -> GPIO 5
 *   ADC_AIoT_init(adc);
 *   ADC_AIoT_start(adc);
 *   ADC_AIoT_sample_t s[256];
 *   int n = ADC_AIoT_read(adc, s, 256, 100);              // s[i].index, s[i].mv
 *
 * Notes:
 *  - Only ADC1 is used (GPIO 1..10 on the ESP32-S3): ADC2 conflicts with WiFi.
 *  - On the CrowPanel 4.3" those GPIOs are taken by the RGB display, so this
 *    library is meant for a separate SENSOR NODE (ESP32-S3 board).
 *  - The sample rate is the TOTAL for all channels (611..83333 Hz on the S3).
 *    Rate per channel = sample_rate / number_of_channels.
 *  - Input range with 12 dB attenuation: about 0..3.1 V. A 0.5..4.5 V
 *    transducer needs a resistor divider; never exceed 3.3 V on a pin.
 *  - Call ADC_AIoT_read() often enough: if the DMA pool fills up, the oldest
 *    data is discarded and ADC_AIoT_get_overruns() increases.
 */
#ifndef ADC_AIOT_H
#define ADC_AIOT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle: points to the ADC_AIoT C++ object */
typedef struct ADC_AIoT_Instance *ADC_AIoT_handle_t;

#define ADC_AIOT_MAX_CHANNELS    10      /* ADC1 channels on the ESP32-S3 */
#define ADC_AIOT_RATE_MIN_HZ     611
#define ADC_AIOT_RATE_MAX_HZ     83333

/* Input attenuation (approximate full scale) */
typedef enum {
    ADC_AIOT_ATTEN_0DB   = 0,    /* ~0..0.95 V */
    ADC_AIOT_ATTEN_2_5DB = 1,    /* ~0..1.25 V */
    ADC_AIOT_ATTEN_6DB   = 2,    /* ~0..1.75 V */
    ADC_AIOT_ATTEN_12DB  = 3,    /* ~0..3.1 V  */
} ADC_AIoT_atten_t;

/* One converted sample */
typedef struct {
    uint8_t  index;      /* channel index, in the order of ADC_AIoT_add_channel() */
    uint8_t  gpio;       /* GPIO of that channel */
    uint16_t raw;        /* 12-bit raw value 0..4095 */
    int32_t  mv;         /* calibrated voltage in millivolts */
} ADC_AIoT_sample_t;

/* -------------------------------------------------------------------------- */
/* Constructor / configuration / destructor                                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief Creates the ADC object (no hardware touched yet).
 * @param sample_rate_hz Total conversions per second for all channels
 *                       (ADC_AIOT_RATE_MIN_HZ..ADC_AIOT_RATE_MAX_HZ)
 */
ADC_AIoT_handle_t ADC_AIoT_create(uint32_t sample_rate_hz);

/**
 * @brief Adds an input (call BEFORE ADC_AIoT_init). Up to 10 channels.
 * @param gpio  ADC1 pin (GPIO 1..10 on the ESP32-S3)
 * @param atten Input range
 * @return ESP_OK, ESP_ERR_INVALID_ARG (not an ADC1 pin / repeated),
 *         ESP_ERR_INVALID_STATE (already initialized) or ESP_ERR_NO_MEM (full).
 */
esp_err_t ADC_AIoT_add_channel(ADC_AIoT_handle_t handle, int gpio, ADC_AIoT_atten_t atten);

/**
 * @brief Creates the DMA driver and the calibration of every channel.
 */
esp_err_t ADC_AIoT_init(ADC_AIoT_handle_t handle);

/**
 * @brief Stops the conversions, releases the driver and frees the object.
 */
void ADC_AIoT_destroy(ADC_AIoT_handle_t handle);

/* -------------------------------------------------------------------------- */
/* Acquisition                                                                 */
/* -------------------------------------------------------------------------- */

/** @brief Starts the continuous conversions */
esp_err_t ADC_AIoT_start(ADC_AIoT_handle_t handle);

/** @brief Stops the continuous conversions */
esp_err_t ADC_AIoT_stop(ADC_AIoT_handle_t handle);

/**
 * @brief Copies the converted samples available in the DMA pool.
 * @param samples     Destination array
 * @param max_samples Size of the array
 * @param timeout_ms  Maximum wait for data (0 = do not wait)
 * @return Number of samples stored (0 if none arrived), -1 on error.
 */
int ADC_AIoT_read(ADC_AIoT_handle_t handle, ADC_AIoT_sample_t *samples, size_t max_samples, uint32_t timeout_ms);

/* -------------------------------------------------------------------------- */
/* Information                                                                 */
/* -------------------------------------------------------------------------- */

/** @brief Samples per second of EACH channel */
uint32_t ADC_AIoT_get_rate_per_channel(ADC_AIoT_handle_t handle);

/** @brief Times the DMA pool overflowed (data lost because reads were late) */
uint32_t ADC_AIoT_get_overruns(ADC_AIoT_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* ADC_AIOT_H */
