#include <services/local_live.h>
#if defined(TRMNL_LOCAL_LIVE) && defined(BOARD_TRMNL_X)
#include <services/local_live_settings.h>
#include <http_utils.h>
#include <misc/clock.h>
#include <services/local_live_policy.h>
#include <services/http_retry_request.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WifiCaptive.h>
#include <display.h>
#include <globals.h>
#include <power.h>
#include <wifi_network.h>
#include <iqs323_task.h>
#include <trmnl_log.h>

namespace {
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
  config.maxAttempts = 1; // the outer loop owns backoff and USB checks
  return config;
}
}

bool local_live_run() {
  if (!usbConnected()) return false;
  const auto settings = local_live_load_settings();
  if (!settings.enabled) return false;
  const String origin = httpOriginOf(settings.endpoint);
  if (settings.endpoint.startsWith("https://") && !systemClock().setTimeFromNTP()) {
    Log_error("Cannot establish clock for local display TLS; using normal firmware flow");
    return false;
  }
  Log_info("USB local live mode started (experimental partial refresh)");
  // Do not use the stock downloadAndShow(): it disconnects Wi-Fi before drawing.
  // No cloud API/OTA calls run while this opt-in mode owns the display.
  String displayedId;
  String pendingAck;
  String ackMode;
  uint8_t ackPasses = 0;
  local_live::RefreshPolicy policy;
  unsigned failures = 0;
  while (usbConnected()) {
    if (pendingAck.length()) {
      String ackPath = pendingAck + (pendingAck.indexOf('?') >= 0 ? "&mode=" : "?mode=") + ackMode;
      if (ackMode == "partial") ackPath += "&passes=" + String(ackPasses);
      HttpRetryRequest ack(requestConfig(origin + ackPath, settings));
      if (ack.execute() == HTTPS_NO_ERR && ack.httpCode() == 200) pendingAck = "";
    }
    if (!usbConnected()) break;
    HttpRetryRequest next(requestConfig(settings.endpoint + "?wait=8&after=" + displayedId, settings));
    bool ok = next.execute() == HTTPS_NO_ERR && next.httpCode() == 200;
    if (!usbConnected()) break;
    JsonDocument document;
    if (ok) ok = next.bodySize() < 2048 && deserializeJson(document, next.body(), next.bodySize()) == DeserializationError::Ok;
    if (ok && !document["changed"].is<bool>()) ok = false;
    if (ok && document["changed"].as<bool>()) {
      const char *id = document["id"] | "";
      int partialPasses = local_live::PARTIAL_DRIVE_PASSES;
      if (!document["partialPasses"].isNull()) {
        if (!document["partialPasses"].is<int>()) ok = false;
        else partialPasses = document["partialPasses"].as<int>();
      }
      String imagePath = document["image"] | "";
      String ackPath = document["ack"] | "";
      ok = ok && local_live::validPartialPasses(partialPasses) && local_live::validId(id) && local_live::validPath(imagePath.c_str()) &&
        (ackPath.isEmpty() || local_live::validPath(ackPath.c_str())) &&
        document["width"].as<int>() == display_width() && document["height"].as<int>() == display_height();
      if (ok) {
        HttpRetryRequest image(requestConfig(origin + imagePath, settings));
        ok = image.execute() == HTTPS_NO_ERR && image.httpCode() == 200 &&
          local_live::validPng(image.body(), image.bodySize(), display_width(), display_height());
        if (!usbConnected()) break;
        if (ok) {
          bool full = policy.full(millis(), document["forceFull"] | true);
          bool actualFull = full;
          ok = display_show_local_image(image.body(), image.bodySize(), full, &actualFull, partialPasses);
          if (ok) {
            policy.displayed(millis(), actualFull);
            displayedId = id;
            pendingAck = ackPath;
            ackMode = actualFull ? "full" : "partial";
            ackPasses = actualFull ? 0 : partialPasses;
          } else policy.failed();
        }
      }
    }
    if (ok) failures = 0;
    else {
      failures = failures < 6 ? failures + 1 : 6;
      Log_error("Local display request failed (HTTP %d); retaining previous image", next.httpCode());
      bool modem = g_modem && WifiCaptivePortal.getLastCredentials().is5GHz;
      if ((!modem && WiFi.status() != WL_CONNECTED) || (modem && failures % 4 == 0)) connectWithSavedCredentials();
    }
    if (!pauseWhilePowered(ok ? 3000 : local_live::retryDelay(failures))) break;
  }
  Log_info("USB disconnected; returning to normal sleep cycle");
  return true;
}
#else
bool local_live_run() { return false; }
#endif
