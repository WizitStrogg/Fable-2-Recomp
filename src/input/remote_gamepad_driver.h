// fable_2 - Remote (AI) gamepad driver
//
// Synthetic gamepad driven by the remote control server (see
// src/input/remote_control_server.h and plans/ai-remote-input-control.md). It
// presents the latest InputSnapshot published into the shared
// InputStateStore as ordinary XInput state, so the guest sees a normal
// gamepad: works in menus, cutscenes, and gameplay alike.
//
// Unlike the keyboard driver (src/input/keyboard_gamepad.h) this one is NOT gated
// on window focus: the AI harness must be able to drive the game even while
// the window is not in the foreground. It still OR-merges with the
// keyboard/physical pads (synthetic devices route to guest user 0 by the
// default assignment), so a human can keep playing while inputs are injected.
//
// `enabled` false -> the driver emits an all-zero state (indistinguishable
// from no input). We deliberately keep the device continuously *connected*
// rather than connect/disconnecting it: some game code keys off device
// arrival/edge state, and the plan is to never change connectivity except
// via the server's enable/disable command (which is a state change, not a
// device-removal event).

#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

#include <rex/input/input.h>
#include <rex/input/input_driver.h>
#include <rex/input/input_system.h>

#include "remote_input_state.h"

namespace fable2::remote {

using rex::X_RESULT;
using rex::X_STATUS;
using rex::input::DeviceId;
using rex::input::DeviceInfo;
using rex::input::X_INPUT_CAPABILITIES;
using rex::input::X_INPUT_KEYSTROKE;
using rex::input::X_INPUT_STATE;
using rex::input::X_INPUT_VIBRATION;
using rex::input::XINPUT_DEVTYPE_GAMEPAD;
using rex::input::X_INPUT_CAPS_FFB_SUPPORTED;

class GamepadDriver final : public rex::input::InputDriver {
 public:
  // InputDriver's constructor is protected, so expose a public one.
  GamepadDriver(rex::ui::Window* window, size_t window_z_order,
                InputStateStore* state, std::atomic<bool>* enabled)
      : rex::input::InputDriver(window, window_z_order),
        state_(state),
        enabled_(enabled) {}

  X_STATUS Setup() override { return X_STATUS_SUCCESS; }

  void EnumerateDevices(std::vector<DeviceInfo>& out) override {
    DeviceInfo info;
    info.id = kDeviceId;
    info.name = "Remote (AI) gamepad";
    info.guid = "fable2-remote-gamepad";
    info.synthetic = true;  // routed to guest user 0 by the default assignment
    out.push_back(info);
  }

  X_RESULT GetDeviceState(DeviceId id, X_INPUT_STATE* out_state) override {
    if (id != kDeviceId) return X_ERROR_DEVICE_NOT_CONNECTED;

    InputSnapshot s;
    if (enabled_ && enabled_->load(std::memory_order_relaxed))
      s = state_->Snapshot();  // disabled -> all-zero state

    // Advance packet_number only when the emitted state actually changes:
    // guest code may use it for edge detection (same convention as the
    // keyboard driver).
    if (!have_prev_ || prev_ != s) {
      ++packet_number_;
      prev_ = s;
      have_prev_ = true;
    }

    out_state->packet_number.set(packet_number_);
    out_state->gamepad.buttons.set(s.buttons);
    out_state->gamepad.left_trigger = s.left_trigger;
    out_state->gamepad.right_trigger = s.right_trigger;
    out_state->gamepad.thumb_lx.set(ToI16(s.stk_lx));
    out_state->gamepad.thumb_ly.set(ToI16(s.stk_ly));
    out_state->gamepad.thumb_rx.set(ToI16(s.stk_rx));
    out_state->gamepad.thumb_ry.set(ToI16(s.stk_ry));
    return X_ERROR_SUCCESS;
  }

  X_RESULT GetDeviceCapabilities(DeviceId id, uint32_t flags,
                                 X_INPUT_CAPABILITIES* out_caps) override {
    (void)flags;
    if (id != kDeviceId) return X_ERROR_DEVICE_NOT_CONNECTED;
    *out_caps = X_INPUT_CAPABILITIES{};
    out_caps->type = XINPUT_DEVTYPE_GAMEPAD;
    out_caps->sub_type = 0x05;  // XINPUT_SUBTYPE_GAMEPAD
    out_caps->flags.set(X_INPUT_CAPS_FFB_SUPPORTED);
    return X_ERROR_SUCCESS;
  }

  X_RESULT SetDeviceVibration(DeviceId id, X_INPUT_VIBRATION* vibration) override {
    (void)vibration;
    return id == kDeviceId ? X_ERROR_SUCCESS : X_ERROR_DEVICE_NOT_CONNECTED;
  }

  // Button edges as XInput pad keystrokes (VK_PAD_*). Some screens read
  // keystrokes instead of the pad state: Fable II's New Game, after the
  // storage step, waits for a keystroke and ignores a state-only press.
  // Edges are taken against the buttons seen at the previous keystroke poll,
  // so nothing piles up while the game is not asking for keystrokes.
  X_RESULT GetDeviceKeystroke(DeviceId id, uint32_t flags,
                              X_INPUT_KEYSTROKE* out_keystroke) override {
    (void)flags;
    if (id != kDeviceId) return X_ERROR_DEVICE_NOT_CONNECTED;
    uint16_t now = 0;
    if (enabled_ && enabled_->load(std::memory_order_relaxed))
      now = state_->Snapshot().buttons;
    const uint16_t changed = now ^ ks_buttons_;
    for (const auto& m : kPadKeys) {
      if (changed & m.bit) ks_queue_.push_back({m.vk, (now & m.bit) != 0});
    }
    ks_buttons_ = now;
    if (ks_queue_.empty()) return X_ERROR_EMPTY;
    const KeyEdge e = ks_queue_.front();
    ks_queue_.erase(ks_queue_.begin());
    if (out_keystroke) {
      *out_keystroke = X_INPUT_KEYSTROKE{};
      out_keystroke->virtual_key.set(e.vk);
      out_keystroke->flags.set(e.down ? rex::input::X_INPUT_KEYSTROKE_KEYDOWN
                                      : rex::input::X_INPUT_KEYSTROKE_KEYUP);
    }
    return X_ERROR_SUCCESS;
  }

 private:
  static int16_t ToI16(int32_t v) {
    constexpr int32_t kMax = 32767;
    if (v > kMax) return kMax;
    if (v < -kMax) return -kMax;
    return static_cast<int16_t>(v);
  }

  // Synthetic device id. MUST NOT collide with the SDL driver's sequential
  // ids (they start at 1) or the keyboard driver's 1<<60 (see
  // src/input/keyboard_gamepad.h): InputSystem::DriverForDevice() resolves an id by
  // first match.
  static constexpr DeviceId kDeviceId{1ull << 61};

  // XINPUT_GAMEPAD button bit -> VK_PAD_* (triggers and sticks send none).
  struct PadKey {
    uint16_t bit, vk;
  };
  static constexpr PadKey kPadKeys[] = {
      {0x1000, 0x5800}, {0x2000, 0x5801}, {0x4000, 0x5802}, {0x8000, 0x5803},  // A B X Y
      {0x0200, 0x5804}, {0x0100, 0x5805},                                      // RB LB
      {0x0001, 0x5810}, {0x0002, 0x5811}, {0x0004, 0x5812}, {0x0008, 0x5813},  // D-pad
      {0x0010, 0x5814}, {0x0020, 0x5815}, {0x0040, 0x5816}, {0x0080, 0x5817},  // Start Back L3 R3
  };
  struct KeyEdge {
    uint16_t vk;
    bool down;
  };
  uint16_t ks_buttons_ = 0;
  std::vector<KeyEdge> ks_queue_;

  InputStateStore* state_;
  std::atomic<bool>* enabled_;
  uint32_t packet_number_ = 0;
  InputSnapshot prev_{};
  bool have_prev_ = false;
};

}  // namespace fable2::remote
