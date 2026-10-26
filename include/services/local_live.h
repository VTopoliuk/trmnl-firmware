#pragma once
#include <stdint.h>
// True when a local session finished and the caller should sleep.
bool local_live_run();
// Suppress stock cached-playlist navigation while the local screen owns it.
bool local_live_local_mode();
// Zero preserves stock sleep; a local session uses the battery refresh interval.
uint32_t local_live_sleep_seconds();
