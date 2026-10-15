/*
 * File: components/Adquisicion_AIoT/src/Adquisicion_AIoT.c
 * Description: Acquisition engine of the SENSOR NODE (see Adquisicion_AIoT.h).
 *
 *   esp_timer (every 1/AIOT_SAMPLE_RATE_HZ s)
 *        |  task notification
 *        v
 *   aiot_acq (core 1) --> pressure (ADC_AIoT or ADS1115_AIoT)
 *        |            --> IMU (MPU6050_AIoT)
 *        |            --> flow (PCNT_AIoT, every AIOT_FLOW_UPDATE_MS)
 *        |  AIoT_frame_t every AIOT_FRAME_SAMPLES samples (queue)
 *        v
 *   aiot_tx (core 0)  --> ESPNOW_AIoT_send() --> CrowPanel
 *
 * Author: Ing. Ernesto José Guerrero González
 * Standards: ESP-IDF v5.5.1, C11. English comments.
 */
#include "Adquisicion_AIoT.h"

#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_log.h"

#include "Sensor_Defines_AIoT.h"
#include "Frame_AIoT.h"
#include "I2C_AIoT.h"
#include "ADC_AIoT.h"
#include "ADS1115_AIoT.h"
#include "MPU6050_AIoT.h"
#include "PCNT_AIoT.h"
#include "ESPNOW_AIoT.h"

static const char *TAG = "Adquisicion";

#define SAMPLE_PERIOD_US    (1000000U / AIOT_SAMPLE_RATE_HZ)
#define ADC_READ_CHUNK      64          /* samples per ADC_AIoT_read() call */

/* The I2C bus is needed by the IMU and by the ADS1115 */
#define AIOT_NEEDS_I2C      (AIOT_IMU_ENABLED || (AIOT_PRESSURE_SOURCE == AIOT_PRESSURE_SOURCE_ADS1115))

/* -------------------------------------------------------------------------- */
/* Module state                                                               */
/* -------------------------------------------------------------------------- */
#if AIOT_NEEDS_I2C
static I2C_AIoT_bus_t        s_bus        = NULL;
#endif
#if AIOT_PRESSURE_SOURCE == AIOT_PRESSURE_SOURCE_ADC
static ADC_AIoT_handle_t     s_adc        = NULL;
#else
static ADS1115_AIoT_handle_t s_ads        = NULL;
#endif
static MPU6050_AIoT_handle_t s_imu        = NULL;
static PCNT_AIoT_handle_t    s_flow       = NULL;
static ESPNOW_AIoT_handle_t  s_link       = NULL;
static QueueHandle_t         s_tx_queue   = NULL;
static TaskHandle_t          s_acq_task   = NULL;
static esp_timer_handle_t    s_tick_timer = NULL;

static uint8_t s_peer[ESPNOW_AIOT_MAC_LEN] = AIOT_PEER_MAC;
static bool    s_unicast     = false;
static bool    s_pressure_ok = false;
static bool    s_imu_ok      = false;
static bool    s_flow_ok     = false;

/* Statistics: written by the tasks, copied by Adquisicion_AIoT_get_stats() */
static Adquisicion_AIoT_stats_t s_stats;
static portMUX_TYPE             s_stats_mux = portMUX_INITIALIZER_UNLOCKED;

/* Transducer ranges, indexed by channel */
static const float k_p_min[2]    = { AIOT_P1_MIN_KPA,    AIOT_P2_MIN_KPA };
static const float k_p_max[2]    = { AIOT_P1_MAX_KPA,    AIOT_P2_MAX_KPA };
static const float k_p_offset[2] = { AIOT_P1_OFFSET_KPA, AIOT_P2_OFFSET_KPA };
static const uint16_t k_p_fault[2] = { AIOT_STATUS_P1_FAULT, AIOT_STATUS_P2_FAULT };

/* -------------------------------------------------------------------------- */
/* Pressure                                                                   */
/* -------------------------------------------------------------------------- */

/* Shunt voltage [mV] -> gauge pressure [kPa]. Open loop -> NaN + fault bit. */
static float shunt_mv_to_kpa(int ch, float mv, uint16_t *status)
{
    const float ma = mv / AIOT_SHUNT_OHMS;              /* mV / Ohm = mA */
    if (ma < AIOT_LOOP_FAULT_LOW_MA) {
        *status |= k_p_fault[ch];
        return NAN;
    }
    if (ma > AIOT_LOOP_FAULT_HIGH_MA) {
        *status |= k_p_fault[ch];                       /* over range: keep the value */
    }
    const float span = k_p_max[ch] - k_p_min[ch];
    return k_p_min[ch] + (ma - AIOT_LOOP_MIN_MA) * span / (AIOT_LOOP_MAX_MA - AIOT_LOOP_MIN_MA)
           + k_p_offset[ch];
}

static esp_err_t pressure_init(void)
{
#if AIOT_PRESSURE_SOURCE == AIOT_PRESSURE_SOURCE_ADC
    static const int k_gpio[2] = { AIOT_P1_GPIO, AIOT_P2_GPIO };
    s_adc = ADC_AIoT_create(AIOT_ADC_TOTAL_RATE_HZ);
    if (s_adc == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = ESP_OK;
    for (int ch = 0; ch < AIOT_PRESSURE_CHANNELS && err == ESP_OK; ch++) {
        err = ADC_AIoT_add_channel(s_adc, k_gpio[ch], ADC_AIOT_ATTEN_12DB);
    }
    if (err == ESP_OK) err = ADC_AIoT_init(s_adc);
    if (err == ESP_OK) err = ADC_AIoT_start(s_adc);
    if (err != ESP_OK) {
        ADC_AIoT_destroy(s_adc);
        s_adc = NULL;
        return err;
    }
    ESP_LOGI(TAG, "Pressure: internal ADC, %d channel(s), %u conversions averaged per sample",
             AIOT_PRESSURE_CHANNELS,
             (unsigned)(AIOT_ADC_TOTAL_RATE_HZ / AIOT_PRESSURE_CHANNELS / AIOT_SAMPLE_RATE_HZ));
    return ESP_OK;
#else
    if (s_bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_ads = ADS1115_AIoT_create(s_bus, AIOT_ADS1115_ADDR, AIOT_I2C_SPEED_HZ);
    if (s_ads == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = ADS1115_AIoT_set_pga(s_ads, ADS1115_AIOT_PGA_4096MV);
    if (err == ESP_OK) err = ADS1115_AIoT_set_data_rate(s_ads, ADS1115_AIOT_DR_860SPS);
    if (err == ESP_OK) err = ADS1115_AIoT_init(s_ads);
    if (err == ESP_OK) err = ADS1115_AIoT_start_continuous(s_ads, (ADS1115_AIoT_input_t)AIOT_ADS1115_INPUT);
    if (err != ESP_OK) {
        ADS1115_AIoT_destroy(s_ads);
        s_ads = NULL;
        return err;
    }
    ESP_LOGI(TAG, "Pressure: ADS1115 at 0x%02X, AIN%d, 860 SPS", AIOT_ADS1115_ADDR, AIOT_ADS1115_INPUT);
    return ESP_OK;
#endif
}

/* One pressure sample per channel, synchronized with the sampling tick */
static void pressure_sample(float p_kpa[AIOT_FRAME_PRESSURE_CH], uint16_t *status)
{
    for (int ch = 0; ch < AIOT_FRAME_PRESSURE_CH; ch++) {
        p_kpa[ch] = NAN;
    }
    if (!s_pressure_ok) {
        for (int ch = 0; ch < AIOT_PRESSURE_CHANNELS; ch++) {
            *status |= k_p_fault[ch];
        }
        return;
    }

#if AIOT_PRESSURE_SOURCE == AIOT_PRESSURE_SOURCE_ADC
    /* Average of every conversion received since the previous tick (boxcar
     * filter = anti-aliasing). If a channel got no data, keep its last value. */
    static ADC_AIoT_sample_t buf[ADC_READ_CHUNK];       /* static: off the stack */
    static float    last_mv[AIOT_PRESSURE_CHANNELS];
    static bool     have_mv[AIOT_PRESSURE_CHANNELS];
    static uint32_t last_overruns = 0;

    int64_t  sum[AIOT_PRESSURE_CHANNELS] = { 0 };
    uint32_t cnt[AIOT_PRESSURE_CHANNELS] = { 0 };
    int n;
    do {
        n = ADC_AIoT_read(s_adc, buf, ADC_READ_CHUNK, 0);
        for (int i = 0; i < n; i++) {
            const uint8_t ch = buf[i].index;
            if (ch < AIOT_PRESSURE_CHANNELS) {
                sum[ch] += buf[i].mv;
                cnt[ch]++;
            }
        }
    } while (n == ADC_READ_CHUNK);

    for (int ch = 0; ch < AIOT_PRESSURE_CHANNELS; ch++) {
        if (cnt[ch] > 0) {
            last_mv[ch] = (float)sum[ch] / (float)cnt[ch];
            have_mv[ch] = true;
        }
        if (have_mv[ch]) {
            p_kpa[ch] = shunt_mv_to_kpa(ch, last_mv[ch], status);
        } else {
            *status |= k_p_fault[ch];
        }
    }

    const uint32_t ovr = ADC_AIoT_get_overruns(s_adc);
    if (ovr != last_overruns) {
        last_overruns = ovr;
        *status |= AIOT_STATUS_ADC_OVERRUN;
    }
#else
    float mv = 0.0f;
    if (ADS1115_AIoT_read_mv(s_ads, &mv) == ESP_OK) {
        p_kpa[0] = shunt_mv_to_kpa(0, mv, status);
    } else {
        *status |= AIOT_STATUS_P1_FAULT;
    }
#endif
}

/* -------------------------------------------------------------------------- */
/* IMU and flow meter                                                         */
/* -------------------------------------------------------------------------- */

#if AIOT_IMU_ENABLED
static esp_err_t imu_init(void)
{
    if (s_bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_imu = MPU6050_AIoT_create(s_bus, AIOT_MPU6050_ADDR, AIOT_I2C_SPEED_HZ);
    if (s_imu == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = MPU6050_AIoT_set_ranges(s_imu, AIOT_IMU_ACCEL_FS, AIOT_IMU_GYRO_FS);
    if (err == ESP_OK) err = MPU6050_AIoT_set_dlpf(s_imu, AIOT_IMU_DLPF);
    if (err == ESP_OK) err = MPU6050_AIoT_init(s_imu);
    if (err != ESP_OK) {
        MPU6050_AIoT_destroy(s_imu);
        s_imu = NULL;
        return err;
    }
#if AIOT_IMU_GYRO_CAL_SAMPLES > 0
    ESP_LOGI(TAG, "Gyro calibration: keep the node still...");
    (void)MPU6050_AIoT_calibrate_gyro(s_imu, AIOT_IMU_GYRO_CAL_SAMPLES);
#endif
    return ESP_OK;
}
#endif /* AIOT_IMU_ENABLED */

#if AIOT_FLOW_ENABLED
static esp_err_t flow_init(void)
{
    s_flow = PCNT_AIoT_create(AIOT_FLOW_GPIO, AIOT_FLOW_GLITCH_NS);
    if (s_flow == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = PCNT_AIoT_set_k_factor(s_flow, AIOT_FLOW_K_PULSES_PER_L);
    if (err == ESP_OK) err = PCNT_AIoT_init(s_flow);
    if (err != ESP_OK) {
        PCNT_AIoT_destroy(s_flow);
        s_flow = NULL;
        return err;
    }
    ESP_LOGI(TAG, "Flow meter: GPIO %d, K = %.1f pulses/L", AIOT_FLOW_GPIO, (double)AIOT_FLOW_K_PULSES_PER_L);
    return ESP_OK;
}
#endif /* AIOT_FLOW_ENABLED */

/* -------------------------------------------------------------------------- */
/* Tasks                                                                      */
/* -------------------------------------------------------------------------- */

/* esp_timer callback (esp_timer task): wake the acquisition task */
static void tick_cb(void *arg)
{
    (void)arg;
    xTaskNotifyGive(s_acq_task);
}

static void acq_task(void *arg)
{
    (void)arg;
    /* Packed frame: members are written one by one (never through a float
     * pointer, which could be unaligned on the Xtensa core) */
    static AIoT_frame_t frame __attribute__((aligned(4)));
    memset(&frame, 0, sizeof(frame));

    uint32_t seq            = 0;
    int      idx            = 0;
    uint16_t status         = 0;
    uint16_t carry_status   = 0;        /* bits that belong to the next frame */
    int64_t  t_prev         = 0;
    int64_t  t_flow         = esp_timer_get_time();
    float    flow_lps       = NAN;
    float    volume_l       = NAN;
    float    temp_c         = NAN;

    /* Local counters, published once per frame */
    uint32_t samples = 0, frames = 0, dropped = 0, missed = 0, imu_err = 0;

    for (;;) {
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100)) == 0) {
            continue;                                   /* timer stopped */
        }
        const int64_t t = esp_timer_get_time();

        /* Late detection: gap larger than 1.5 periods */
        if (t_prev != 0 && (t - t_prev) > (int64_t)(SAMPLE_PERIOD_US * 3U / 2U)) {
            missed += (uint32_t)(((t - t_prev) + SAMPLE_PERIOD_US / 2) / SAMPLE_PERIOD_US) - 1U;
            status |= AIOT_STATUS_TIMING_LATE;
        }
        t_prev = t;

        if (idx == 0) {
            frame.t0_us = (uint64_t)t;
        }
        AIoT_sample_t *s = &frame.s[idx];

        /* Pressure (computed in an aligned local array, then copied) */
        float p[AIOT_FRAME_PRESSURE_CH];
        pressure_sample(p, &status);
        for (int k = 0; k < AIOT_FRAME_PRESSURE_CH; k++) {
            s->p_kpa[k] = p[k];
        }

        /* IMU */
        MPU6050_AIoT_data_t d;
        if (s_imu_ok && MPU6050_AIoT_read(s_imu, &d) == ESP_OK) {
            s->acc[0] = d.ax; s->acc[1] = d.ay; s->acc[2] = d.az;
            s->gyr[0] = d.gx; s->gyr[1] = d.gy; s->gyr[2] = d.gz;
            temp_c = d.temp_c;
        } else {
            for (int k = 0; k < 3; k++) {
                s->acc[k] = NAN;
                s->gyr[k] = NAN;
            }
            if (s_imu_ok) {
                imu_err++;
            }
#if AIOT_IMU_ENABLED
            status |= AIOT_STATUS_IMU_FAULT;
#endif
        }
        samples++;
        idx++;

        if (idx < AIOT_FRAME_SAMPLES) {
            continue;
        }

        /* ---- Frame complete ---- */
        if (s_flow_ok) {
            if ((t - t_flow) >= (int64_t)AIOT_FLOW_UPDATE_MS * 1000) {
                t_flow = t;
                if (PCNT_AIoT_get_flow(s_flow, &flow_lps) != ESP_OK) flow_lps = NAN;
                if (PCNT_AIoT_get_volume(s_flow, &volume_l) != ESP_OK) volume_l = NAN;
            }
        } else {
#if AIOT_FLOW_ENABLED
            status |= AIOT_STATUS_FLOW_FAULT;
#endif
        }

        frame.node_id   = AIOT_NODE_ID;
        frame.n_samples = AIOT_FRAME_SAMPLES;
        frame.seq       = seq++;
        frame.period_us = SAMPLE_PERIOD_US;
        frame.status    = (uint16_t)(status | carry_status);
        frame.flow_lps  = flow_lps;
        frame.volume_l  = volume_l;
        frame.temp_c    = temp_c;
        AIoT_frame_seal(&frame);

        carry_status = 0;
        if (xQueueSend(s_tx_queue, &frame, 0) != pdTRUE) {
            dropped++;
            carry_status = AIOT_STATUS_TX_DROPPED;      /* reported in the next frame */
        }
        frames++;

        /* Publish statistics (short critical section, 100 times per second) */
        taskENTER_CRITICAL(&s_stats_mux);
        s_stats.samples        = samples;
        s_stats.frames_built   = frames;
        s_stats.frames_dropped = dropped;
        s_stats.missed_ticks   = missed;
        s_stats.imu_errors     = imu_err;
        s_stats.last_status    = frame.status;
        s_stats.p_kpa[0]       = s->p_kpa[0];
        s_stats.p_kpa[1]       = s->p_kpa[1];
        for (int k = 0; k < 3; k++) {
            s_stats.acc[k] = s->acc[k];
            s_stats.gyr[k] = s->gyr[k];
        }
        s_stats.temp_c   = temp_c;
        s_stats.flow_lps = flow_lps;
        s_stats.volume_l = volume_l;
        taskEXIT_CRITICAL(&s_stats_mux);

        idx    = 0;
        status = 0;
    }
}

static void tx_task(void *arg)
{
    (void)arg;
    AIoT_frame_t frame;
    uint32_t sent = 0, failed = 0;
    const uint8_t *dest = s_unicast ? s_peer : NULL;    /* NULL = broadcast */

    for (;;) {
        if (xQueueReceive(s_tx_queue, &frame, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (ESPNOW_AIoT_send(s_link, dest, &frame, sizeof(frame)) == ESP_OK) {
            sent++;
        } else {
            failed++;
        }
        taskENTER_CRITICAL(&s_stats_mux);
        s_stats.frames_sent   = sent;
        s_stats.frames_failed = failed;
        taskEXIT_CRITICAL(&s_stats_mux);
    }
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

esp_err_t Adquisicion_AIoT_Init(void)
{
    if (s_acq_task != NULL) {
        return ESP_OK;                                  /* already running */
    }
    esp_err_t err;

    /* 1. I2C bus (IMU and/or ADS1115) */
#if AIOT_NEEDS_I2C
    s_bus = I2C_AIoT_bus_create(AIOT_I2C_PORT, AIOT_I2C_SDA_PIN, AIOT_I2C_SCL_PIN, AIOT_I2C_INTERNAL_PULLUP);
    if (s_bus != NULL && I2C_AIoT_bus_init(s_bus) == ESP_OK) {
        (void)I2C_AIoT_scan(s_bus, NULL, 0);            /* logs every device found */
    } else {
        ESP_LOGE(TAG, "I2C bus failed: IMU / ADS1115 not available");
        I2C_AIoT_bus_destroy(s_bus);
        s_bus = NULL;
    }
#endif

    /* 2. Sensors: a failure is reported, the node keeps running */
    err = pressure_init();
    s_pressure_ok = (err == ESP_OK);
    if (!s_pressure_ok) {
        ESP_LOGE(TAG, "Pressure source failed: %s (values sent as NaN)", esp_err_to_name(err));
    }

#if AIOT_IMU_ENABLED
    err = imu_init();
    s_imu_ok = (err == ESP_OK);
    if (!s_imu_ok) {
        ESP_LOGE(TAG, "IMU failed: %s (values sent as NaN)", esp_err_to_name(err));
    }
#endif

#if AIOT_FLOW_ENABLED
    err = flow_init();
    s_flow_ok = (err == ESP_OK);
    if (!s_flow_ok) {
        ESP_LOGE(TAG, "Flow meter failed: %s (values sent as NaN)", esp_err_to_name(err));
    }
#endif

    /* 3. ESP-NOW (starts WiFi in station mode on AIOT_ESPNOW_CHANNEL) */
    s_link = ESPNOW_AIoT_create(AIOT_ESPNOW_CHANNEL, 4);
    if (s_link == NULL) {
        return ESP_ERR_NO_MEM;
    }
    err = ESPNOW_AIoT_init(s_link);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW failed: %s", esp_err_to_name(err));
        ESPNOW_AIoT_destroy(s_link);
        s_link = NULL;
        return err;
    }
    (void)esp_wifi_set_ps(WIFI_PS_NONE);                /* lowest latency, radio always on */

    static const uint8_t k_broadcast[ESPNOW_AIOT_MAC_LEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    s_unicast = (memcmp(s_peer, k_broadcast, ESPNOW_AIOT_MAC_LEN) != 0);
    if (s_unicast) {
        err = ESPNOW_AIoT_add_peer(s_link, s_peer);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Peer " MACSTR " not added (%s): using broadcast",
                     MAC2STR(s_peer), esp_err_to_name(err));
            s_unicast = false;
        }
    }
    ESP_LOGI(TAG, "Destination: %s " MACSTR ", channel %u",
             s_unicast ? "unicast" : "broadcast", MAC2STR(s_unicast ? s_peer : k_broadcast),
             (unsigned)ESPNOW_AIoT_get_channel(s_link));

    /* 4. Queue and tasks */
    s_tx_queue = xQueueCreate(AIOT_TX_QUEUE_LEN, sizeof(AIoT_frame_t));
    if (s_tx_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    taskENTER_CRITICAL(&s_stats_mux);
    s_stats.pressure_ok = s_pressure_ok;
    s_stats.imu_ok      = s_imu_ok;
    s_stats.flow_ok     = s_flow_ok;
    for (int k = 0; k < 2; k++) s_stats.p_kpa[k] = NAN;
    for (int k = 0; k < 3; k++) { s_stats.acc[k] = NAN; s_stats.gyr[k] = NAN; }
    s_stats.temp_c = s_stats.flow_lps = s_stats.volume_l = NAN;
    taskEXIT_CRITICAL(&s_stats_mux);

    if (xTaskCreatePinnedToCore(tx_task, "aiot_tx", AIOT_TX_TASK_STACK, NULL,
                                AIOT_TX_TASK_PRIORITY, NULL, AIOT_TX_TASK_CORE) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreatePinnedToCore(acq_task, "aiot_acq", AIOT_ACQ_TASK_STACK, NULL,
                                AIOT_ACQ_TASK_PRIORITY, &s_acq_task, AIOT_ACQ_TASK_CORE) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    /* 5. Sampling clock */
    const esp_timer_create_args_t targs = {
        .callback              = tick_cb,
        .arg                   = NULL,
        .dispatch_method       = ESP_TIMER_TASK,
        .name                  = "aiot_tick",
        .skip_unhandled_events = true,
    };
    err = esp_timer_create(&targs, &s_tick_timer);
    if (err == ESP_OK) err = esp_timer_start_periodic(s_tick_timer, SAMPLE_PERIOD_US);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Sampling timer failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Running -> %d Hz, %d samples per frame (%u bytes), %d frames/s, node %d",
             AIOT_SAMPLE_RATE_HZ, AIOT_FRAME_SAMPLES, (unsigned)sizeof(AIoT_frame_t),
             AIOT_SAMPLE_RATE_HZ / AIOT_FRAME_SAMPLES, AIOT_NODE_ID);
    return ESP_OK;
}

void Adquisicion_AIoT_get_stats(Adquisicion_AIoT_stats_t *out)
{
    if (out == NULL) {
        return;
    }
    taskENTER_CRITICAL(&s_stats_mux);
    *out = s_stats;
    taskEXIT_CRITICAL(&s_stats_mux);
#if AIOT_PRESSURE_SOURCE == AIOT_PRESSURE_SOURCE_ADC
    out->adc_overruns = (s_adc != NULL) ? ADC_AIoT_get_overruns(s_adc) : 0;
#endif
}
