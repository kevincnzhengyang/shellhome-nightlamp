/*
 * @Author      : kevin.z.y <kevin.cn.zhengyang@gmail.com>
 * @Date        : 2024-11-29 10:46:27
 * @LastEditors : kevin.z.y <kevin.cn.zhengyang@gmail.com>
 * @LastEditTime: 2024-11-30 14:37:14
 * @FilePath    : /shellhome-nightlamp/main/board_leds.c
 * @Description :
 * Copyright (c) 2024 by Zheng, Yang, All Rights Reserved.
 */

#include <stdlib.h>
#include <string.h>
#include <sys/cdefs.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "driver/ledc.h"

#include "led_strip.h"
#include "board_leds.h"
#include "board_sensor.h"

extern EventGroupHandle_t g_event_group;


static const char *TAG = "LEDS";

/**
* @brief LED rgb Type
*
*/
typedef struct led_rgb_s led_rgb_t;

typedef enum {
    LAMP_MODE_MARQUEE,
    LAMP_MODE_BREATH,
    LAMP_MODE_STACK,
    LAMP_MODE_FIXED,
    LAMP_MODE_BUTT
} LAMP_MODE_ENUM;

#define LAMP_MODE_MASK ((1<<2)-1)

#define SAVE_TIMER_MS (3*60*1000)
#define OFF_TIMER_MS (30*60*1000)

/**
* @brief Declare of LED rgb Type
*
*/
struct led_rgb_s {
    /**
    * @brief Set RGB for LED
    *
    * @param led_rgb: Pointer of led_rgb struct
    * @param red: red part of color
    * @param green: green part of color
    * @param blue: blue part of color
    *
    * @return
    *      - ESP_OK: Set RGB successfully
    *      - ESP_ERR_INVALID_ARG: Set RGB failed because of invalid parameters
    *      - ESP_FAIL: Set RGB failed because other error occurred
    */
    esp_err_t (*set_rgb)(led_rgb_t *led_rgb, uint32_t red, uint32_t green, uint32_t blue);

    /**
    * @brief Set HSV for LED
    *
    * @param led_rgb: Pointer of led_rgb struct
    * @param hue: hue part of color
    * @param saturation: saturation part of color
    * @param value: value part of color
    *
    * @return
    *      - ESP_OK: Set HSV successfully
    *      - ESP_ERR_INVALID_ARG: Set HSV failed because of invalid parameters
    *      - ESP_FAIL: Set HSV failed because other error occurred
    */
    esp_err_t (*set_hsv)(led_rgb_t *led_rgb, uint32_t hue, uint32_t saturation, uint32_t value);

    /**
    * @brief Get RGB for LED
    *
    * @param led_rgb: Pointer of led_rgb struct
    * @param red: red part of color
    * @param green: green part of color
    * @param blue: blue part of color
    *
    * @return
    *      - ESP_OK: Get RGB successfully
    */
    esp_err_t (*get_rgb)(led_rgb_t *led_rgb, uint8_t *red, uint8_t *green, uint8_t *blue);

    /**
    * @brief Get HSV for LED
    *
    * @param led_rgb: Pointer of led_rgb struct
    * @param hue: hue part of color
    * @param saturation: saturation part of color
    * @param value: value part of color
    *
    * @return
    *      - ESP_OK: Get HSV successfully
    */
    esp_err_t (*get_hsv)(led_rgb_t *led_rgb, uint32_t *hue, uint32_t *saturation, uint32_t *value);

    /**
    * @brief Clear LED rgb (turn off LED)
    *
    * @param led_rgb: Pointer of led_rgb struct
    *
    * @return
    *      - ESP_OK: Clear LEDs successfully
    *      - ESP_ERR_TIMEOUT: Clear LEDs failed because of timeout
    *      - ESP_FAIL: Clear LEDs failed because some other error occurred
    */
    esp_err_t (*clear)(led_rgb_t *led_rgb);

    /**
    * @brief Free LED rgb resources
    *
    * @param led_rgb: Pointer of led_rgb struct
    *
    * @return
    *      - ESP_OK: Free resources successfully
    *      - ESP_FAIL: Free resources failed because error occurred
    */
    esp_err_t (*del)(led_rgb_t *led_rgb);
};

/**
* @brief LED rgb Configuration Type
*
*/
typedef struct {
    ledc_mode_t speed_mode;         /*!< LEDC speed speed_mode, high-speed mode or low-speed mode */
    ledc_timer_t timer_sel;         /*!< Select the timer source of channel (0 - 3) */

    int32_t red_gpio_num;      /*!< Red LED pwm gpio number */
    int32_t green_gpio_num;    /*!< Green LED pwm gpio number */
    int32_t blue_gpio_num;     /*!< Blue LED pwm gpio number */
    ledc_channel_t red_ledc_ch;      /*!< Red LED LEDC channel */
    ledc_channel_t green_ledc_ch;    /*!< Green LED LEDC channel */
    ledc_channel_t blue_ledc_ch;     /*!< Blue LED LEDC channel */

    uint32_t freq;
    uint32_t resolution;
} led_rgb_config_t;


#define LED_RGB_DEFAULT_CONFIG(red_gpio, green_gpio, blue_gpio) {  \
    .red_gpio_num   = (red_gpio),         \
    .green_gpio_num = (green_gpio),       \
    .blue_gpio_num  = (blue_gpio),        \
    .red_ledc_ch    = LEDC_CHANNEL_0,     \
    .green_ledc_ch  = LEDC_CHANNEL_1,     \
    .blue_ledc_ch   = LEDC_CHANNEL_2,     \
    .speed_mode = LEDC_LOW_SPEED_MODE,    \
    .timer_sel  = LEDC_TIMER_0,           \
    .freq       = 20000,                  \
    .resolution = LEDC_TIMER_8_BIT,       \
}

#define LED_RGB_CHECK(a, str, goto_tag, ret_value, ...)                             \
    do                                                                            \
    {                                                                             \
        if (!(a))                                                                 \
        {                                                                         \
            ESP_LOGE(TAG, "%s(%d): " str, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
            ret = ret_value;                                                      \
            goto goto_tag;                                                        \
        }                                                                         \
    } while (0)

typedef struct {
    led_rgb_t parent;
    ledc_mode_t speed_mode[3];
    ledc_channel_t channel[3];
    uint32_t rgb[3];
} led_pwm_t;

typedef struct {
    led_rgb_t *              top_led;
    led_strip_handle_t     led_strip;
    nvs_handle_t          nvs_handle;
    esp_timer_handle_t    save_timer;
    esp_timer_handle_t     off_timer;
    uint32_t                   index;
    uint32_t                   count;
    LAMP_MODE_ENUM         lamp_mode;
    uint8_t                increased;
    uint16_t                     hue;
    uint8_t               saturation;
    uint8_t                    value;
} lamp_light_t;

static lamp_light_t g_lamp;

/**
 * @brief sin() function from 0 to 2π, in total 255 values rounded up，maximum 255，minimum 0
 *
 */
/*
static uint8_t  const SinValue[256]={	128,   131,   134,   137,   140,   143,   147,   150,   153,   156,
                                159,   162,   165,   168,   171,   174,   177,   180,   182,   185,
                                188,   191,   194,   196,   199,   201,   204,   206,   209,   211,
                                214,   216,   218,   220,   223,   225,   227,   229,   230,   232,
                                234,   236,   237,   239,   240,   242,   243,   245,   246,   247,
                                248,   249,   250,   251,   252,   252,   253,   253,   254,   254,
                                255,   255,   255,   255,   255,   255,   255,   255,   255,   254,
                                254,   253,   253,   252,   251,   250,   249,   249,   247,   246,
                                245,   244,   243,   241,   240,   238,   237,   235,   233,   231,
                                229,   228,   226,   224,   221,   219,   217,   215,   212,   210,
                                208,   205,   203,   200,   198,   195,   192,   189,   187,   184,
                                181,   178,   175,   172,   169,   166,   163,   160,   157,   154,
                                151,   148,   145,   142,   139,   136,   132,   129,   126,   123,
                                120,   117,   114,   111,   107,   104,   101,    98,    95,    92,
                                89,    86,    83,    80,    77,    74,    72,    69,    66,    63,
                                61,    58,    55,    53,    50,    48,    45,    43,    41,    38,
                                36,    34,    32,    30,    28,    26,    24,    22,    21,    19,
                                17,    16,    14,    13,    12,    10,     9,     8,     7,     6,
                                5,     4,     4,     3,     2,     2,     1,     1,     1,     0,
                                0,     0,     0,     0,     1,     1,     1,     2,     2,     3,
                                3,     4,     5,     6,     6,     7,     9,    10,    11,    12,
                                14,    15,    17,    18,    20,    21,    23,    25,    27,    29,
                                31,    33,    35,    37,    40,    42,    44,    47,    49,    52,
                                54,    57,    59,    62,    65,    67,    70,    73,    76,    79,
                                82,    85,    88,    91,    94,    97,   100,   103,   106,   109,
                                112,   115,   118,   121,   125,   128
};
*/

static const uint8_t LEDGammaTable[] = {
    0,   0,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
    2,   2,   2,   2,   2,   2,   2,   3,   3,   3,   3,   3,   4,   4,   4,   4,
    5,   5,   5,   5,   6,   6,   6,   6,   7,   7,   7,   8,   8,   8,   9,   9,
    10,  10,  10,  11,  11,  12,  12,  12,  13,  13,  14,  14,  15,  15,  16,  16,
    17,  17,  18,  18,  19,  19,  20,  20,  21,  21,  22,  23,  23,  24,  24,  25,
    26,  26,  27,  28,  28,  29,  30,  30,  31,  32,  32,  33,  34,  34,  35,  36,
    37,  37,  38,  39,  40,  41,  41,  42,  43,  44,  45,  45,  46,  47,  48,  49,
    50,  51,  51,  52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  62,  63,  64,
    65,  66,  67,  68,  69,  70,  71,  72,  73,  74,  75,  76,  77,  78,  80,  81,
    82,  83,  84,  85,  86,  88,  89,  90,  91,  92,  94,  95,  96,  97,  98, 100,
    101, 102, 103, 105, 106, 107, 109, 110, 111, 113, 114, 115, 117, 118, 119, 121,
    122, 123, 125, 126, 128, 129, 130, 132, 133, 135, 136, 138, 139, 141, 142, 144,
    145, 147, 148, 150, 151, 153, 154, 156, 157, 159, 161, 162, 164, 165, 167, 169,
    170, 172, 173, 175, 177, 178, 180, 182, 183, 185, 187, 189, 190, 192, 194, 196,
    197, 199, 201, 203, 204, 206, 208, 210, 212, 213, 215, 217, 219, 221, 223, 225,
    226, 228, 230, 232, 234, 236, 238, 240, 242, 244, 246, 248, 250, 252, 254, 255
};


/**
 * @brief Simple helper function, converting HSV color space to RGB color space
 *
 * Wiki: https://en.wikipedia.org/wiki/HSL_and_HSV
 *
 */
static void led_hsv2rgb(uint32_t h, uint32_t s, uint32_t v, uint32_t *r, uint32_t *g, uint32_t *b)
{
    h %= 360; /**< h -> [0,360] */
    uint32_t rgb_max = v * 2.55f;
    uint32_t rgb_min = rgb_max * (100 - s) / 100.0f;

    uint32_t i = h / 60;
    uint32_t diff = h % 60;

    /**< RGB adjustment amount by hue */
    uint32_t rgb_adj = (rgb_max - rgb_min) * diff / 60;

    switch (i) {
        case 0:
            *r = rgb_max;
            *g = rgb_min + rgb_adj;
            *b = rgb_min;
            break;

        case 1:
            *r = rgb_max - rgb_adj;
            *g = rgb_max;
            *b = rgb_min;
            break;

        case 2:
            *r = rgb_min;
            *g = rgb_max;
            *b = rgb_min + rgb_adj;
            break;

        case 3:
            *r = rgb_min;
            *g = rgb_max - rgb_adj;
            *b = rgb_max;
            break;

        case 4:
            *r = rgb_min + rgb_adj;
            *g = rgb_min;
            *b = rgb_max;
            break;

        default:
            *r = rgb_max;
            *g = rgb_min;
            *b = rgb_max - rgb_adj;
            break;
    }
}

static esp_err_t led_set_rgb(led_rgb_t *led_rgb, uint32_t red,
                             uint32_t green, uint32_t blue) {
    led_pwm_t *led_pwm = __containerof(led_rgb, led_pwm_t, parent);

    led_pwm->rgb[0] = red;
    led_pwm->rgb[1] = green;
    led_pwm->rgb[2] = blue;

    uint8_t rgb[3];
    rgb[0] = LEDGammaTable[(uint8_t)red];
    rgb[1] = LEDGammaTable[(uint8_t)green];
    rgb[2] = LEDGammaTable[(uint8_t)blue];

    for (size_t i = 0; i < 3; i++) {
        ledc_set_duty(led_pwm->speed_mode[i], led_pwm->channel[i], rgb[i]);
        ledc_update_duty(led_pwm->speed_mode[i], led_pwm->channel[i]);
    }

    return ESP_OK;

}

static esp_err_t led_get_rgb(led_rgb_t *led_rgb, uint8_t *red,
                             uint8_t *green, uint8_t *blue) {
    led_pwm_t *led_pwm = __containerof(led_rgb, led_pwm_t, parent);

    if (NULL != red) {
        *red = led_pwm->rgb[0];
    }

    if (NULL != green) {
        *green = led_pwm->rgb[1];
    }

    if (NULL != blue) {
        *blue = led_pwm->rgb[2];
    }

    return ESP_OK;
}

static esp_err_t led_del(led_rgb_t *led_rgb) {
    led_pwm_t *led_pwm = __containerof(led_rgb, led_pwm_t, parent);
    free(led_pwm);
    return ESP_OK;
}

static esp_err_t led_clear(led_rgb_t *led_rgb) {
    led_pwm_t *led_pwm = __containerof(led_rgb, led_pwm_t, parent);

    for (size_t i = 0; i < 3; i++) {
        ledc_set_duty(led_pwm->speed_mode[i], led_pwm->channel[i], 0);
        ledc_update_duty(led_pwm->speed_mode[i], led_pwm->channel[i]);
    }

    return ESP_OK;
}

static led_rgb_t *led_rgb_create(const led_rgb_config_t *cfg) {
    led_rgb_t *ret = NULL;
    LED_RGB_CHECK(cfg, "configuration can't be null", err, NULL);

    /*
     * Prepare and set configuration of timers
     * that will be used by LED Controller
     */
    ledc_timer_config_t ledc_timer = {
        .duty_resolution = cfg->resolution,     /**< resolution of PWM duty */
        .freq_hz         = cfg->freq,           /**< frequency of PWM signal */
        .speed_mode      = cfg->speed_mode,     /**< timer mode */
        .timer_num       = cfg->timer_sel,      /**< timer index */
        .clk_cfg         = LEDC_USE_APB_CLK,    /**< Auto select the source clock */
    };
    /**< Set configuration of timer for low speed channels */
    LED_RGB_CHECK(ledc_timer_config(&ledc_timer) == ESP_OK,
                  "ledc timer config failed", err, NULL);

    /*
     * Prepare and set configuration of ledc channels
     */
    ledc_channel_config_t ledc_channel = {0};
    ledc_channel.channel    = cfg->red_ledc_ch;
    ledc_channel.duty       = 0;
    ledc_channel.gpio_num   = cfg->red_gpio_num;
    ledc_channel.speed_mode = cfg->speed_mode;
    ledc_channel.hpoint     = 0;
    ledc_channel.timer_sel  = cfg->timer_sel;
    LED_RGB_CHECK(ledc_channel_config(&ledc_channel) == ESP_OK,
                  "ledc channel config failed", err, NULL);

    ledc_channel.channel    = cfg->green_ledc_ch;
    ledc_channel.gpio_num   = cfg->green_gpio_num;
    LED_RGB_CHECK(ledc_channel_config(&ledc_channel) == ESP_OK,
                  "ledc channel config failed", err, NULL);

    ledc_channel.channel    = cfg->blue_ledc_ch;
    ledc_channel.gpio_num   = cfg->blue_gpio_num;
    LED_RGB_CHECK(ledc_channel_config(&ledc_channel) == ESP_OK,
                  "ledc channel config failed", err, NULL);

    /**< alloc memory for led */
    led_pwm_t *led_pwm = calloc(1, sizeof(led_pwm_t));
    LED_RGB_CHECK(led_pwm, "request memory for led failed", err, NULL);

    for (size_t i = 0; i < 3; i++) {
        led_pwm->speed_mode[i] = ledc_channel.speed_mode;
    }

    led_pwm->channel[0] = cfg->red_ledc_ch;
    led_pwm->channel[1] = cfg->green_ledc_ch;
    led_pwm->channel[2] = cfg->blue_ledc_ch;

    led_pwm->parent.set_rgb = led_set_rgb;
    led_pwm->parent.set_hsv = NULL;
    led_pwm->parent.get_rgb = led_get_rgb;
    led_pwm->parent.get_hsv = NULL;
    led_pwm->parent.clear = led_clear;
    led_pwm->parent.del = led_del;

    return &led_pwm->parent;
err:
    return ret;
}


/**
 * @brief Set RGB for all
 *
 * @return
 *      - ESP_OK: Set RGB for a specific pixel successfully
 *      - ESP_ERR_INVALID_ARG: Set RGB for a specific pixel failed because of invalid parameters
 *      - ESP_FAIL: Set RGB for a specific pixel failed because other error occurred
 */
static esp_err_t all_set(uint8_t red, uint8_t green, uint8_t blue) {
    ESP_ERROR_CHECK(led_set_rgb(g_lamp.top_led, red, green, blue));
    for (int i = 0; i < CONFIG_STRIP_LED_NUM; i++) {
        ESP_ERROR_CHECK(led_strip_set_pixel(g_lamp.led_strip, i,
                                red, green, blue));
    }
    /* Refresh the strip to send data */
    ESP_ERROR_CHECK(led_strip_refresh(g_lamp.led_strip));
    return ESP_OK;
}

/**
 * @description : Clear LED
 */
static esp_err_t all_clear(void) {
    ESP_ERROR_CHECK(led_clear(g_lamp.top_led));
    return led_strip_clear(g_lamp.led_strip);
}

static void random_color(void) {
    /**< Set a random color */
    g_lamp.hue = esp_random() / 11930465;
    g_lamp.saturation = esp_random() / 42949673;
    g_lamp.saturation = g_lamp.saturation < 40 ? 40 : (g_lamp.saturation > 100 ? 100 :g_lamp.saturation);
    g_lamp.value = 100;
}

static esp_err_t save_mod_to_nvs(void) {
    esp_err_t err = nvs_set_u8(g_lamp.nvs_handle, "lamp-mode",
                            (uint8_t)g_lamp.lamp_mode);
    ESP_ERROR_CHECK(err);
    ESP_LOGI(TAG, "Save mode");
    return nvs_commit(g_lamp.nvs_handle);
}

static esp_err_t load_mod_from_nvs(void) {
    esp_err_t err = nvs_get_u8(g_lamp.nvs_handle, "lamp-mode",
                            (uint8_t *)&g_lamp.lamp_mode);
    if (ESP_ERR_NVS_NOT_FOUND == err) {
        // set default and save
        g_lamp.lamp_mode = LAMP_MODE_MARQUEE;
        err = save_mod_to_nvs();
    }
    ESP_LOGI(TAG, "Load mode");
    return err;
}

static esp_err_t save_hvs_to_nvs(void) {
    esp_err_t err = nvs_set_u16(g_lamp.nvs_handle, "lamp-h", g_lamp.hue);
    ESP_ERROR_CHECK(err);
    err = nvs_set_u8(g_lamp.nvs_handle, "lamp-s", g_lamp.saturation);
    ESP_ERROR_CHECK(err);
    err = nvs_set_u8(g_lamp.nvs_handle, "lamp-v", g_lamp.value);
    ESP_ERROR_CHECK(err);
    ESP_LOGI(TAG, "Save mode");
    return nvs_commit(g_lamp.nvs_handle);
}

static esp_err_t load_hvs_from_nvs(void) {
    bool changed = pdFALSE;

    esp_err_t err = nvs_get_u16(g_lamp.nvs_handle, "lamp-h",
                            &g_lamp.hue);
    if (ESP_ERR_NVS_NOT_FOUND == err) {
        // set default and save
        random_color();
        changed = pdTRUE;
        err = ESP_OK;
    }
    ESP_ERROR_CHECK(err);
    err = nvs_get_u8(g_lamp.nvs_handle, "lamp-s",
                            &g_lamp.saturation);
    if (ESP_ERR_NVS_NOT_FOUND == err) {
        // set default and save
        g_lamp.saturation = 100;
        err = ESP_OK;
        changed = pdTRUE;
    }
    ESP_ERROR_CHECK(err);
    err = nvs_get_u8(g_lamp.nvs_handle, "lamp-v",
                            &g_lamp.value);
    if (ESP_ERR_NVS_NOT_FOUND == err) {
        // set default and save
        g_lamp.value = 100;
        err = ESP_OK;
        changed = pdTRUE;
    }

    if (changed) err = save_hvs_to_nvs();   // save change
    ESP_LOGI(TAG, "Load hsv");
    return err;
}

static void save_timer_cb(void *args) {
    save_mod_to_nvs();
    save_hvs_to_nvs();
}

static void reset_save_timer(void) {
    if (NULL == g_lamp.save_timer) {
        // create timer
        esp_timer_create_args_t save_cnf = {
            .arg = NULL,
            .callback = save_timer_cb,
            .dispatch_method = ESP_TIMER_TASK
        };

        esp_timer_create(&save_cnf, &g_lamp.save_timer);
        esp_timer_start_once(g_lamp.save_timer, SAVE_TIMER_MS);
        ESP_LOGI(TAG, "create save timer");
    } else {
        // restart
        esp_timer_restart(g_lamp.save_timer, SAVE_TIMER_MS);
        ESP_LOGI(TAG, "restart save timer");
    }
}

static void off_timer_cb(void *args) {
    all_clear();
}

static void reset_off_timer(void) {
    if (NULL == g_lamp.off_timer) {
        // create timer
        esp_timer_create_args_t off_cnf = {
            .arg = NULL,
            .callback = off_timer_cb,
            .dispatch_method = ESP_TIMER_TASK
        };

        esp_timer_create(&off_cnf, &g_lamp.off_timer);
        esp_timer_start_once(g_lamp.off_timer, OFF_TIMER_MS);
        ESP_LOGI(TAG, "create off timer");
    } else {
        // restart
        esp_timer_restart(g_lamp.off_timer, OFF_TIMER_MS);
        ESP_LOGI(TAG, "restart off timer");
    }
}

static void leds_task(void *pvParameters) {
    ESP_LOGI(TAG, "svc ...");
    while (1) {
        EventBits_t bits = xEventGroupWaitBits(g_event_group,
                                EVENT_MODE_BITS|EVENT_TIMER_BITS|EVENT_COLOR_BITS,
                                pdTRUE, pdFAIL, portMAX_DELAY);
        if (bits & EVENT_MODE_BITS) {
            // change mode
            g_lamp.lamp_mode += 1;
            g_lamp.lamp_mode &= LAMP_MODE_MASK;
            // reset counter, index, increased
            g_lamp.count = CONFIG_STRIP_LED_NUM + 1; // LED at the top
            g_lamp.increased = pdTRUE;
            g_lamp.index = 0;
            // reset save timer
            reset_save_timer();
            ESP_LOGI(TAG, "mode changed to %d", g_lamp.lamp_mode);
        } else if (bits & EVENT_COLOR_BITS) {
            // change color
            if (LAMP_MODE_MARQUEE != g_lamp.lamp_mode) {
                random_color();
                // reset save timer
                reset_save_timer();
                ESP_LOGI(TAG, "next random");
            } else {
                ESP_LOGE(TAG, "can't change color at this mode");
            }
        } else if (bits & EVENT_TIMER_BITS) {
            reset_off_timer();
            ESP_LOGI(TAG, "Timer off reset");
        } else {
            ESP_LOGE(TAG, "Unknown Bit set %d", (int)bits);
        }
        vTaskDelay(1000/portTICK_PERIOD_MS);
    }

    ESP_LOGI(TAG, "task closing");
    nvs_close(g_lamp.nvs_handle);
    vTaskDelete(NULL);
}

//  init leds
esp_err_t leds_init(void) {
    memset(&g_lamp, 0, sizeof(g_lamp));
    g_lamp.lamp_mode = LAMP_MODE_BUTT;

    ESP_LOGI(TAG, "init ...");

    // Open NVS
    esp_err_t err = nvs_open("ShellHome", NVS_READWRITE, &g_lamp.nvs_handle);
    ESP_ERROR_CHECK(err);

    // load Mode
    ESP_LOGI(TAG, "load ...");
    err = load_mod_from_nvs();
    ESP_ERROR_CHECK(err);
    // load hsv
    err = load_hvs_from_nvs();
    ESP_ERROR_CHECK(err);

    ESP_LOGI(TAG, "init top led");
    /**< configure top led driver */
    led_rgb_config_t rgb_config = LED_RGB_DEFAULT_CONFIG(CONFIG_GPIO_R, CONFIG_GPIO_G, CONFIG_GPIO_B);
    g_lamp.top_led = led_rgb_create(&rgb_config);

    if (!g_lamp.top_led) {
        ESP_LOGE(TAG, "install LED driver failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "init led strip");
    // LED strip general initialization
    led_strip_config_t strip_config = {
        .strip_gpio_num = CONFIG_STRIP_GPIO_NUM,       // The GPIO connected to the LED strip's data line
        .max_leds = CONFIG_STRIP_LED_NUM,              // The number of LEDs in the strip,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,       // Pixel format of your LED strip
        // .led_pixel_format = LED_PIXEL_FORMAT_GRB,       // Pixel format of your LED strip
        .led_model = LED_MODEL_WS2812,                  // LED strip model
        .flags.invert_out = false,                      // whether to invert the output signal
    };

    // LED strip backend configuration: RMT
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,                 // different clock source can lead to different power consumption
        .resolution_hz = CONFIG_LED_STRIP_RESOLUTION_HZ, // RMT counter clock frequency
        .flags.with_dma = false,               // DMA feature is available on ESP target like ESP32-S3
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config,
                                             &(g_lamp.led_strip)));
    return ESP_OK;
}


// start leds task
esp_err_t leds_start(void) {
    xTaskCreate(&leds_task, "leds_task", 4 * 1024, NULL, 2, NULL);
    return ESP_OK;
}

// flush leds
void leds_flush(void) {

    uint32_t r, g, b;

    if (LAMP_MODE_FIXED == g_lamp.lamp_mode) {
        led_hsv2rgb((uint32_t)g_lamp.hue, (uint32_t)g_lamp.saturation,
                    (uint32_t)g_lamp.value,
                    &r, &g, &b);
        all_set((uint8_t)r, (uint8_t)g, (uint8_t)b);
        vTaskDelay(pdMS_TO_TICKS(100));
    } else if (LAMP_MODE_BREATH == g_lamp.lamp_mode) {
        led_hsv2rgb((uint32_t)g_lamp.hue, 100,
                    (uint32_t)g_lamp.value,
                    &r, &g, &b);
        all_set((uint8_t)r, (uint8_t)g, (uint8_t)b);
        if (g_lamp.increased) {
            g_lamp.increased = ((g_lamp.value++) >= 99) ? pdFALSE : pdTRUE;
        } else {
            g_lamp.increased = ((g_lamp.value--) < 15) ? pdTRUE : pdFALSE;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    } else if (LAMP_MODE_MARQUEE == g_lamp.lamp_mode) {
        for (int i = 0; i < CONFIG_STRIP_LED_NUM; i++) {
            led_hsv2rgb((uint32_t)g_lamp.hue+i, 100, 100,
                        &r, &g, &b);
            if (0 == i) led_set_rgb(g_lamp.top_led, r, g, b);
            led_strip_set_pixel(g_lamp.led_strip, i, r, g, b);
        }
        /* Refresh the strip to send data */
        led_strip_refresh(g_lamp.led_strip);

        // increase hue for next time
        g_lamp.hue++;
        g_lamp.hue = g_lamp.hue > 360 ? 0 : g_lamp.hue;

        vTaskDelay(pdMS_TO_TICKS(30));
    } else if (LAMP_MODE_STACK == g_lamp.lamp_mode) {
        // clear for all
        all_clear();
        if (0 == g_lamp.index) {
            led_set_rgb(g_lamp.top_led, r, g, b);
        } else {
            led_strip_set_pixel(g_lamp.led_strip, g_lamp.index-1, r, g, b);
        }
        /* Refresh the strip to send data */
        led_strip_refresh(g_lamp.led_strip);
        g_lamp.index++;
        g_lamp.index = g_lamp.index >= CONFIG_STRIP_LED_NUM ? 0 : g_lamp.index;
        vTaskDelay(pdMS_TO_TICKS(100));
    } else {
        ESP_LOGE(TAG, "unknow mode of lamp");
    }
}
