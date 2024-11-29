/***
 * @Author      : kevin.z.y <kevin.cn.zhengyang@gmail.com>
 * @Date        : 2024-11-29 10:46:18
 * @LastEditors : kevin.z.y <kevin.cn.zhengyang@gmail.com>
 * @LastEditTime: 2024-11-29 10:46:21
 * @FilePath    : /shellhome-nightlamp/main/board_leds.h
 * @Description :
 * @Copyright (c) 2024 by Zheng, Yang, All Rights Reserved.
 */

#ifndef BOARD_LAMP_H
#define BOARD_LAMP_H

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


// timer for saving
#define EVENT_SAVE_BITS     BIT0

//  init leds
esp_err_t leds_init(void);

// start leds task
esp_err_t leds_start(void);

// flush leds
void leds_flush(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* BOARD_LEDS_H */