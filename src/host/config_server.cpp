// Native-only settings server. Mirrors the hardware's WiFiManager
// captive-portal form so you can drive the SDL emulator the same way
// you'll drive a real ESP32 once the hardware arrives. See header for
// the full contract.
//
// Implementation notes:
//   * Hand-rolled BSD-socket HTTP server (no cpp-httplib, no libevent).
//     The endpoint set is tiny (GET /, POST /save, POST /reset), so a
//     ~250-line handler is smaller than pulling a header-only lib.
//   * Loopback only (127.0.0.1). Never binds a public address —
//     nothing on this port needs to reach the LAN.
//   * State mutations are queued from the HTTP thread; the main SDL
//     loop drains them via applyPending() on each frame. Keeps all
//     service::*::save calls on the render thread.

#ifdef USE_NATIVE

#include "host/config_server.h"

#include <ArduinoJson.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "services/focus_points.h"
#include "services/metar_config.h"
#include "services/radar_location.h"

namespace host::config_server {
namespace {

constexpr const char* kConfigPath = "emulator_config.json";

struct PendingChange {
  std::optional<double> home_lat;
  std::optional<double> home_lon;
  std::optional<float> metar_lat;
  std::optional<float> metar_lon;
  std::optional<float> metar_rad;
  std::optional<std::string> focus_ring_json;
  bool reset = false;
};

std::mutex s_pending_mutex;
std::vector<PendingChange> s_pending;

std::thread s_server_thread;
std::atomic<bool> s_running{false};
int s_listen_fd = -1;

// -- URL decode --------------------------------------------------------
std::string urlDecode(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '+') {
      out.push_back(' ');
    } else if (s[i] == '%' && i + 2 < s.size()) {
      char hex[3] = {s[i + 1], s[i + 2], '\0'};
      out.push_back(static_cast<char>(std::strtol(hex, nullptr, 16)));
      i += 2;
    } else {
      out.push_back(s[i]);
    }
  }
  return out;
}

std::vector<std::pair<std::string, std::string>>
parseFormBody(const std::string& body) {
  std::vector<std::pair<std::string, std::string>> out;
  std::stringstream ss(body);
  std::string kv;
  while (std::getline(ss, kv, '&')) {
    const auto eq = kv.find('=');
    if (eq == std::string::npos) {
      out.emplace_back(urlDecode(kv), "");
    } else {
      out.emplace_back(urlDecode(kv.substr(0, eq)),
                       urlDecode(kv.substr(eq + 1)));
    }
  }
  return out;
}

// -- HTML page ---------------------------------------------------------
// Kept intentionally close to what WiFiManager's default portal looks
// like on hardware — same field names, same order. When the user opens
// http://plane-radar.local on their ESP32 after the hardware arrives,
// they should recognize this page.
std::string renderIndex() {
  char home_lat[32], home_lon[32];
  char metar_lat[32], metar_lon[32], metar_rad[16];
  std::snprintf(home_lat, sizeof(home_lat), "%.6f",
                services::location::lat());
  std::snprintf(home_lon, sizeof(home_lon), "%.6f",
                services::location::lon());
  std::snprintf(metar_lat, sizeof(metar_lat), "%.6f",
                static_cast<double>(services::metar_config::centerLat()));
  std::snprintf(metar_lon, sizeof(metar_lon), "%.6f",
                static_cast<double>(services::metar_config::centerLon()));
  std::snprintf(metar_rad, sizeof(metar_rad), "%.1f",
                static_cast<double>(services::metar_config::radiusNm()));
  const std::string focus_ring_json =
      std::string(services::focus::currentRingJson().c_str());

  std::stringstream html;
  html << "<!doctype html><html><head><meta charset=\"utf-8\">"
       << "<title>Plane Radar — Emulator Settings</title>"
       << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
       << "<style>"
       << "body{font-family:-apple-system,system-ui,sans-serif;max-width:640px;"
       << "margin:24px auto;padding:0 16px;background:#04081c;color:#e6e6ec}"
       << "h1{font-size:20px;margin:0 0 4px}"
       << "p.sub{color:#7a86ad;margin:0 0 20px;font-size:13px}"
       << "label{display:block;margin:12px 0 4px;font-size:12px;color:#7a86ad}"
       << "input[type=text],input[type=number],textarea{width:100%;padding:6px 8px;"
       << "background:#0b1330;border:1px solid #1a2450;border-radius:6px;"
       << "color:#e6e6ec;font-family:inherit;font-size:13px;box-sizing:border-box}"
       << "textarea{min-height:80px;font-family:ui-monospace,Menlo,monospace}"
       << ".row{display:grid;grid-template-columns:1fr 1fr;gap:8px}"
       << ".row3{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px}"
       << "button{padding:8px 16px;border-radius:6px;font-size:13px;cursor:pointer;"
       << "border:1px solid #1a2450;font-family:inherit}"
       << ".save{background:#4dd0e1;color:#04081c;border-color:#4dd0e1;font-weight:600}"
       << ".reset{background:transparent;color:#7a86ad;margin-right:8px}"
       << ".hint{color:#7a86ad;font-size:12px;margin:0 0 8px}"
       << "hr{border:none;border-top:1px solid #1a2450;margin:20px 0}"
       << "footer{margin-top:24px;display:flex;justify-content:space-between}"
       << "</style></head><body>"
       << "<h1>Plane Radar — Emulator Settings</h1>"
       << "<p class=\"sub\">Same fields as the hardware WiFi portal. "
       << "Changes apply to the SDL window immediately and persist to "
       << "<code>emulator_config.json</code>.</p>"
       << "<form method=\"POST\" action=\"/save\">"
       << "<h3>Home</h3>"
       << "<p class=\"hint\">Radar center + outdoor-temperature reference.</p>"
       << "<div class=\"row\">"
       << "<div><label>Latitude</label>"
       << "<input type=\"number\" step=\"0.000001\" name=\"radar_lat\" value=\""
       << home_lat << "\" required></div>"
       << "<div><label>Longitude</label>"
       << "<input type=\"number\" step=\"0.000001\" name=\"radar_lon\" value=\""
       << home_lon << "\" required></div>"
       << "</div>"
       << "<hr>"
       << "<h3>METAR flight-rules map</h3>"
       << "<p class=\"hint\">Center + radius for the airport-dots view.</p>"
       << "<div class=\"row3\">"
       << "<div><label>Center lat</label>"
       << "<input type=\"number\" step=\"0.000001\" name=\"metar_lat\" value=\""
       << metar_lat << "\" required></div>"
       << "<div><label>Center lon</label>"
       << "<input type=\"number\" step=\"0.000001\" name=\"metar_lon\" value=\""
       << metar_lon << "\" required></div>"
       << "<div><label>Radius (nm)</label>"
       << "<input type=\"number\" step=\"0.1\" min=\"1\" name=\"metar_rad\" value=\""
       << metar_rad << "\" required></div>"
       << "</div>"
       << "<hr>"
       << "<h3>Focus airports</h3>"
       << "<p class=\"hint\">JSON: <code>[{\"name\":\"SFO\",\"lat\":37.62,"
       << "\"lon\":-122.38,\"range_idx\":1}, ...]</code></p>"
       << "<label>focus_ring</label>"
       << "<textarea name=\"focus_ring\">" << focus_ring_json
       << "</textarea>"
       << "<footer>"
       << "<button type=\"submit\" formaction=\"/reset\" class=\"reset\">"
       << "Reset to defaults</button>"
       << "<button type=\"submit\" class=\"save\">Save</button>"
       << "</footer>"
       << "</form></body></html>";
  return html.str();
}

// -- HTTP write helpers ------------------------------------------------
void writeAll(int fd, const std::string& s) {
  const char* p = s.data();
  size_t left = s.size();
  while (left > 0) {
    const ssize_t n = ::write(fd, p, left);
    if (n <= 0) return;
    p += n;
    left -= static_cast<size_t>(n);
  }
}

void send200Html(int fd, const std::string& body) {
  std::stringstream head;
  head << "HTTP/1.1 200 OK\r\n"
       << "Content-Type: text/html; charset=utf-8\r\n"
       << "Content-Length: " << body.size() << "\r\n"
       << "Connection: close\r\n\r\n";
  writeAll(fd, head.str());
  writeAll(fd, body);
}

void send303Redirect(int fd, const char* location) {
  std::stringstream head;
  head << "HTTP/1.1 303 See Other\r\n"
       << "Location: " << location << "\r\n"
       << "Content-Length: 0\r\n"
       << "Connection: close\r\n\r\n";
  writeAll(fd, head.str());
}

void send404(int fd) {
  const char* body = "not found";
  std::stringstream head;
  head << "HTTP/1.1 404 Not Found\r\n"
       << "Content-Type: text/plain\r\n"
       << "Content-Length: " << std::strlen(body) << "\r\n"
       << "Connection: close\r\n\r\n"
       << body;
  writeAll(fd, head.str());
}

// -- Persistence -------------------------------------------------------
void writeConfigJson() {
  JsonDocument doc;
  doc["home_lat"] = services::location::lat();
  doc["home_lon"] = services::location::lon();
  doc["metar_lat"] = services::metar_config::centerLat();
  doc["metar_lon"] = services::metar_config::centerLon();
  doc["metar_rad"] = services::metar_config::radiusNm();
  // Focus ring persists as the same JSON blob the ESP32 stores in NVS,
  // so switching from emulator → hardware later (or vice versa) just
  // copy-pastes.
  doc["focus_ring"] =
      serialized(std::string(services::focus::currentRingJson().c_str()));

  std::ofstream out(kConfigPath, std::ios::trunc);
  if (!out) {
    std::printf("config_server: failed to open %s for write\n", kConfigPath);
    return;
  }
  std::string blob;
  serializeJsonPretty(doc, blob);
  out << blob;
}

void applyChangeUnlocked(const PendingChange& p) {
  if (p.reset) {
    // Reset to compiled-in defaults by clearing the location and
    // re-init'ing metar_config against its NVS defaults (which the
    // native Preferences shim treats as an empty in-memory store).
    services::location::clear();
    services::metar_config::init();
    return;
  }
  if (p.home_lat && p.home_lon) {
    char lat_buf[32], lon_buf[32];
    std::snprintf(lat_buf, sizeof(lat_buf), "%.6f", *p.home_lat);
    std::snprintf(lon_buf, sizeof(lon_buf), "%.6f", *p.home_lon);
    services::location::saveFromStrings(lat_buf, lon_buf);
  }
  if (p.metar_lat && p.metar_lon && p.metar_rad) {
    char lat_buf[32], lon_buf[32], rad_buf[16];
    std::snprintf(lat_buf, sizeof(lat_buf), "%.6f",
                  static_cast<double>(*p.metar_lat));
    std::snprintf(lon_buf, sizeof(lon_buf), "%.6f",
                  static_cast<double>(*p.metar_lon));
    std::snprintf(rad_buf, sizeof(rad_buf), "%.1f",
                  static_cast<double>(*p.metar_rad));
    services::metar_config::saveFromStrings(lat_buf, lon_buf, rad_buf);
  }
  if (p.focus_ring_json && !p.focus_ring_json->empty()) {
    services::focus::saveRingJson(p.focus_ring_json->c_str());
  }
}

// -- Request handling --------------------------------------------------
void handleConnection(int client_fd) {
  std::string buf;
  buf.reserve(4096);
  char chunk[2048];
  size_t header_end = std::string::npos;
  size_t content_length = 0;
  bool have_headers = false;

  // Read headers.
  while (header_end == std::string::npos) {
    const ssize_t n = ::read(client_fd, chunk, sizeof(chunk));
    if (n <= 0) { ::close(client_fd); return; }
    buf.append(chunk, static_cast<size_t>(n));
    header_end = buf.find("\r\n\r\n");
    if (buf.size() > 64 * 1024) { ::close(client_fd); return; }
  }
  have_headers = true;
  const std::string headers = buf.substr(0, header_end);
  buf.erase(0, header_end + 4);

  // Parse request line + Content-Length.
  const auto first_line_end = headers.find("\r\n");
  const std::string request_line = headers.substr(0, first_line_end);
  const auto sp1 = request_line.find(' ');
  const auto sp2 = sp1 == std::string::npos ? std::string::npos
                                            : request_line.find(' ', sp1 + 1);
  if (sp1 == std::string::npos || sp2 == std::string::npos) {
    ::close(client_fd);
    return;
  }
  const std::string method = request_line.substr(0, sp1);
  const std::string path = request_line.substr(sp1 + 1, sp2 - sp1 - 1);

  // Content-Length header (case-insensitive on the name).
  {
    std::stringstream ss(headers);
    std::string line;
    while (std::getline(ss, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      const auto colon = line.find(':');
      if (colon == std::string::npos) continue;
      std::string name = line.substr(0, colon);
      for (auto& c : name) c = static_cast<char>(std::tolower(c));
      if (name == "content-length") {
        content_length = std::strtoull(line.substr(colon + 1).c_str(),
                                       nullptr, 10);
      }
    }
  }
  (void)have_headers;

  // Read remaining body bytes.
  while (buf.size() < content_length) {
    const ssize_t n = ::read(client_fd, chunk, sizeof(chunk));
    if (n <= 0) break;
    buf.append(chunk, static_cast<size_t>(n));
  }
  const std::string body = buf.substr(0, content_length);

  // Dispatch.
  if (method == "GET" && (path == "/" || path == "/index.html")) {
    send200Html(client_fd, renderIndex());
  } else if (method == "POST" && path == "/save") {
    const auto fields = parseFormBody(body);
    PendingChange p;
    for (const auto& [k, v] : fields) {
      if (k == "radar_lat")  p.home_lat  = std::atof(v.c_str());
      else if (k == "radar_lon")  p.home_lon  = std::atof(v.c_str());
      else if (k == "metar_lat")  p.metar_lat = static_cast<float>(std::atof(v.c_str()));
      else if (k == "metar_lon")  p.metar_lon = static_cast<float>(std::atof(v.c_str()));
      else if (k == "metar_rad")  p.metar_rad = static_cast<float>(std::atof(v.c_str()));
      else if (k == "focus_ring") p.focus_ring_json = v;
    }
    {
      std::lock_guard<std::mutex> lock(s_pending_mutex);
      s_pending.push_back(std::move(p));
    }
    send303Redirect(client_fd, "/");
  } else if (method == "POST" && path == "/reset") {
    PendingChange p; p.reset = true;
    {
      std::lock_guard<std::mutex> lock(s_pending_mutex);
      s_pending.push_back(std::move(p));
    }
    send303Redirect(client_fd, "/");
  } else {
    send404(client_fd);
  }
  ::close(client_fd);
}

void serverThreadMain(int port) {
  s_listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (s_listen_fd < 0) {
    std::printf("config_server: socket() failed: %s\n", std::strerror(errno));
    return;
  }
  int yes = 1;
  ::setsockopt(s_listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(static_cast<uint16_t>(port));
  if (::bind(s_listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    std::printf("config_server: bind :%d failed: %s\n", port,
                std::strerror(errno));
    ::close(s_listen_fd);
    s_listen_fd = -1;
    return;
  }
  if (::listen(s_listen_fd, 8) < 0) {
    std::printf("config_server: listen failed: %s\n", std::strerror(errno));
    ::close(s_listen_fd);
    s_listen_fd = -1;
    return;
  }
  std::printf("config_server: http://127.0.0.1:%d (mirrors WiFiManager form)\n",
              port);

  while (s_running.load()) {
    sockaddr_in client_addr = {};
    socklen_t client_len = sizeof(client_addr);
    const int client_fd = ::accept(
        s_listen_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
    if (client_fd < 0) {
      if (errno == EINTR) continue;
      if (!s_running.load()) break;
      std::printf("config_server: accept failed: %s\n", std::strerror(errno));
      continue;
    }
    handleConnection(client_fd);
  }
  if (s_listen_fd >= 0) {
    ::close(s_listen_fd);
    s_listen_fd = -1;
  }
}

// -- Load persisted config on boot -------------------------------------
double readDouble(JsonVariant v, double fallback) {
  if (v.is<double>() || v.is<float>() || v.is<int>()) {
    return v.as<double>();
  }
  return fallback;
}

}  // namespace

void loadPersistedConfig() {
  std::ifstream in(kConfigPath);
  if (!in) {
    // No persisted config yet — that's fine, defaults apply. Still
    // write out the current defaults so the user can see the file
    // and edit it directly if they want.
    writeConfigJson();
    return;
  }
  std::stringstream buf;
  buf << in.rdbuf();
  const std::string raw = buf.str();

  JsonDocument doc;
  const auto err = deserializeJson(doc, raw);
  if (err) {
    std::printf("config_server: %s parse failed: %s\n", kConfigPath, err.c_str());
    return;
  }

  const double home_lat = readDouble(doc["home_lat"], services::location::lat());
  const double home_lon = readDouble(doc["home_lon"], services::location::lon());
  char home_lat_buf[32], home_lon_buf[32];
  std::snprintf(home_lat_buf, sizeof(home_lat_buf), "%.6f", home_lat);
  std::snprintf(home_lon_buf, sizeof(home_lon_buf), "%.6f", home_lon);
  services::location::saveFromStrings(home_lat_buf, home_lon_buf);

  const double metar_lat = readDouble(
      doc["metar_lat"], static_cast<double>(services::metar_config::centerLat()));
  const double metar_lon = readDouble(
      doc["metar_lon"], static_cast<double>(services::metar_config::centerLon()));
  const double metar_rad = readDouble(
      doc["metar_rad"], static_cast<double>(services::metar_config::radiusNm()));
  char m_lat[32], m_lon[32], m_rad[16];
  std::snprintf(m_lat, sizeof(m_lat), "%.6f", metar_lat);
  std::snprintf(m_lon, sizeof(m_lon), "%.6f", metar_lon);
  std::snprintf(m_rad, sizeof(m_rad), "%.1f", metar_rad);
  services::metar_config::saveFromStrings(m_lat, m_lon, m_rad);

  // focus_ring is stored as a nested JSON string; extract and pass to
  // the focus service. If missing, leave whatever init() populated.
  if (doc["focus_ring"].is<const char*>()) {
    services::focus::saveRingJson(doc["focus_ring"].as<const char*>());
  }

  std::printf("config_server: loaded %s\n", kConfigPath);
}

void start(int port) {
  if (s_running.exchange(true)) return;  // already started
  s_server_thread = std::thread([port]() { serverThreadMain(port); });
  // Detach so process exit (Ctrl-C, Cmd-Q on the SDL window) doesn't
  // abort with "libc++abi: terminating" when std::thread's destructor
  // fires on a still-joinable thread. The listen socket dies with the
  // process; no resource leaks in a one-shot local emulator.
  s_server_thread.detach();
}

bool applyPending() {
  std::vector<PendingChange> local;
  {
    std::lock_guard<std::mutex> lock(s_pending_mutex);
    if (s_pending.empty()) return false;
    local.swap(s_pending);
  }
  for (const auto& p : local) applyChangeUnlocked(p);
  // Flush the resulting state to disk so restarts remember it.
  writeConfigJson();
  return true;
}

void stop() {
  if (!s_running.exchange(false)) return;
  // Kick the accept loop out. Shutting down the socket is the least
  // fragile cross-platform way. The thread is detached (see start),
  // so no join here.
  if (s_listen_fd >= 0) ::shutdown(s_listen_fd, SHUT_RDWR);
}

}  // namespace host::config_server

#endif  // USE_NATIVE
