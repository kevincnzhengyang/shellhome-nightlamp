/***
 * @Author      : kevin.z.y <kevin.cn.zhengyang@gmail.com>
 * @Date        : 2024-11-29 11:58:27
 * @LastEditors : kevin.z.y <kevin.cn.zhengyang@gmail.com>
 * @LastEditTime: 2024-11-29 11:58:29
 * @FilePath    : /shellhome-nightlamp/main/board_sensor.h
 * @Description :
 * @Copyright (c) 2024 by Zheng, Yang, All Rights Reserved.
 */

#ifndef BOARD_SENSOR_H
#define BOARD_SENSOR_H

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_idf_version.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#define EVENT_MODE_BITS     BIT1
#define EVENT_TIMER_BITS    BIT2
#define EVENT_COLOR_BITS    BIT3

// innit sensor
esp_err_t sensor_init(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* BOARD_SENSOR_H */