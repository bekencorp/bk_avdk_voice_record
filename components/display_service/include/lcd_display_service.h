// Copyright 2020-2021 Beken
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "driver/lcd_types.h"

typedef struct {
    /** area define*/
	uint16_t start_x;
	uint16_t start_y;
    uint16_t end_x;
	uint16_t end_y;

	uint16_t width;
	uint16_t height;

    uint8_t *buffer;
    void (*pre_refresh)(void); //callback before refresh
    void (*post_refresh)(void *); //refresh complete callback
} lcd_partial_area_t;

typedef struct {
    /** area define*/
	uint16_t width;
	uint16_t height;
    lcd_type_t interface;
} lcd_info_t;

bk_err_t lcd_display_service_init(void);

bk_err_t lcd_display_open(lcd_open_t *config);

bk_err_t lcd_display_close(void);

bool check_lcd_task_is_open(void);

bk_err_t lcd_display_frame_request(frame_buffer_t *frame);

bk_err_t lcd_display_partial_request(lcd_partial_area_t *partial_area);

bk_err_t lcd_display_get_info(lcd_info_t *info);


