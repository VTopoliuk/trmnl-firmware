#include <services/local_live.h>
#if defined(TRMNL_LOCAL_LIVE) && defined(BOARD_TRMNL_X)
#include <services/local_live_policy.h>
#include <services/http_retry_request.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WifiCaptive.h>
#include <Preferences.h>
#include <display.h>
#include <globals.h>
#include <http_utils.h>
#include <power.h>
#include <wifi_network.h>
#include <iqs323_task.h>
#include <trmnl_log.h>
#include <misc/clock.h>
#include <esp_attr.h>
#include <services/local_live_settings.h>

namespace {
LocalLiveSettings readSettings() { return local_live_load_settings(); }
bool activeLocalSession = false;
constexpr uint32_t FRAME_MAGIC = 0x4c465231;
struct RetainedFrame {
  uint32_t magic;
  char id[33];
  char ack[513];
  bool full;
  uint8_t passes;
};
RTC_DATA_ATTR RetainedFrame retained = {};

bool savedLocalMode() {
  Preferences prefs;
  if (!prefs.begin("local-display", true)) return true;
  bool local = prefs.getBool("mode_local", true);
  prefs.end();
  return local;
}
void saveLocalMode(bool local) {
  Preferences prefs;
  if (!prefs.begin("local-display", false)) return;
  if (prefs.getBool("mode_local", true) != local) prefs.putBool("mode_local", local);
  prefs.end();
}
bool usbConnected() {
  iqs323_task_i2c_lock();
  bool connected = power().usbStatus() == UsbStatus::CONNECTED;
  iqs323_task_i2c_unlock();
  return connected;
}
bool pauseWhilePowered(uint32_t duration) {
  uint32_t start = millis();
  while (uint32_t(millis() - start) < duration) {
    if (!usbConnected()) return false;
    delay(100);
  }
  return true;
}
HttpRetryRequestConfig requestConfig(const String &url, const LocalLiveSettings &settings) {
  HttpRetryRequestConfig config;
  config.apiBaseUrl = httpOriginOf(settings.endpoint);
  config.url = url;
  if (!settings.token.isEmpty()) config.headers.emplace_back("Authorization", String("Bearer ") + settings.token);
  config.readTimeoutSeconds = 12;
  config.modemTimeoutMs = 12000;
  config.maxAttempts = 1;
  return config;
}
void acknowledge(const String &origin, const LocalLiveSettings &settings) {
  if (!retained.ack[0]) return;
  String path = retained.ack;
  path += path.indexOf('?') >= 0 ? "&mode=" : "?mode=";
  path += retained.full ? "full" : "partial";
  if (!retained.full) path += "&passes=" + String(retained.passes);
  HttpRetryRequest request(requestConfig(origin + path, settings));
  if (request.execute() == HTTPS_NO_ERR && request.httpCode() == 200) retained.ack[0] = 0;
}
}

bool local_live_local_mode() {
  return readSettings().enabled && savedLocalMode();
}
uint32_t local_live_sleep_seconds() {
  return activeLocalSession ? local_live::BATTERY_REFRESH_SECONDS : 0;
}

bool local_live_run() {
  const auto settings = readSettings();
  if (!settings.enabled || !WifiCaptivePortal.isSaved()) return false;
  activeLocalSession = savedLocalMode();
  retained.id[32] = 0;
  retained.ack[512] = 0;
  if (retained.magic != FRAME_MAGIC || !local_live::validId(retained.id) ||
      (retained.ack[0] && !local_live::validPath(retained.ack)) ||
      (!retained.full && !local_live::validPartialPasses(retained.passes))) retained = {};
  const String origin = httpOriginOf(settings.endpoint);
  String displayedId = usbConnected() ? String() : String(retained.id);
  local_live::RefreshPolicy policy; // lost framebuffers always require a full draw
  local_live::PowerCycles cycles;
  unsigned failures = 0;
  bool networkReady = WiFi.status() == WL_CONNECTED;
  Log_info("Local display: USB live polling, battery periodic polling");
  while (true) {
    bool powered = usbConnected();
    if (!cycles.begin(powered)) break;
    bool modem = g_modem && WifiCaptivePortal.getLastCredentials().is5GHz;
    if (!networkReady || (!modem && WiFi.status() != WL_CONNECTED)) networkReady = connectWithSavedCredentials();
    bool ok = networkReady;
    if (ok && settings.endpoint.startsWith("https://")) ok = systemClock().setTimeFromNTP();
    if (ok) {
      acknowledge(origin, settings);
      String poll = settings.endpoint + "?wait=" + String(powered && activeLocalSession ? 8 : 0) +
                    "&after=" + displayedId + "&power=" + (powered ? "usb" : "battery");
      HttpRetryRequest next(requestConfig(poll, settings));
      ok = next.execute() == HTTPS_NO_ERR && next.httpCode() == 200;
      JsonDocument document;
      if (ok) ok = next.bodySize() < 2048 && deserializeJson(document, next.body(), next.bodySize()) == DeserializationError::Ok;
      String mode = document["displayMode"] | "local";
      if (ok) ok = document["changed"].is<bool>() && (mode == "local" || mode == "normal");
      if (ok && mode == "normal") {
        saveLocalMode(false);
        retained = {}; // a cloud frame must never be mistaken for the old local frame
        activeLocalSession = false;
        Log_info("Local server selected normal TRMNL mode");
        return false;
      }
      if (ok) { saveLocalMode(true); activeLocalSession = true; }
      if (ok && document["changed"].as<bool>()) {
        const char *id = document["id"] | "";
        String imagePath = document["image"] | "";
        String ackPath = document["ack"] | "";
        int passes = local_live::PARTIAL_DRIVE_PASSES;
        if (!document["partialPasses"].isNull()) {
          if (!document["partialPasses"].is<int>()) ok = false;
          else passes = document["partialPasses"].as<int>();
        }
        ok = ok && local_live::validId(id) && local_live::validPartialPasses(passes) &&
             local_live::validPath(imagePath.c_str()) && (ackPath.isEmpty() || local_live::validPath(ackPath.c_str())) &&
             document["width"].as<int>() == display_width() && document["height"].as<int>() == display_height();
        if (ok) {
          HttpRetryRequest image(requestConfig(origin + imagePath, settings));
          ok = image.execute() == HTTPS_NO_ERR && image.httpCode() == 200 &&
               local_live::validPng(image.body(), image.bodySize(), display_width(), display_height());
          if (ok) {
            bool full = policy.full(millis(), document["forceFull"] | true);
            bool actualFull = full;
            ok = display_show_local_image(image.body(), image.bodySize(), full, &actualFull, passes);
            if (ok) {
              policy.displayed(millis(), actualFull);
              displayedId = id;
              retained.magic = FRAME_MAGIC;
              displayedId.toCharArray(retained.id, sizeof(retained.id));
              ackPath.toCharArray(retained.ack, sizeof(retained.ack));
              retained.full = actualFull;
              retained.passes = actualFull ? 0 : passes;
              acknowledge(origin, settings);
            } else policy.failed();
          }
        }
      }
    }
    if (ok) failures = 0;
    else {
      Log_error("Local display request failed; retaining the current screen");
      if (!activeLocalSession) return false; // normal mode still works if the local server is offline
      failures = failures < 6 ? failures + 1 : 6;
      if (modem && failures % 4 == 0) networkReady = false;
    }
    if (powered) pauseWhilePowered(ok ? 3000 : local_live::retryDelay(failures));
  }
  return true;
}
#else
bool local_live_run() { return false; }
bool local_live_local_mode() { return false; }
uint32_t local_live_sleep_seconds() { return 0; }
#endif
