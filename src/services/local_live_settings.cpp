#include <services/local_live_settings.h>
#if defined(TRMNL_LOCAL_LIVE) && defined(BOARD_TRMNL_X)
#include <services/local_live_policy.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <esp_random.h>
#include <http_utils.h>

namespace {
const char *SETTINGS_NAMESPACE = "local-display";
String escapeHtml(String value) {
  value.replace("&", "&amp;"); value.replace("\"", "&quot;");
  value.replace("<", "&lt;"); value.replace(">", "&gt;");
  return value;
}
bool saveSettings(const LocalLiveSettings &settings) {
  JsonDocument doc;
  doc["enabled"] = settings.enabled;
  doc["endpoint"] = settings.endpoint;
  doc["token"] = settings.token;
  String json;
  serializeJson(doc, json);
  Preferences prefs;
  if (!prefs.begin(SETTINGS_NAMESPACE, false)) return false;
  bool ok = prefs.putString("config", json) == json.length();
  prefs.end();
  return ok;
}
String parameter(AsyncWebServerRequest *request, const char *name) {
  return request->hasParam(name, true) ? request->getParam(name, true)->value() : String();
}
void sendPage(AsyncWebServerRequest *request, int status, const String &page) {
  auto *response = request->beginResponse(status, "text/html; charset=utf-8", page);
  response->addHeader("Cache-Control", "no-store");
  response->addHeader("X-Frame-Options", "DENY");
  request->send(response);
}
}

LocalLiveSettings local_live_load_settings() {
  LocalLiveSettings settings;
  Preferences prefs;
  if (!prefs.begin(SETTINGS_NAMESPACE, true)) return settings;
  String json = prefs.getString("config", "");
  prefs.end();
  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) return settings;
  settings.endpoint = doc["endpoint"] | "";
  settings.token = doc["token"] | "";
  settings.enabled = (doc["enabled"] | false) && local_live::validEndpoint(settings.endpoint.c_str()) &&
                     local_live::validToken(settings.token.c_str());
  return settings;
}

void local_live_register_settings(AsyncWebServer &server) {
  // Available only while the existing Wi-Fi setup AP is running. This nonce
  // prevents a web page on another origin from submitting a settings change.
  const String nonce = String(esp_random(), HEX) + String(esp_random(), HEX) + String(esp_random(), HEX);
  server.on("/local-display", HTTP_GET, [nonce](AsyncWebServerRequest *request) {
    auto settings = local_live_load_settings();
    String page = F("<!doctype html><html lang=en><meta name=viewport content='width=device-width,initial-scale=1'>"
                    "<title>Local display</title><style>body{font:18px system-ui;max-width:36rem;margin:2rem auto;padding:1rem}"
                    "input[type=url],input[type=password]{box-sizing:border-box;width:100%;padding:.6rem}"
                    "label{display:block;margin:1rem 0}button{padding:.7rem}</style><h1>Local display</h1>"
                    "<p>USB-powered image polling. Disabled uses the normal TRMNL flow. Changes apply on the next boot.</p>"
                    "<form method=post action=/local-display><input type=hidden name=nonce value=\"");
    page += nonce + "\"><label><input type=checkbox name=enabled";
    if (settings.enabled) page += " checked";
    page += "> Enable local display</label><label>Poll endpoint<input type=url name=endpoint maxlength=512 value=\"";
    page += escapeHtml(settings.endpoint) + "\"></label><label>Bearer token<input type=password name=token maxlength=256 autocomplete=new-password></label>";
    page += settings.token.isEmpty() ? "<p>No token saved.</p>" : "<p>A token is saved. Leave blank to keep it.</p>";
    page += F("<label><input type=checkbox name=clear_token> Remove saved token</label><input type=submit value=Save></form>"
              "<p>Configure Wi-Fi on the <a href=/>main setup page</a>, then restart the device.</p></html>");
    sendPage(request, 200, page);
  });
  server.on("/local-display", HTTP_POST, [nonce](AsyncWebServerRequest *request) {
    if (parameter(request, "nonce") != nonce) { sendPage(request, 403, "Invalid setup session. Reload the form."); return; }
    auto previous = local_live_load_settings();
    LocalLiveSettings next;
    next.enabled = request->hasParam("enabled", true);
    next.endpoint = parameter(request, "endpoint");
    next.token = parameter(request, "token");
    bool clear = request->hasParam("clear_token", true);
    if (clear) next.token = "";
    else if (next.token.isEmpty()) next.token = previous.token;
    bool valid = (!next.endpoint.isEmpty() ? local_live::validEndpoint(next.endpoint.c_str()) : !next.enabled) &&
                 local_live::validToken(next.token.c_str());
    if (!valid) { sendPage(request, 400, "Invalid endpoint or token. Use HTTP(S), a host and path, no query or embedded credentials. <a href=/local-display>Back</a>"); return; }
    if (!previous.token.isEmpty() && !clear && parameter(request, "token").isEmpty() &&
        httpOriginOf(next.endpoint) != httpOriginOf(previous.endpoint)) {
      sendPage(request, 400, "Changing servers requires a new token or explicitly removing the old token. <a href=/local-display>Back</a>"); return;
    }
    if (!saveSettings(next)) { sendPage(request, 500, "Could not save settings. Try again. <a href=/local-display>Back</a>"); return; }
    sendPage(request, 200, "Saved. Changes apply after restarting. <a href=/>Wi-Fi setup</a> | <a href=/local-display>Local display settings</a>");
  });
}
#endif
