/*
 * @Author      : kevin.z.y <kevin.cn.zhengyang@gmail.com>
 * @Date        : 2024-11-28 22:40:42
 * @LastEditors : kevin.z.y <kevin.cn.zhengyang@gmail.com>
 * @LastEditTime: 2024-11-29 15:31:50
 * @FilePath    : /shellhome-nightlamp/main/app_main.c
 * @Description :
 * Copyright (c) 2024 by Zheng, Yang, All Rights Reserved.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_check.h"
#include "nvs_flash.h"

#include "board_leds.h"
#include "board_sensor.h"


static const char *TAG = "LAMP";
EventGroupHandle_t g_event_group;


void app_main(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "Starting ...");

    // init event group
    g_event_group = xEventGroupCreate();

    // init NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ret = nvs_flash_erase();
        ESP_RETURN_VOID_ON_FALSE(ESP_OK == ret, TAG, "nvs flash erase failed");
        ret = nvs_flash_init();
    }
    ESP_RETURN_VOID_ON_FALSE(ESP_OK == ret, TAG, "nvs flash init failed");

    ESP_LOGI(TAG, "Init ...");

    // init leds
    ret = leds_init();
    ESP_RETURN_VOID_ON_FALSE(ESP_OK == ret, TAG, "leds init failed");

    // init sensor
    ret = sensor_init();
    ESP_RETURN_VOID_ON_FALSE(ESP_OK == ret, TAG, "sensor init failed");

    // start leds
    ESP_LOGI(TAG, "Start ...");
    ret = leds_start();
    ESP_RETURN_VOID_ON_FALSE(ESP_OK == ret, TAG, "leds start failed");

    // flush lamp
    ESP_LOGI(TAG, "Flushing ...");
    while (true) {
        leds_flush();
    }
}
