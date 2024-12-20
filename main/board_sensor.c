/*
 * @Author      : kevin.z.y <kevin.cn.zhengyang@gmail.com>
 * @Date        : 2024-11-29 11:58:35
 * @LastEditors : kevin.z.y <kevin.cn.zhengyang@gmail.com>
 * @LastEditTime: 2024-11-29 15:24:03
 * @FilePath    : /shellhome-nightlamp/main/board_sensor.c
 * @Description :
 * Copyright (c) 2024 by Zheng, Yang, All Rights Reserved.
 */

#include <stdlib.h>
#include <string.h>
#include <sys/cdefs.h>

#include "esp_err.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "esp_idf_version.h"

#include "board_sensor.h"
#include "iot_button.h"

extern EventGroupHandle_t g_event_group;

static const char *TAG = "SENSOR";


static uint8_t btn1_pressed = 0;
static uint8_t btn2_pressed = 1;

/*  vibration */
typedef void (*vibration_isr_t)(void *);

static QueueHandle_t g_gpio_evt_queue  = NULL;
static vibration_isr_t g_vibration_fn = NULL;
static void *g_vibration_fn_arg       = NULL;
static int32_t g_is_enable_vibration = 1;

/* battery */
#ifdef CONFIG_BATTERY_IN_USE
typedef enum {
    CHRG_STATE_UNKNOW   = 0x00,
    CHRG_STATE_CHARGING,
    CHRG_STATE_FULL,
} chrg_state_t;

static int32_t g_bat_chrg_num = 0;
static int32_t g_bat_stby_num = 0;
static int32_t g_vol_bat = 0;

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)

#if CONFIG_ESP32_MOONLIGHT_BOARD
    #define BATTERY_ADC_CHAN          0
#elif CONFIG_ESP32_S3_MOONLIGHT_BOARD
    #define BATTERY_ADC_CHAN          7
#endif

adc_oneshot_unit_handle_t battery_adc_handle = NULL;
adc_cali_handle_t battery_adc_cali_handle = NULL;
bool do_calibration = 0;

static bool example_adc_calibration_init(adc_unit_t unit, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Curve Fitting");
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Line Fitting");
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

    *out_handle = handle;
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Calibration Success");
    } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
        ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
    } else {
        ESP_LOGE(TAG, "Invalid arg or no memory");
    }

    return calibrated;
}

#else
#define DEFAULT_VREF    1100        /**< Use adc2_vref_to_gpio() to obtain a better estimate */
#define NO_OF_SAMPLES   16          /**< Multisampling */
static esp_adc_cal_characteristics_t *g_adc_chars;
static int32_t g_adc_ch_bat = 0;

static void adc_check_efuse()
{
    /**< Check TP is burned into eFuse */
    if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_TP) == ESP_OK) {
        ESP_LOGI(TAG, "ADC eFuse Two Point: Supported");
    } else {
        ESP_LOGI(TAG, "ADC eFuse Two Point: NOT supported");
    }

    /**< Check Vref is burned into eFuse */
    if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_VREF) == ESP_OK) {
        ESP_LOGI(TAG, "ADC eFuse Vref: Supported");
    } else {
        ESP_LOGI(TAG, "ADC eFuse Vref: NOT supported");
    }
}

static void print_char_val_type(esp_adc_cal_value_t val_type)
{
    if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
        ESP_LOGI(TAG, "ADC Characterized using Two Point Value\n");
    } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
        ESP_LOGI(TAG, "ADC Characterized using eFuse Vref\n");
    } else {
        ESP_LOGI(TAG, "ADC Characterized using Default Vref\n");
    }
}

#endif

static void adc_get_voltage(int32_t *out_voltage)
{
    int voltage;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    static int adc_raw;
    ESP_ERROR_CHECK(adc_oneshot_read(battery_adc_handle, CONFIG_BAT_ADC_CHANNEL, &adc_raw));
    if (do_calibration) {
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(battery_adc_cali_handle, adc_raw, &voltage));
        *out_voltage = voltage;
    }
#else
    static uint32_t sample_index = 0;
    static uint16_t filter_buf[NO_OF_SAMPLES] = {0};

    uint32_t adc_reading = adc1_get_raw(g_adc_ch_bat);
    filter_buf[sample_index++] = adc_reading;

    if (NO_OF_SAMPLES == sample_index) {
        sample_index = 0;
    }

    uint32_t sum = 0;

    for (int i = 0; i < NO_OF_SAMPLES; i++) {
        sum += filter_buf[i];
    }

    sum /= NO_OF_SAMPLES;
    /**< Convert adc_reading to voltage in mV */
    voltage = esp_adc_cal_raw_to_voltage(sum, g_adc_chars);
    *out_voltage = voltage;
#endif
}

static void adc_proid_sample(TimerHandle_t xTimer)
{
    adc_get_voltage(&g_vol_bat);
    /**< The resistance on the hardware has decreased twice */
    g_vol_bat *= 2;
}

esp_err_t sensor_battery_get_info(int32_t *voltage, uint8_t *chrg_state)
{
    if (NULL != voltage) {
        *voltage = g_vol_bat;
    }

    if (NULL != chrg_state) {
        /**< Low level active */
        uint8_t chrg = !gpio_get_level(g_bat_chrg_num);
        uint8_t stby = !gpio_get_level(g_bat_stby_num);
        *chrg_state = (chrg << 1) | stby;
    }

    return ESP_OK;
}

esp_err_t sensor_battery_get_info_simple(int32_t *level, chrg_state_t *state)
{
    int32_t vol;
    uint8_t chrg_state;

    sensor_battery_get_info(&vol, &chrg_state);

    if (NULL != level) {
        vol = vol > 4200 ? 4200 : vol;
        vol = vol < 3600 ? 3600 : vol;
        vol -= 3600;
        *level = vol / 6;
    }

    if (NULL != state) {
        if (0x02 == chrg_state) {
            *state = CHRG_STATE_CHARGING;
        } else if (0x01 == chrg_state) {
            *state = CHRG_STATE_FULL;
        } else {
            *state = CHRG_STATE_UNKNOW;
        }
    }

    return ESP_OK;
}


static void sensor_battery_task(void *arg)
{
    static TimerHandle_t tmr;
    tmr = xTimerCreate("adc measure",
                       10 / portTICK_PERIOD_MS,
                       pdTRUE,
                       NULL,
                       adc_proid_sample);

    if (tmr) {
        xTimerStart(tmr, 0);
    }

    while (1) {
        ESP_LOGI(TAG, "battery voltage: %"PRId32"mv", g_vol_bat);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

esp_err_t sensor_battery_init(int32_t adc_channel, int32_t chrg_num, int32_t stby_num)
{
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    adc_oneshot_unit_init_cfg_t battery_adc_init_config = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&battery_adc_init_config, &battery_adc_handle));

    //-------------ADC1 Config---------------//
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = (ADC_ATTEN_DB_6 + 1),
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(battery_adc_handle, CONFIG_BAT_ADC_CHANNEL, &config));

    //-------------ADC1 Calibration Init---------------//
    do_calibration = example_adc_calibration_init(ADC_UNIT_1, (ADC_ATTEN_DB_6 + 1), &battery_adc_cali_handle);
#else
    g_adc_ch_bat = adc_channel;
    g_bat_chrg_num = chrg_num;
    g_bat_stby_num = stby_num;
    /**< Check if Two Point or Vref are burned into eFuse */
    adc_check_efuse();

    /**< Configure ADC */
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(g_adc_ch_bat, ADC_ATTEN_DB_12);

    /**< Characterize ADC */
    g_adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12, DEFAULT_VREF, g_adc_chars);
    print_char_val_type(val_type);

#endif

    /**< Configure GPIO for read battery voltage */
    if (chrg_num != GPIO_NUM_NC) {
        gpio_config_t io_conf = {0};
        io_conf.intr_type = GPIO_INTR_DISABLE;
        io_conf.pin_bit_mask = (((uint64_t) 1) << chrg_num);
        io_conf.mode = GPIO_MODE_INPUT;
        gpio_config(&io_conf);
    }

    if (stby_num != GPIO_NUM_NC) {
        gpio_config_t io_conf = {0};
        io_conf.intr_type = GPIO_INTR_DISABLE;
        io_conf.pin_bit_mask = (((uint64_t) 1) << stby_num);
        io_conf.mode = GPIO_MODE_INPUT;
        gpio_config(&io_conf);
    }

    xTaskCreate(sensor_battery_task, "battery", 1024 * 2, NULL, 3, NULL);
    return ESP_OK;
}

#endif /* CONFIG_BATTERY_IN_USE */

static void sensor_vibration_task(void *arg)
{
    int32_t gpio_num = (int32_t)arg;

    while (1) {
        int32_t io_num;
        uint8_t level;

        if (xQueueReceive(g_gpio_evt_queue, &io_num, portMAX_DELAY)) {
            level = gpio_get_level(io_num);
            ESP_LOGI(TAG, "GPIO[%"PRId32"] intr, val: %d\n", io_num, level);

            if (io_num == gpio_num) {
                /**
                 * remove isr handler for gpio number.
                 * Discard interrupts generated during the delay period
                 */

                if (NULL != g_vibration_fn) {
                    g_vibration_fn(g_vibration_fn_arg);
                }

                vTaskDelay(pdMS_TO_TICKS(250));
                g_is_enable_vibration = 1;
            }
        }
    }
}

static void periodic_timer_callback(void *arg)
{
    static int8_t last_level = 1;
    int8_t level;
    int32_t gpio_num = (int32_t)arg;

    level = gpio_get_level(gpio_num);

    /**< Find a falling edge */
    if ((1 == g_is_enable_vibration) && (0 == level) && (1 == last_level)) {
        g_is_enable_vibration = 0;
        xQueueOverwrite(g_gpio_evt_queue, &gpio_num);
    }

    last_level = level;
}

esp_err_t sensor_vibration_triggered_register(vibration_isr_t fn, void *arg)
{
    g_vibration_fn = fn;
    g_vibration_fn_arg = arg;
    return ESP_OK;
}

esp_err_t sensor_vibration_init(int32_t gpio_num)
{
    gpio_config_t io_conf = {0};

    /**< interrupt of rising edge */
    io_conf.intr_type = GPIO_INTR_DISABLE;
    /**< bit mask of the pins */
    io_conf.pin_bit_mask = (((uint64_t) 1) << gpio_num);
    /**< set as input mode */
    io_conf.mode = GPIO_MODE_INPUT;
    /**< enable pull-up mode */
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);

    /**< create a queue to handle gpio event from isr */
    g_gpio_evt_queue = xQueueCreate(1, sizeof(uint32_t));

    if (g_gpio_evt_queue == NULL) {
        return ESP_FAIL;
    }

    /**< install gpio isr service */
    esp_timer_create_args_t periodic_timer_args = {
        .callback = &periodic_timer_callback,
        .arg = (void *)gpio_num,
        /* name is optional, but may help identify the timer when debugging */
        .name = "periodic"
    };

    esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    /* Start the timers */
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 5000));

    xTaskCreate(sensor_vibration_task, "vibration", 1024 * 2, (void *)gpio_num, 3, NULL);

    return ESP_OK;
}


static void vibration_handle(void *arg)
{
    xEventGroupSetBits(g_event_group, EVENT_MODE_BITS);
}

static void button_press_down_cb(void *arg, void *data)
{
    uint8_t btn = *(uint8_t *)data;
    if (btn1_pressed == btn) {
        xEventGroupSetBits(g_event_group, EVENT_COLOR_BITS);
    } else {
        xEventGroupSetBits(g_event_group, EVENT_TIMER_BITS);
    }
}

// innit sensor
esp_err_t sensor_init(void) {

    ESP_LOGI(TAG, "init buttons");
    button_config_t cfg1 = {
        .type = BUTTON_TYPE_GPIO,
        .gpio_button_config = {
            .gpio_num = CONFIG_GPIO_BTN_1,
            .active_level = 0,
        },
    };
    button_handle_t btn1 = iot_button_create(&cfg1);
    ESP_RETURN_ON_FALSE(btn1 != NULL, ESP_FAIL, TAG, "Button1 initialization failed.");

    esp_err_t err = iot_button_register_cb(btn1, BUTTON_PRESS_DOWN,
                                button_press_down_cb, &btn1_pressed);
    ESP_ERROR_CHECK(err);

    button_config_t cfg2 = {
        .type = BUTTON_TYPE_GPIO,
        .gpio_button_config = {
            .gpio_num = CONFIG_GPIO_BTN_2,
            .active_level = 0,
        },
    };
    button_handle_t btn2 = iot_button_create(&cfg2);
    ESP_RETURN_ON_FALSE(btn2 != NULL, ESP_FAIL, TAG, "Button2 initialization failed.");

    err = iot_button_register_cb(btn2, BUTTON_PRESS_DOWN,
                                button_press_down_cb, &btn2_pressed);
    ESP_ERROR_CHECK(err);

#ifdef CONFIG_BATTERY_IN_USE
    ESP_LOGI(TAG, "init sensor");
    esp_err_t ret = sensor_battery_init(CONFIG_BAT_ADC_CHANNEL, CONFIG_GPIO_BAT_CHRG, CONFIG_GPIO_BAT_STBY);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Initialize adc for measure battery voltage failed.");
    }
#endif /* CONFIG_BATTERY_IN_USE */

    err = sensor_vibration_init(CONFIG_GPIO_VIBRATION);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Initialize vabration sensors failed.");
    }
    err = sensor_vibration_triggered_register(vibration_handle, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Register vabration sensor triggered handler failed.");
    }
    return err;
}
