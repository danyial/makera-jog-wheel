// Host tool (ticket #36): writes the web pages as static HTML files for a browser check.
//
//   c++ -std=c++17 -Iinclude -Isrc tools/dump_web_pages.cpp -o <tmp>/dump && <tmp>/dump <out-dir>
//
// status.html is byte for byte what GET / sends (dynamic start + flash tail). Serve <out-dir> with a
// mock status.json next to it, e.g. `python3 -m http.server`. Since ticket #37 the static assets land in
// <out-dir>/a/ as the gzip bytes the handwheel sends: <out-dir>/a must exist, and the server must send
// them with `Content-Encoding: gzip` and `Content-Type: text/javascript` (http.server does not), or the
// 3D view cannot load. settings.html and portal.html put the
// shared page head around a static copy of the form from settings.cpp/portal.cpp (those build their
// fields with Arduino String at runtime), only to check the look.
#include <cstdio>
#include <string>

#include "web_assets.h"
#include "web_pages.h"

namespace {

const char kTail[] = WEB_STATUS_TAIL;
const char kHead[] = WEB_PAGE_HEAD;

const char kFields[] =
    "<label>WLAN-SSID</label><input name='ssid' required maxlength='32' list='nets' value='Beispiel-WLAN'>"
    "<label>WLAN-Passwort</label><input name='pass' type='password' maxlength='63' "
    "placeholder='unverändert lassen: leer'>"
    "<label>Maschinen-IP</label><input name='host' required pattern='\\d{1,3}(\\.\\d{1,3}){3}' "
    "value='192.0.2.31'>"
    "<label>Port</label><input name='port' type='number' min='1' max='65535' value='2222'>"
    "<label class='chk'><input type='checkbox' name='axis_a' value='1'> Drehachse A vorhanden</label>"
    "<label class='chk'><input type='checkbox' name='ota' value='1' checked> Update über WLAN erlauben</label>"
    "<p class='hint'>Firmware-Update per espota, Passwort = Handrad-Passwort. Nur ohne gewählte Achse.</p>";

bool write(const std::string& path, const std::string& text) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (f == nullptr) return false;
  std::fwrite(text.data(), 1, text.size(), f);
  std::fclose(f);
  std::printf("%s: %zu Byte\n", path.c_str(), text.size());
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s <out-dir>\n", argv[0]);
    return 2;
  }
  const std::string dir = argv[1];
  char start[320];
  const int n = web_status_start(start, sizeof(start));
  std::printf("status start %d Byte, tail %zu Byte\n", n, sizeof(kTail) - 1);
  bool ok = write(dir + "/status.html", std::string(start) + kTail);
  ok = ok && write(dir + "/settings.html",
                   std::string(kHead) + "<p class='err'>Handrad-Passwort falsch. Nichts gespeichert.</p>"
                       "<form method='post' action='/settings'>" + kFields +
                       "<label>Handrad-Passwort</label><input name='auth' type='password' required>"
                       "<p class='hint'>Das Passwort des Einrichtungs-WLANs (AP-Passwort). Es steht auf dem "
                       "Display, wenn das Handrad mit gehaltener X-Taste startet.</p>"
                       "<button type='submit'>Speichern und neu starten</button></form>"
                       "<p class='foot'><a href='/'>Status</a></p></main></body></html>");
  ok = ok && write(dir + "/portal.html",
                   std::string(kHead) + "<form method='post' action='/save'>" + kFields +
                       "<datalist id='nets'><option value='Beispiel-WLAN'></datalist>"
                       "<button type='submit'>Speichern und neu starten</button></form>"
                       "<form method='get' action='/'><input type='hidden' name='scan' value='1'>"
                       "<button type='submit' class='sec'>Netzwerke neu suchen</button></form>"
                       "<form method='post' action='/diag'><button type='submit' class='sec'>Diagnose starten"
                       "</button></form><p class='foot'>Firmware 0.1.0</p></main></body></html>");
  for (const WebAsset& a : kWebAssets) {
    ok = ok && write(dir + a.path, std::string(reinterpret_cast<const char*>(a.gz), a.len));
  }
  return ok ? 0 : 1;
}
