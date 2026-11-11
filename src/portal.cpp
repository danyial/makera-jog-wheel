#include "portal.h"

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_mac.h>

#include "core_config.h"
#include "settings.h"

namespace portal {
namespace {

WebServer server(80);
DNSServer dns;
const IPAddress kApIp(192, 168, 4, 1);
char ssid_buf[32];
char pass_buf[16];
uint32_t last_request_ms = 0;
bool have_credentials = false;
void (*diag_cb)() = nullptr;

void send_form(const String& error) {
  settings::Settings s;
  settings::load(s);
  String html = FPSTR(settings::kPageHead);
  if (error.length() > 0) html += "<p class='err'>" + settings::html_escape(error) + "</p>";
  html += "<form method='post' action='/save'>";
  settings::append_fields(html, s, "nets");
  html += "<datalist id='nets'>";
  const int n = WiFi.scanComplete();
  for (int i = 0; i < n; ++i) html += "<option value='" + settings::html_escape(WiFi.SSID(i)) + "'>";
  html += "</datalist>";
  if (n < 0) html += "<p class='hint'>Netzwerksuche läuft …</p>";
  html += "<button type='submit'>Speichern und neu starten</button></form>";
  html += "<form method='get' action='/'><input type='hidden' name='scan' value='1'>"
          "<button type='submit' class='sec'>Netzwerke neu suchen</button></form>";
  html += "<form method='post' action='/diag'><button type='submit' class='sec'>Diagnose starten</button></form>";
  html += "<p class='foot'>Firmware " + String(FW_VERSION) + "</p></main></body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handle_root() {
  last_request_ms = millis();
  if (server.hasArg("scan") && WiFi.scanComplete() != WIFI_SCAN_RUNNING) {
    WiFi.scanDelete();
    WiFi.scanNetworks(true);
  }
  send_form("");
}

void handle_save() {
  last_request_ms = millis();
  settings::Form f;
  f.ssid = server.arg("ssid");
  f.pass = server.arg("pass");
  f.host = server.arg("host");
  f.port = server.arg("port");
  f.axis_a = server.hasArg("axis_a");
  f.ota = server.hasArg("ota");
  const char* err = settings::save_form(f);
  if (err != nullptr) return send_form(err);
  String html = FPSTR(settings::kPageHead);
  html += "<p>Gespeichert. Das Handrad startet neu.</p></main></body></html>";
  server.send(200, "text/html; charset=utf-8", html);
  delay(500);
  esp_restart();
}

void handle_diag() {
  last_request_ms = millis();
  String html = FPSTR(settings::kPageHead);
  html += "<p>Diagnose startet. Verlassen: Ein/Aus-Schalter mindestens 3 s aus.</p></main></body></html>";
  server.send(200, "text/html; charset=utf-8", html);
  delay(500);
  if (diag_cb != nullptr) diag_cb();
}

void handle_other() {
  // Captive portal: send every unknown host/path to the form.
  server.sendHeader("Location", String("http://") + kApIp.toString() + "/", true);
  server.send(302, "text/plain", "");
}

}  // namespace

void begin(void (*on_diag)()) {
  diag_cb = on_diag;
  settings::Settings s;
  have_credentials = settings::load(s);

  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  snprintf(ssid_buf, sizeof(ssid_buf), "Makera-Handrad-%02X%02X", mac[4], mac[5]);
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  // Generate the AP password only now: esp_random() is a true RNG only while the radio runs.
  const String pw = settings::ap_pass();
  snprintf(pass_buf, sizeof(pass_buf), "%s", pw.c_str());
  WiFi.softAPConfig(kApIp, kApIp, IPAddress(255, 255, 255, 0));
  WiFi.softAP(ssid_buf, pass_buf);
  WiFi.scanNetworks(true);

  dns.start(53, "*", kApIp);
  server.on("/", HTTP_GET, handle_root);
  server.on("/save", HTTP_POST, handle_save);
  server.on("/diag", HTTP_POST, handle_diag);
  server.onNotFound(handle_other);
  server.begin();
  last_request_ms = millis();
}

void loop(uint32_t now_ms) {
  (void)now_ms;
  dns.processNextRequest();
  server.handleClient();
  // Read the clock after handleClient(): a handler may have set last_request_ms later than now_ms.
  if (have_credentials && static_cast<uint32_t>(millis() - last_request_ms) >= PORTAL_TIMEOUT_MS) esp_restart();
}

const char* ap_ssid() { return ssid_buf; }
const char* ap_password() { return pass_buf; }

}  // namespace portal
