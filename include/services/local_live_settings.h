#pragma once
#if defined(TRMNL_LOCAL_LIVE) && defined(BOARD_TRMNL_X)
#include <Arduino.h>
class AsyncWebServer;
struct LocalLiveSettings {
  bool enabled = false;
  String endpoint;
  String token;
};
LocalLiveSettings local_live_load_settings();
void local_live_register_settings(AsyncWebServer &server);
#endif
