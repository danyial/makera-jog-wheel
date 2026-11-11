#include "board_rev.h"

BoardRevAction board_rev_action(bool stored_valid, uint8_t stored, uint8_t compiled) {
  if (!stored_valid) return BoardRevAction::Store;
  return stored == compiled ? BoardRevAction::Run : BoardRevAction::WrongBoard;
}

BoardRevConfirm::BoardRevConfirm(uint32_t hold_ms) : hold_ms_(hold_ms) {}

bool BoardRevConfirm::update(uint32_t now_ms, bool pressed) {
  if (!pressed) {
    armed_ = true;
    holding_ = false;
    fired_ = false;
    return false;
  }
  if (!armed_) return false;
  if (!holding_) {
    holding_ = true;
    since_ms_ = now_ms;
  }
  if (!fired_ && static_cast<uint32_t>(now_ms - since_ms_) >= hold_ms_) {
    fired_ = true;
    return true;
  }
  return false;
}
