#include "unlock.h"

#include "core_config.h"

static_assert(UNLOCK_HOLD_MS > 0 && UNLOCK_SHOW_MS > 0, "unlock times must be > 0");

namespace {

bool alarm_of(const UnlockInputs& in) {
  return in.have_status && (in.status.state == MachineState::Alarm || in.status.halted);
}

}  // namespace

const char* UnlockRequest::blocked(const UnlockInputs& in) {
  if (!in.normal_mode) return "Betriebsart";
  if (in.ota_updating) return "OTA";
  if (!in.online) return "keine Verbindung";
  if (!in.have_status || in.status_age_ms > SEND_MAX_AGE_MS) return "kein frischer Status";
  if (!alarm_of(in)) return "kein Alarm";
  return nullptr;
}

UnlockTick UnlockRequest::tick(uint32_t now_ms, const UnlockInputs& in) {
  UnlockTick r{false, UnlockView::None, 0, nullptr};
  const char* why = blocked(in);

  // "Entsperren..." after a send: until a status without Alarm, the link is gone, or UNLOCK_SHOW_MS passed.
  if (showing_sent_ && (!in.online || (in.have_status && !alarm_of(in)) ||
                        static_cast<uint32_t>(now_ms - sent_ms_) >= UNLOCK_SHOW_MS)) {
    showing_sent_ = false;
  }

  if (holding_) {
    // A stale status alone does not abort (WiFi stall): $X moves nothing, and it is sent only on a fresh status
    // with Alarm (why == nullptr). Everything else that makes the unlock wrong or unexpected aborts.
    const bool fresh = in.have_status && in.status_age_ms <= SEND_MAX_AGE_MS;
    const char* abort = nullptr;
    if (!in.normal_mode) abort = "Betriebsart";
    else if (in.ota_updating) abort = "OTA";
    else if (!in.online) abort = "keine Verbindung";
    else if (!in.have_status) abort = "kein Status";
    else if (fresh && !alarm_of(in)) abort = "kein Alarm";
    else if (in.new_message) abort = "neue Meldung";
    else if ((in.pressed & bit_) == 0) abort = "losgelassen";
    if (abort != nullptr) {
      holding_ = false;
      r.aborted = abort;
    } else {
      const uint32_t held = static_cast<uint32_t>(now_ms - hold_since_ms_);
      const bool interval_ok = !have_sent_ || static_cast<uint32_t>(now_ms - sent_ms_) >= UNLOCK_MIN_INTERVAL_MS;
      if (held >= UNLOCK_HOLD_MS && interval_ok && why == nullptr) {
        holding_ = false;  // spent: the next send needs release and a new press
        have_sent_ = true;
        sent_ms_ = now_ms;
        showing_sent_ = true;
        r.send = true;
      } else {
        r.view = UnlockView::Holding;
        // Waiting (stale status, interval): 999, the bar is full only with the send.
        r.permille = held >= UNLOCK_HOLD_MS ? 999 : static_cast<uint16_t>(static_cast<uint64_t>(held) * 1000 /
                                                                            UNLOCK_HOLD_MS);
        return r;
      }
    }
  }

  if (showing_sent_) {
    r.view = UnlockView::Sent;
    return r;
  }
  if (why != nullptr) return r;

  // Start: a press edge (never a level held from before) while the hint is on the display. Order X, Y, Z, A.
  if (!r.send && in.hint_visible) {
    const uint8_t start = static_cast<uint8_t>(in.edges & in.pressed);
    for (uint8_t b = 0; b < 4; ++b) {
      const uint8_t bit = static_cast<uint8_t>(1u << b);
      if ((start & bit) != 0) {
        holding_ = true;
        bit_ = bit;
        hold_since_ms_ = now_ms;
        r.view = UnlockView::Holding;
        r.permille = 0;
        return r;
      }
    }
  }
  r.view = UnlockView::Hint;
  return r;
}
