#include "status_json.h"

#include <cstring>

#include "fixed_fmt.h"

namespace {

// Appends to a fixed buffer; once full, every further write is ignored and ok() stays false.
class Writer {
 public:
  Writer(char* out, size_t cap) : out_(out), cap_(cap) {}

  void raw(const char* s) {
    while (*s != '\0') put(*s++);
  }

  void put(char c) {
    if (out_ == nullptr || n_ + 1 >= cap_) {
      ok_ = false;
      return;
    }
    out_[n_++] = c;
  }

  void str(const char* s) {
    put('"');
    if (s != nullptr) {
      for (; *s != '\0'; ++s) {
        const unsigned char c = static_cast<unsigned char>(*s);
        if (c == '"' || c == '\\') {
          put('\\');
          put(static_cast<char>(c));
        } else if (c < 0x20) {
          static const char kHex[] = "0123456789abcdef";
          raw("\\u00");
          put(kHex[c >> 4]);
          put(kHex[c & 0xF]);
        } else {
          put(static_cast<char>(c));
        }
      }
    }
    put('"');
  }

  void u32(uint32_t v) {
    char tmp[11];
    size_t k = 0;
    do {
      tmp[k++] = static_cast<char>('0' + v % 10);
      v /= 10;
    } while (v > 0);
    while (k > 0) put(tmp[--k]);
  }

  void i32(int32_t v) {
    if (v < 0) {
      put('-');
      u32(static_cast<uint32_t>(-static_cast<int64_t>(v)));
    } else {
      u32(static_cast<uint32_t>(v));
    }
  }

  void boolean(bool b) { raw(b ? "true" : "false"); }

  void key(const char* k) {
    if (!first_) put(',');
    first_ = false;
    str(k);
    put(':');
  }

  void begin_object() {
    put('{');
    first_ = true;
  }
  void end_object() {
    put('}');
    first_ = false;
  }

  size_t finish() {
    if (!ok_ || out_ == nullptr || n_ >= cap_) return 0;
    out_[n_] = '\0';
    return n_;
  }

 private:
  char* out_;
  size_t cap_;
  size_t n_ = 0;
  bool ok_ = true;
  bool first_ = true;
};

void positions(Writer& w, const char* k, const int32_t* um, bool has_a) {
  w.key(k);
  w.put('[');
  const int count = has_a ? 4 : 3;
  for (int i = 0; i < count; ++i) {
    if (i > 0) w.put(',');
    char buf[16];
    if (format_um(um[i], buf, sizeof(buf)) == 0) buf[0] = '\0';
    w.raw(buf);
  }
  w.put(']');
}

}  // namespace

const char* machine_state_name(MachineState s) {
  switch (s) {
    case MachineState::Idle: return "Idle";
    case MachineState::Run: return "Run";
    case MachineState::Hold: return "Hold";
    case MachineState::Pause: return "Pause";
    case MachineState::Alarm: return "Alarm";
    case MachineState::Home: return "Home";
    case MachineState::Tool: return "Tool";
    case MachineState::Sleep: return "Sleep";
    case MachineState::Wait: return "Wait";
    case MachineState::Unknown: return "Unknown";
  }
  return "Unknown";
}

size_t format_status_json(const StatusView& v, char* out, size_t cap) {
  Writer w(out, cap);
  w.begin_object();
  w.key("fw"); w.str(v.fw);
  w.key("uptime_s"); w.u32(v.uptime_s);
  w.key("link"); w.str(v.link);
  w.key("last_error"); w.str(v.last_error);
  w.key("wifi"); w.boolean(v.wifi);
  w.key("rssi"); if (v.wifi) w.i32(v.rssi_dbm); else w.raw("null");
  w.key("ip"); w.str(v.ip);
  w.key("have_status"); w.boolean(v.have_status);
  if (v.have_status) {
    w.key("state"); w.str(machine_state_name(v.status.state));
    w.key("playing"); w.boolean(v.status.playing);
    w.key("halted"); w.boolean(v.status.halted);
    w.key("inch_mode"); w.boolean(v.status.inch_mode);
    w.key("has_a"); w.boolean(v.status.has_a);
    positions(w, "wpos", v.status.wpos_um, v.status.has_a);
    positions(w, "mpos", v.status.mpos_um, v.status.has_a);
    w.key("status_age_ms"); w.u32(v.status_age_ms);
  }
  w.key("gate"); w.str(v.gate);
  w.key("axis"); w.str(v.axis);
  w.key("axis_a_enabled"); w.boolean(v.axis_a_enabled);
  w.key("session_active"); w.boolean(v.session_active);
  w.key("settings_allowed"); w.boolean(v.settings_allowed);
  w.key("bat_mv"); w.u32(v.bat_mv);
  w.key("bat_percent"); w.u32(v.bat_percent);
  w.key("bat_absent"); w.boolean(v.bat_absent);
  w.key("bat_warn"); w.boolean(v.bat_warn);
  w.key("alarm"); if (v.alarm != nullptr) w.str(v.alarm); else w.raw("null");
  w.key("stats");
  if (v.have_stats) {
    w.begin_object();
    w.key("status_ok"); w.u32(v.stats.status_ok);
    w.key("status_bad"); w.u32(v.stats.status_bad);
    w.key("text_frames"); w.u32(v.stats.text_frames);
    w.key("other_frames"); w.u32(v.stats.other_frames);
    w.key("max_gap_ms"); w.u32(v.stats.max_gap_ms);
    w.key("gaps_over_limit"); w.u32(v.stats.gaps_over_limit);
    w.key("dropped"); w.u32(v.stats.dropped);
    w.key("loop_max_ms"); w.u32(v.stats.loop_max_ms);
    w.key("loop_mean_us"); w.u32(v.stats.loop_mean_us);
    w.key("web_max_us"); w.u32(v.stats.web_max_us);
    w.end_object();
  } else {
    w.raw("null");
  }
  w.end_object();
  return w.finish();
}
