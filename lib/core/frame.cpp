#include "frame.h"

#include <cstring>

namespace frame {

uint16_t crc16_update(uint16_t crc, const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    crc = static_cast<uint16_t>(crc ^ (static_cast<uint16_t>(data[i]) << 8));
    for (int b = 0; b < 8; ++b) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

uint16_t crc16(const uint8_t* data, size_t len) { return crc16_update(0x0000, data, len); }

size_t build(uint8_t type, const uint8_t* payload, size_t n, uint8_t* out, size_t out_cap) {
  if (n > TX_MAX_PAYLOAD) return 0;
  if (payload == nullptr && n > 0) return 0;
  const size_t total = n + OVERHEAD;
  if (out == nullptr || out_cap < total) return 0;
  const uint16_t len = static_cast<uint16_t>(1 + n + 2);
  out[0] = HEAD0;
  out[1] = HEAD1;
  out[2] = static_cast<uint8_t>(len >> 8);
  out[3] = static_cast<uint8_t>(len & 0xFF);
  out[4] = type;
  if (n > 0) std::memcpy(out + 5, payload, n);
  const uint16_t crc = crc16(out + 2, 3 + n);
  out[5 + n] = static_cast<uint8_t>(crc >> 8);
  out[6 + n] = static_cast<uint8_t>(crc & 0xFF);
  out[7 + n] = FOOT0;
  out[8 + n] = FOOT1;
  return total;
}

int Decoder::check() {
  if (used_ == 0) return 0;
  if (used_ >= 2 && buf_[1] != HEAD1) return -1;
  if (used_ < 4) return 0;
  const uint16_t len = static_cast<uint16_t>((buf_[2] << 8) | buf_[3]);
  if (len < 3 || len > MAX_RX_PAYLOAD + 3) return -1;
  const size_t n = static_cast<size_t>(len) - 3;
  if (used_ < n + OVERHEAD) return 0;
  const uint16_t crc = crc16(buf_ + 2, 3 + n);
  const uint16_t got = static_cast<uint16_t>((buf_[5 + n] << 8) | buf_[6 + n]);
  if (crc != got) return -1;
  if (buf_[7 + n] != FOOT0 || buf_[8 + n] != FOOT1) return -1;
  frame_.type = buf_[4];
  frame_.n = static_cast<uint16_t>(n);
  if (n > 0) std::memcpy(frame_.payload, buf_ + 5, n);
  return 1;
}

void Decoder::consume(size_t k) {
  if (k > used_) k = used_;
  size_t skip = k;
  while (skip < used_ && buf_[skip] != HEAD0) ++skip;
  std::memmove(buf_, buf_ + skip, used_ - skip);
  used_ -= skip;
}

void Decoder::reject() {
  if (used_ >= 2 && buf_[1] == HEAD1) ++dropped_;  // only count real frame candidates
  consume(1);
}

}  // namespace frame
