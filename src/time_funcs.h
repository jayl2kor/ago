#pragma once

#include <stdint.h>

/* Return current time as Unix timestamp in milliseconds */
int64_t agl_now_ms(void);

/* Sleep for the given number of milliseconds */
void agl_sleep_ms(int64_t ms);
