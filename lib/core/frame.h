#pragma once
// Frame codec for the Makera Z1 link (SPEC 8.1, 8.2).
// 0x86 0x68 | len (2, BE) | type (1) | payload (n) | crc (2, BE) | 0x55 0xAA, len = 1 + n + 2,
// crc = CRC-16/XModem over len, type, payload.
#include <cstddef>
#include <cstdint>

#include "core_config.h"

namespace frame {

constexpr uint8_t HEAD0 = 0x86;
constexpr uint8_t HEAD1 = 0x68;
constexpr uint8_t FOOT0 = 0x55;
constexpr uint8_t FOOT1 = 0xAA;
constexpr size_t OVERHEAD = 2 + 2 + 1 + 2 + 2;  // head, len, type, crc, foot

constexpr uint8_t TYPE_REALTIME = 0xA1;
constexpr uint8_t TYPE_CONSOLE  = 0xA2;
constexpr uint8_t TYPE_STATUS   = 0x81;
constexpr uint8_t TYPE_TEXT     = 0x90;

uint16_t crc16(const uint8_t* data, size_t len);
uint16_t crc16_update(uint16_t crc, const uint8_t* data, size_t len);

// Writes a complete frame to out; returns its length, 0 if out is too small or n > TX_MAX_PAYLOAD.
size_t build(uint8_t type, const uint8_t* payload, size_t n, uint8_t* out, size_t out_cap);

struct Frame {
  uint8_t type;
  uint16_t n;  // payload length
  uint8_t payload[MAX_RX_PAYLOAD];
};

// Byte-wise decoder. Frames with len < 3 or len > MAX_RX_PAYLOAD + 3 are invalid. On any error
// (invalid len, wrong CRC, wrong footer) it searches again for 0x86 0x68 starting at the byte
// after the discarded 0x86.
class Decoder {
 public:
  // Feeds bytes; calls on_frame(const Frame&) for every valid frame. Buffers partial frames.
  template <class F>
  void feed(const uint8_t* data, size_t n, F on_frame) {
    for (size_t i = 0; i < n; ++i) {
      if (used_ == 0 && data[i] != HEAD0) continue;  // skip garbage between frames
      buf_[used_++] = data[i];
      for (;;) {
        const int r = check();
        if (r == 0) break;
        if (r > 0) {
          on_frame(static_cast<const Frame&>(frame_));
          consume(frame_.n + OVERHEAD);
        } else {
          reject();
        }
      }
    }
  }
  uint32_t dropped() const { return dropped_; }

 private:
  static constexpr size_t BUF_CAP = MAX_RX_PAYLOAD + OVERHEAD;
  uint8_t buf_[BUF_CAP];  // candidate bytes, buf_[0] == HEAD0 whenever used_ > 0
  size_t used_ = 0;
  Frame frame_{};
  uint32_t dropped_ = 0;

  // -1 candidate invalid, 0 need more bytes (or buffer empty), 1 complete valid frame in frame_.
  int check();
  void consume(size_t k);  // drop k leading bytes, then skip to the next HEAD0
  void reject();           // drop the leading HEAD0 and resync
};

}  // namespace frame
