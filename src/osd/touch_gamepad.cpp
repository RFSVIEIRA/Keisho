// ██╗  ██╗███████╗██╗███████╗██╗  ██╗ ██████╗
// ██║ ██╔╝██╔════╝██║██╔════╝██║  ██║██╔═══██╗
// █████╔╝ █████╗  ██║███████╗███████║██║   ██║
// ██╔═██╗ ██╔══╝  ██║╚════██║██╔══██║██║   ██║
// ██║  ██╗███████╗██║███████║██║  ██║╚██████╔╝
// ╚═╝  ╚═╝╚══════╝╚═╝╚══════╝╚═╝  ╚═╝ ╚═════╝
//            継 承  ( k e i s h ō )
//
#include "osd/touch_gamepad.h"

#include "osd/i18n.h"
#include "osd/input.h"
#include "rom/game.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sm2::osd {
namespace {

using i18n::tr;

[[nodiscard]] bool has_flag(const Gui::GameFamily* family, rom::InputFlags flag)
{
    return family != nullptr
        && rom::has_input(static_cast<rom::InputFlags>(family->inputs), flag);
}

[[nodiscard]] bool is_twin_stick(const Gui::GameFamily* family)
{
    return has_flag(family, rom::InputFlags::TwinJoysticks)
        || (family != nullptr && family->key == "von");
}

[[nodiscard]] bool is_fishing(const Gui::GameFamily* family)
{
    return has_flag(family, rom::InputFlags::Fishing);
}

[[nodiscard]] bool is_magtruck(const Gui::GameFamily* family)
{
    return has_flag(family, rom::InputFlags::MagTruck);
}

[[nodiscard]] bool is_gun_game(const Gui::GameFamily* family)
{
    return family != nullptr && family->gun;
}

[[nodiscard]] bool use_direct_touch_gun(const Config& config, const Gui::GameFamily* family)
{
    return is_gun_game(family) && config.touch_gun_aim == Config::TouchGunAim::Direct;
}

[[nodiscard]] bool is_ski_or_board(const Gui::GameFamily* family)
{
    if (family == nullptr) return false;
    return has_flag(family, rom::InputFlags::Ski)
        || family->key == "skisuprg"
        || family->key == "segawski"
        || family->key == "topskatr";
}

[[nodiscard]] bool is_flight_stick(const Gui::GameFamily* family)
{
    if (family == nullptr) return false;
    return has_flag(family, rom::InputFlags::AnalogJoystick)
        || family->key == "skytargt"
        || family->key == "wingwar"
        || family->key == "swa";
}

[[nodiscard]] bool is_racer(const Gui::GameFamily* family)
{
    if (family == nullptr) return false;
    if (family->key == "desert" || is_fishing(family) || is_magtruck(family)
        || is_ski_or_board(family) || is_flight_stick(family) || is_gun_game(family)
        || is_twin_stick(family) || family->bat) {
        return false;
    }
    return family->accel || family->brake
        || family->steer == Gui::GameFamily::Steer::Wheel
        || family->steer == Gui::GameFamily::Steer::Handlebar;
}

[[nodiscard]] bool fighting_m12(const Gui::GameFamily* family)
{
    if (family == nullptr) return false;
    static constexpr const char* kFighters[] = {
        "vf", "vf2", "fvipers", "doa", "lastbrnx", "schamp", "airwlkrs",
    };
    for (const char* k : kFighters) {
        if (family->key == k) return true;
    }
    return false;
}

void trigger_pad_binding(Input* input, s32 binding)
{
    if (binding < 0) return;
    if (binding >= Config::kPadAxisMinus) {
        input->set_virtual_axis(static_cast<SDL_GamepadAxis>(binding - Config::kPadAxisMinus), -32767);
    } else if (binding >= Config::kPadAxisPlus) {
        input->set_virtual_axis(static_cast<SDL_GamepadAxis>(binding - Config::kPadAxisPlus), 32767);
    } else {
        input->set_virtual_button(static_cast<SDL_GamepadButton>(binding), true);
    }
}

void trigger_axis_role(Input* input, const Config::PadLayout& layout,
                       Config::PadAxisRole role, SDL_GamepadAxis fallback)
{
    const auto r = static_cast<usize>(role);
    if (layout.axis_buttons[0][r] >= 0) {
        trigger_pad_binding(input, layout.axis_buttons[0][r]);
        return;
    }
    const s32 bound = layout.axes[0][r];
    if (bound == Config::kPadAxisNone) return;
    const SDL_GamepadAxis axis = bound >= 0 ? static_cast<SDL_GamepadAxis>(bound) : fallback;
    const bool inv      = layout.axis_invert[0][r];
    const bool is_stick = axis == SDL_GAMEPAD_AXIS_LEFTX || axis == SDL_GAMEPAD_AXIS_LEFTY
                       || axis == SDL_GAMEPAD_AXIS_RIGHTX || axis == SDL_GAMEPAD_AXIS_RIGHTY;
    input->set_virtual_axis(axis, inv ? (is_stick ? static_cast<s16>(-32767) : static_cast<s16>(0))
                                      : static_cast<s16>(32767));
}

[[nodiscard]] ImU32 rgba_u32(u8 r, u8 g, u8 b, float alpha_norm)
{
    const auto a = static_cast<u32>(std::clamp(alpha_norm, 0.0f, 1.0f) * 255.0f + 0.5f);
    return IM_COL32(r, g, b, a);
}

}  // namespace

bool TouchGamepad::is_active(const Config& config, const Input& input)
{
#if !defined(__ANDROID__)
    static_cast<void>(input);
#endif
    switch (config.virtual_gamepad) {
        case Config::VirtualGamepadMode::On:   return true;
        case Config::VirtualGamepadMode::Off:  return false;
        case Config::VirtualGamepadMode::Auto:
#if defined(__ANDROID__)
            return !input.has_controller();
#else
            return false;
#endif
    }
    return false;
}

void TouchGamepad::reset(Input* input)
{
    m_fingers.clear();
    m_gun_finger_down = false;
    m_gun_aim_engaged = false;
    m_gun_aim_finger  = 0;
    m_gun_aim_nx      = 0.5f;
    m_gun_aim_ny      = 0.5f;
    m_service_pulse = 0;
    m_test_pulse    = 0;
    if (input != nullptr) {
        input->clear_virtual_pad();
        input->set_suppress_touch_pointer(false);
    }
}

bool TouchGamepad::take_menu_request()
{
    const bool out   = m_menu_requested;
    m_menu_requested = false;
    return out;
}

bool TouchGamepad::take_fps_toggle_request()
{
    const bool out         = m_fps_toggle_requested;
    m_fps_toggle_requested = false;
    return out;
}

bool TouchGamepad::take_exit_request()
{
    const bool out   = m_exit_requested;
    m_exit_requested = false;
    return out;
}

TouchGamepad::StateAction TouchGamepad::take_state_request()
{
    const StateAction out = m_state_request;
    m_state_request       = {};
    return out;
}

bool TouchGamepad::hit_test(const Widget& w, float px, float py)
{
    if (w.circle) {
        const float dx = px - w.cx;
        const float dy = py - w.cy;
        // Slight generous touch radius (+12%) for thumb comfort.
        const float r  = w.w * 1.12f;
        return (dx * dx + dy * dy) <= (r * r);
    }
    const float hw = w.w * 0.52f;
    const float hh = w.h * 0.56f;
    return px >= (w.cx - hw) && px <= (w.cx + hw)
        && py >= (w.cy - hh) && py <= (w.cy + hh);
}

std::vector<TouchGamepad::Widget> TouchGamepad::build_layout(
    const Config& config, const Gui::GameFamily* family,
    bool pad_active, float w, float h) const
{
    std::vector<Widget> out;
    if (w <= 0.0f || h <= 0.0f) {
        return out;
    }

    const float base  = std::min(w, h);
    const float scale = static_cast<float>(std::clamp(config.virtual_gamepad_scale, 60u, 150u)) / 100.0f;
    const float u     = (base / 720.0f) * scale;
    const float hu    = base / 720.0f;

    float inset_l = 0.0f;
    float inset_t = 0.0f;
    float inset_r = 0.0f;
    float inset_b = 0.0f;
#if defined(__ANDROID__)
    if (m_window != nullptr) {
        SDL_Rect safe{};
        int      win_w = 0;
        int      win_h = 0;
        SDL_GetWindowSize(m_window, &win_w, &win_h);
        if (win_w > 0 && win_h > 0 && SDL_GetWindowSafeArea(m_window, &safe) && safe.w > 0 && safe.h > 0) {
            const float sx = w / static_cast<float>(win_w);
            const float sy = h / static_cast<float>(win_h);
            inset_l = std::max(0.0f, static_cast<float>(safe.x) * sx);
            inset_t = std::max(0.0f, static_cast<float>(safe.y) * sy);
            inset_r = std::max(0.0f, static_cast<float>(win_w - (safe.x + safe.w)) * sx);
            inset_b = std::max(0.0f, static_cast<float>(win_h - (safe.y + safe.h)) * sy);
        }
    }
    const float safe_left   = std::max({inset_l + 14.0f * hu, 34.0f * hu, w * 0.032f});
    const float safe_right  = std::max({inset_r + 14.0f * hu, 34.0f * hu, w * 0.032f});
    const float safe_top    = std::max({inset_t + 12.0f * hu, 22.0f * hu, h * 0.030f});
    // Keep the bottom bar well clear of both gesture handles and 3-button
    // Android navigation bars even when immersive mode reports zero insets.
    const float safe_bottom = std::max({inset_b + 20.0f * hu, 72.0f * hu, h * 0.095f});
#else
    const float safe_left   = std::max(inset_l + 34.0f * hu, w * 0.032f);
    const float safe_right  = std::max(inset_r + 34.0f * hu, w * 0.032f);
    const float safe_top    = std::max(inset_t + 20.0f * hu, h * 0.028f);
    const float safe_bottom = std::max(inset_b + 20.0f * hu, h * 0.028f);
#endif

    const float pill_w  = std::max(90.0f, 102.0f * hu);
    const float pill_h  = std::max(38.0f, 44.0f * hu);
    const float pill_y  = safe_top + pill_h * 0.5f;
    const float row2_y  = pill_y + pill_h + 10.0f * hu;
    const float left_x  = safe_left + pill_w * 0.5f;
    const float right_x = w - safe_right - pill_w * 0.5f;

    // Top-left: FPS pill + SETTINGS/MENU pill (always available to toggle HUD/menu).
    out.push_back(Widget{ControlId::HudFps, left_x, pill_y, pill_w, pill_h, tr("FPS"), nullptr, false, config.show_fps, false});
    out.push_back(Widget{ControlId::Menu, left_x + pill_w * 0.10f, row2_y, pill_w * 1.22f, pill_h, tr("SETTINGS"), nullptr, false, false, false});

    // Top-right: EXIT pill.
    out.push_back(Widget{ControlId::HudExit, right_x, pill_y, pill_w, pill_h, tr("EXIT"), nullptr, false, false, true});

    // Top-center: SERVICE and TEST operator switches (always live, even with physical pad).
    {
        const float left_max  = left_x + pill_w * 0.5f;
        const float right_min = right_x - pill_w * 0.5f;
        const float avail     = std::max(0.0f, right_min - left_max - 18.0f * hu);
        const float op_cx     = (left_max + right_min) * 0.5f;
        const float op_gap    = 10.0f * hu;
        float       svc_w     = pill_w * 1.12f;
        float       tst_w     = pill_w * 0.96f;
        const bool  top_row   = (svc_w + tst_w + op_gap) <= avail;
        const float op_y      = top_row ? pill_y : row2_y;
        if (!top_row && avail > 120.0f) {
            const float fit = std::min(1.0f, avail / (svc_w + tst_w + op_gap));
            svc_w *= fit;
            tst_w *= fit;
        }
        const float svc_x = op_cx - (op_gap + tst_w) * 0.5f;
        const float tst_x = op_cx + (op_gap + svc_w) * 0.5f;
        out.push_back(Widget{ControlId::HudService, svc_x, op_y, svc_w, pill_h, tr("SERVICE"), nullptr, false, false, false});
        out.push_back(Widget{ControlId::HudTest,    tst_x, op_y, tst_w, pill_h, tr("TEST"),    nullptr, false, false, false});
    }

    if (!pad_active) {
        return out;
    }

    // Bottom-center 5-pill bar: COIN | SAVE | SLOT N | LOAD | START
    const float bar_scale  = std::clamp(0.75f + 0.25f * scale, 0.85f, 1.25f);
    const float inner_w    = std::max(240.0f, w - safe_left - safe_right);
    const float bar_h      = std::max(52.0f, 58.0f * hu * bar_scale);
    const float bar_w      = std::clamp(inner_w * 0.098f, 94.0f, 146.0f * hu);
    const float bar_gap    = bar_w + 14.0f * hu;
    const float bar_y      = h - safe_bottom - bar_h * 0.5f;
    const float bar_cx     = safe_left + inner_w * 0.5f;
    const float bar_half_w = bar_gap * 2.0f + bar_w * 0.5f;
    std::snprintf(m_slot_label, sizeof m_slot_label, "%s %d", tr("SLOT"), m_active_slot);
    const auto role_u8 = [](Config::PadRole r) { return static_cast<u8>(r); };
    out.push_back(Widget{ControlId::Coin,    bar_cx - bar_gap * 2.0f, bar_y, bar_w, bar_h, tr("COIN"),  nullptr, false, false, false,
                         ActionTarget::PadRole, role_u8(Config::PadRole::Coin)});
    out.push_back(Widget{ControlId::HudSave, bar_cx - bar_gap * 1.0f, bar_y, bar_w, bar_h, tr("SAVE"),  nullptr, false, false, false});
    out.push_back(Widget{ControlId::HudSlot, bar_cx,                  bar_y, bar_w, bar_h, m_slot_label, nullptr, false, false, false});
    out.push_back(Widget{ControlId::HudLoad, bar_cx + bar_gap * 1.0f, bar_y, bar_w, bar_h, tr("LOAD"),  nullptr, false, false, false});
    out.push_back(Widget{ControlId::Start,   bar_cx + bar_gap * 2.0f, bar_y, bar_w, bar_h, tr("START"), nullptr, false, false, false,
                         ActionTarget::PadRole, role_u8(Config::PadRole::Start)});

    const float stick_r  = 90.0f * u;
    const float margin_x = std::max({safe_left, safe_right, 38.0f * u});
    // On widescreen displays, side controls sit alongside the centered bar; on
    // narrow displays, lift them cleanly above the bottom bar.
    const bool  direct_gun  = use_direct_touch_gun(config, family);
    const float side_need   = direct_gun ? (2.0f * 42.0f * u) : std::max(stick_r * 2.0f, 224.0f * u);
    const bool  sides_clear = (bar_cx - bar_half_w) >= (margin_x + side_need + 18.0f * hu)
                           && (w - margin_x - side_need - 18.0f * hu) >= (bar_cx + bar_half_w);
    const float margin_y = sides_clear
        ? std::max(safe_bottom, 44.0f * u)
        : std::max(safe_bottom + bar_h + 14.0f * hu, 44.0f * u);

    // Light-gun games in Direct mode: keep center screen open for direct touch aim & fire;
    // show Reload/Missile and Fire at lower corners above the bar.
    // In Stick mode, fall through so the user gets a fixed corner analog stick + Fire/Reload.
    if (direct_gun) {
        const float r_btn = 42.0f * u;
        out.push_back(Widget{ControlId::BtnB, margin_x + r_btn, h - margin_y - r_btn, r_btn, 0.0f,
                             family->missile ? tr("MSL") : tr("RELOAD"), nullptr, true, false, false,
                             ActionTarget::PadRole, role_u8(Config::PadRole::Button2)});
        out.push_back(Widget{ControlId::BtnA, w - margin_x - r_btn, h - margin_y - r_btn, r_btn, 0.0f,
                             tr("FIRE"), nullptr, true, true, false,
                             ActionTarget::PadRole, role_u8(Config::PadRole::Button1)});
        return out;
    }

    // Left-hand floating analog stick / D-pad zone.
    const float stick_x = margin_x + stick_r;
    const float stick_y = h - margin_y - stick_r;
    out.push_back(Widget{ControlId::LeftStick, stick_x, stick_y, stick_r, 0.0f, "", nullptr, true, false, false});

    struct PillSpec {
        ControlId    id;
        const char*  label;
        bool         accent;
        ActionTarget target;
        u8           code;
    };
    const auto add_pill_row = [&](float row_cx, float row_y, float span_w, float row_h,
                                  const PillSpec* items, size_t count) {
        if (count == 0) return;
        const float gap = count >= 4 ? 6.0f * u : count == 3 ? 8.0f * u : 12.0f * u;
        float       pw  = (span_w - gap * static_cast<float>(count - 1)) / static_cast<float>(count);
        pw              = std::clamp(pw, 44.0f * u, 92.0f * u);
        const float tot = pw * static_cast<float>(count) + gap * static_cast<float>(count - 1);
        float       px  = row_cx - tot * 0.5f + pw * 0.5f;
        for (size_t i = 0; i < count; ++i) {
            out.push_back(Widget{items[i].id, px, row_y, pw, row_h, items[i].label, nullptr,
                                 false, items[i].accent, false, items[i].target, items[i].code});
            px += pw + gap;
        }
    };
    const auto raw_btn_u8  = [](SDL_GamepadButton b) { return static_cast<u8>(b); };
    const auto raw_axis_u8 = [](SDL_GamepadAxis a)   { return static_cast<u8>(a); };

    // Twin-stick arena (Virtual On / Virtual On Oratorio Tangram).
    if (is_twin_stick(family)) {
        const float rx = w - margin_x - stick_r;
        const float ry = stick_y;
        out.push_back(Widget{ControlId::RightStick, rx, ry, stick_r, 0.0f, "", nullptr, true, false, false});

        const PillSpec left_row[] = {
            {ControlId::TriggerL, "L SHOT", false, ActionTarget::AxisRoleBrake, 0},
            {ControlId::BtnLB,    "L DASH", false, ActionTarget::PadRole,       role_u8(Config::PadRole::GearDown)},
        };
        const PillSpec right_row[] = {
            {ControlId::BtnRB,    "R DASH", false, ActionTarget::PadRole,       role_u8(Config::PadRole::GearUp)},
            {ControlId::TriggerR, "R SHOT", true,  ActionTarget::AxisRoleAccel, 0},
        };
        const float row_y = stick_y - stick_r - 32.0f * u;
        add_pill_row(stick_x, row_y, stick_r * 2.0f, 38.0f * u, left_row, 2);
        add_pill_row(rx,      row_y, stick_r * 2.0f, 38.0f * u, right_row, 2);
        return out;
    }

    // Desert Tank: turret stick + machine gun, cannon, gas, fwd/rev gear, VR1..3.
    if (family != nullptr && family->key == "desert") {
        const float rx    = w - margin_x - stick_r;
        const float ry    = stick_y;
        const float row_h = 34.0f * u;
        out.push_back(Widget{ControlId::RightStick, rx, ry, stick_r, 0.0f, "", nullptr, true, false, false});

        const PillSpec left_gears[] = {
            {ControlId::DpadDown, tr("REV"), false, ActionTarget::PadRole, role_u8(Config::PadRole::GearDown)},
            {ControlId::DpadUp,   tr("FWD"), false, ActionTarget::PadRole, role_u8(Config::PadRole::GearUp)},
        };
        add_pill_row(stick_x, stick_y - stick_r - 24.0f * u, stick_r * 2.0f, row_h, left_gears, 2);

        const PillSpec row1[] = {
            {ControlId::BtnLB,    tr("M.GUN"),  false, ActionTarget::PadRole,       role_u8(Config::PadRole::Button1)},
            {ControlId::BtnRB,    tr("CANNON"), false, ActionTarget::PadRole,       role_u8(Config::PadRole::Button2)},
            {ControlId::TriggerR, tr("GAS"),    true,  ActionTarget::AxisRoleAccel, 0},
        };
        const PillSpec row2[] = {
            {ControlId::BtnA, "VR1", false, ActionTarget::PadRole, role_u8(Config::PadRole::Button3)},
            {ControlId::BtnB, "VR2", false, ActionTarget::PadRole, role_u8(Config::PadRole::Button4)},
            {ControlId::BtnX, "VR3", false, ActionTarget::PadRole, role_u8(Config::PadRole::Up)},
        };
        add_pill_row(rx, ry - stick_r - 24.0f * u, stick_r * 2.1f, row_h, row1, 3);
        add_pill_row(rx, ry - stick_r - 66.0f * u, stick_r * 2.1f, row_h, row2, 3);
        return out;
    }

    // Sega Bass Fishing: rod stick, reel stick, cast/select, reel/tension.
    if (is_fishing(family)) {
        const float rx    = w - margin_x - stick_r;
        const float ry    = stick_y;
        const float row_h = 36.0f * u;
        out.push_back(Widget{ControlId::RightStick, rx, ry, stick_r, 0.0f, "", nullptr, true, false, false});

        const PillSpec row1[] = {
            {ControlId::TriggerL, tr("TENSION"), false, ActionTarget::AxisRoleBrake, 0},
            {ControlId::TriggerR, tr("REEL"),    true,  ActionTarget::AxisRoleAccel, 0},
        };
        const PillSpec row2[] = {
            {ControlId::BtnA, tr("CAST"),   false, ActionTarget::PadRole, role_u8(Config::PadRole::Button1)},
            {ControlId::BtnB, tr("SELECT"), false, ActionTarget::PadRole, role_u8(Config::PadRole::Button2)},
        };
        add_pill_row(rx, ry - stick_r - 24.0f * u, stick_r * 2.0f, row_h, row1, 2);
        add_pill_row(rx, ry - stick_r - 68.0f * u, stick_r * 2.0f, row_h, row2, 2);
        return out;
    }

    // Magical Truck Adventure: twin pump levers + pedal.
    if (is_magtruck(family)) {
        const float rx = w - margin_x - stick_r;
        const float ry = stick_y;
        out.push_back(Widget{ControlId::RightStick, rx, ry, stick_r, 0.0f, "", nullptr, true, false, false});
        const PillSpec row1[] = {
            {ControlId::BtnA, tr("PEDAL"), true, ActionTarget::PadRole, role_u8(Config::PadRole::Button1)},
        };
        add_pill_row(rx, ry - stick_r - 26.0f * u, stick_r * 2.0f, 38.0f * u, row1, 1);
        return out;
    }

    // Common right-hand diamond metrics.
    const float btn_r = is_racer(family) ? 44.0f * u : 40.0f * u;
    const float span  = is_racer(family) ? 68.0f * u : 66.0f * u;
    const float cx    = w - margin_x - span - btn_r;
    const float cy    = h - margin_y - span - btn_r;
    const float row_y = cy - span - btn_r - 26.0f * u;
    const float row_w = (span + btn_r) * 2.0f;
    const float row_h = 36.0f * u;

    // Ski / water-ski / skateboard titles.
    if (is_ski_or_board(family)) {
        if (has_flag(family, rom::InputFlags::Ski)) {
            out.push_back(Widget{ControlId::BtnX, cx - span, cy, btn_r, 0.0f, "SEL 1", nullptr, true, true, false,
                                 ActionTarget::PadButtonRaw, raw_btn_u8(SDL_GAMEPAD_BUTTON_WEST)});
            out.push_back(Widget{ControlId::BtnY, cx, cy - span, btn_r, 0.0f, "SEL 2", nullptr, true, false, false,
                                 ActionTarget::PadButtonRaw, raw_btn_u8(SDL_GAMEPAD_BUTTON_NORTH)});
            out.push_back(Widget{ControlId::BtnB, cx + span, cy, btn_r, 0.0f, "SEL 3", nullptr, true, false, false,
                                 ActionTarget::PadButtonRaw, raw_btn_u8(SDL_GAMEPAD_BUTTON_EAST)});
            const PillSpec poles[] = {
                {ControlId::BtnLB, tr("POLE L"), false, ActionTarget::PadButtonRaw, raw_btn_u8(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)},
                {ControlId::BtnRB, tr("POLE R"), false, ActionTarget::PadButtonRaw, raw_btn_u8(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)},
            };
            add_pill_row(cx, row_y, row_w, row_h, poles, 2);
        } else if (family->key == "skisuprg") {
            out.push_back(Widget{ControlId::BtnX, cx - span, cy, btn_r, 0.0f, "SEL 1", nullptr, true, true, false,
                                 ActionTarget::PadButtonRaw, raw_btn_u8(SDL_GAMEPAD_BUTTON_WEST)});
            out.push_back(Widget{ControlId::BtnY, cx, cy - span, btn_r, 0.0f, "SEL 2", nullptr, true, false, false,
                                 ActionTarget::PadButtonRaw, raw_btn_u8(SDL_GAMEPAD_BUTTON_NORTH)});
            out.push_back(Widget{ControlId::BtnB, cx + span, cy, btn_r, 0.0f, "SEL 3", nullptr, true, false, false,
                                 ActionTarget::PadButtonRaw, raw_btn_u8(SDL_GAMEPAD_BUTTON_EAST)});
            const PillSpec zoom[] = {
                {ControlId::DpadDown, "ZOOM-", false, ActionTarget::PadButtonRaw, raw_btn_u8(SDL_GAMEPAD_BUTTON_DPAD_DOWN)},
                {ControlId::DpadUp,   "ZOOM+", false, ActionTarget::PadButtonRaw, raw_btn_u8(SDL_GAMEPAD_BUTTON_DPAD_UP)},
            };
            add_pill_row(cx, row_y, row_w, row_h, zoom, 2);
        } else if (family->key == "segawski") {
            out.push_back(Widget{ControlId::BtnA,     cx, cy + span, btn_r, 0.0f, "SET",    nullptr, true, true, false,
                                 ActionTarget::PadButtonRaw, raw_btn_u8(SDL_GAMEPAD_BUTTON_SOUTH)});
            out.push_back(Widget{ControlId::DpadDown, cx - span, cy, btn_r, 0.0f, "SEL -",  nullptr, true, false, false,
                                 ActionTarget::PadButtonRaw, raw_btn_u8(SDL_GAMEPAD_BUTTON_DPAD_DOWN)});
            out.push_back(Widget{ControlId::DpadUp,   cx, cy - span, btn_r, 0.0f, "SEL +",  nullptr, true, false, false,
                                 ActionTarget::PadButtonRaw, raw_btn_u8(SDL_GAMEPAD_BUTTON_DPAD_UP)});
            const PillSpec pitch[] = {
                {ControlId::BtnLB, tr("PITCH L"), false, ActionTarget::PadButtonRaw, raw_btn_u8(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)},
                {ControlId::BtnRB, tr("PITCH R"), false, ActionTarget::PadButtonRaw, raw_btn_u8(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)},
            };
            add_pill_row(cx, row_y, row_w, row_h, pitch, 2);
        } else {
            // topskatr
            out.push_back(Widget{ControlId::BtnA,     cx, cy + span, btn_r, 0.0f, tr("JUMP F"),  nullptr, true, true, false,
                                 ActionTarget::PadButtonRaw, raw_btn_u8(SDL_GAMEPAD_BUTTON_SOUTH)});
            out.push_back(Widget{ControlId::BtnB,     cx + span, cy, btn_r, 0.0f, tr("JUMP T"),  nullptr, true, false, false,
                                 ActionTarget::PadButtonRaw, raw_btn_u8(SDL_GAMEPAD_BUTTON_EAST)});
            out.push_back(Widget{ControlId::TriggerL, cx - span, cy, btn_r, 0.0f, tr("SLIDE L"), nullptr, true, false, false,
                                 ActionTarget::PadAxisPlus, raw_axis_u8(SDL_GAMEPAD_AXIS_LEFT_TRIGGER)});
            out.push_back(Widget{ControlId::TriggerR, cx, cy - span, btn_r, 0.0f, tr("SLIDE R"), nullptr, true, false, false,
                                 ActionTarget::PadAxisPlus, raw_axis_u8(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)});
            const PillSpec sel[] = {
                {ControlId::DpadLeft,  "SEL <", false, ActionTarget::PadButtonRaw, raw_btn_u8(SDL_GAMEPAD_BUTTON_DPAD_LEFT)},
                {ControlId::DpadRight, "SEL >", false, ActionTarget::PadButtonRaw, raw_btn_u8(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)},
            };
            add_pill_row(cx, row_y, row_w, row_h, sel, 2);
        }
        return out;
    }

    // Flight stick titles: swtrilgy, skytargt, wingwar, swa.
    if (is_flight_stick(family)) {
        if (has_flag(family, rom::InputFlags::AnalogJoystick)) {
            out.push_back(Widget{ControlId::BtnA, cx, cy + span, btn_r, 0.0f, tr("TRIG 1"), nullptr, true, true, false,
                                 ActionTarget::PadRole, role_u8(Config::PadRole::Up)});
            out.push_back(Widget{ControlId::BtnB, cx + span, cy, btn_r, 0.0f, tr("TRIG 2"), nullptr, true, false, false,
                                 ActionTarget::PadRole, role_u8(Config::PadRole::Down)});
            out.push_back(Widget{ControlId::BtnX, cx - span, cy, btn_r, 0.0f, tr("EVT 1"),  nullptr, true, false, false,
                                 ActionTarget::PadRole, role_u8(Config::PadRole::Button1)});
            out.push_back(Widget{ControlId::BtnY, cx, cy - span, btn_r, 0.0f, tr("EVT 2"),  nullptr, true, false, false,
                                 ActionTarget::PadRole, role_u8(Config::PadRole::Button2)});
        } else if (family->key == "skytargt") {
            out.push_back(Widget{ControlId::TriggerR, cx, cy + span, btn_r, 0.0f, tr("M.GUN"), nullptr, true, true, false,
                                 ActionTarget::PadAxisPlus, raw_axis_u8(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)});
            out.push_back(Widget{ControlId::TriggerL, cx + span, cy, btn_r, 0.0f, tr("MSL"),   nullptr, true, false, false,
                                 ActionTarget::PadAxisPlus, raw_axis_u8(SDL_GAMEPAD_AXIS_LEFT_TRIGGER)});
            out.push_back(Widget{ControlId::BtnY,     cx, cy - span, btn_r, 0.0f, tr("VIEW"),  nullptr, true, false, false,
                                 ActionTarget::PadButtonRaw, raw_btn_u8(SDL_GAMEPAD_BUTTON_NORTH)});
        } else if (family->key == "wingwar") {
            const PillSpec thr[] = {
                {ControlId::TriggerL, tr("THR-"), false, ActionTarget::AxisRoleBrake, 0},
                {ControlId::TriggerR, tr("THR+"), false, ActionTarget::AxisRoleAccel, 0},
            };
            add_pill_row(stick_x, stick_y - stick_r - 24.0f * u, stick_r * 2.0f, row_h, thr, 2);

            out.push_back(Widget{ControlId::BtnA, cx, cy + span, btn_r, 0.0f, tr("M.GUN"), nullptr, true, true, false,
                                 ActionTarget::PadRole, role_u8(Config::PadRole::Button1)});
            out.push_back(Widget{ControlId::BtnB, cx + span, cy, btn_r, 0.0f, tr("MSL"),   nullptr, true, false, false,
                                 ActionTarget::PadRole, role_u8(Config::PadRole::Button2)});
            out.push_back(Widget{ControlId::BtnX, cx - span, cy, btn_r, 0.0f, tr("SMOKE"), nullptr, true, false, false,
                                 ActionTarget::PadRole, role_u8(Config::PadRole::Button3)});
            const PillSpec vrs[] = {
                {ControlId::DpadUp,    "VR1", false, ActionTarget::PadRole, role_u8(Config::PadRole::Up)},
                {ControlId::DpadRight, "VR2", false, ActionTarget::PadRole, role_u8(Config::PadRole::Right)},
                {ControlId::DpadDown,  "VR3", false, ActionTarget::PadRole, role_u8(Config::PadRole::Down)},
                {ControlId::DpadLeft,  "VR4", false, ActionTarget::PadRole, role_u8(Config::PadRole::Left)},
            };
            add_pill_row(cx, row_y, row_w, row_h, vrs, 4);
        } else {
            // swa (Star Wars Arcade)
            const PillSpec thr[] = {
                {ControlId::TriggerL, tr("THR-"), false, ActionTarget::AxisRoleBrake, 0},
                {ControlId::TriggerR, tr("THR+"), false, ActionTarget::AxisRoleAccel, 0},
            };
            add_pill_row(stick_x, stick_y - stick_r - 24.0f * u, stick_r * 2.0f, row_h, thr, 2);

            out.push_back(Widget{ControlId::BtnA, cx, cy + span, btn_r, 0.0f, tr("LASER"),   nullptr, true, true, false,
                                 ActionTarget::PadRole, role_u8(Config::PadRole::Button1)});
            out.push_back(Widget{ControlId::BtnB, cx + span, cy, btn_r, 0.0f, tr("TORPEDO"), nullptr, true, false, false,
                                 ActionTarget::PadRole, role_u8(Config::PadRole::Button2)});
            out.push_back(Widget{ControlId::BtnX, cx - span, cy, btn_r, 0.0f, tr("VIEW"),    nullptr, true, false, false,
                                 ActionTarget::PadRole, role_u8(Config::PadRole::Button3)});
        }
        return out;
    }

    // Racing & riding titles (Model 1, Model 2, Model 3): Gas, Brake (if present),
    // Gears (if present), plus per-game View / VR1..4 / Handbrake / Music / Punch / Kick.
    if (is_racer(family)) {
        if (family->key == "powsled") {
            out.push_back(Widget{ControlId::TriggerR, cx,        cy + span, btn_r, 0.0f, "A", tr("GAS"),   true, true,  false,
                                 ActionTarget::AxisRoleAccel, 0});
            out.push_back(Widget{ControlId::TriggerL, cx + span, cy,        btn_r, 0.0f, "B", tr("BRAKE"), true, false, false,
                                 ActionTarget::AxisRoleBrake, 0});
            out.push_back(Widget{ControlId::BtnA,     cx - span, cy,        btn_r, 0.0f, tr("ENTRY"), nullptr, true, false, false,
                                 ActionTarget::PadRole, role_u8(Config::PadRole::Button1)});
            out.push_back(Widget{ControlId::BtnB,     cx,        cy - span, btn_r, 0.0f, tr("CALL"),  nullptr, true, false, false,
                                 ActionTarget::PadRole, role_u8(Config::PadRole::Button2)});
            const PillSpec net[] = {
                {ControlId::BtnLB, tr("ERR"), false, ActionTarget::PadButtonRaw, raw_btn_u8(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)},
                {ControlId::BtnRB, tr("NET"), false, ActionTarget::PadButtonRaw, raw_btn_u8(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)},
            };
            add_pill_row(cx, row_y, row_w, row_h, net, 2);
            return out;
        }

        out.push_back(Widget{ControlId::TriggerR, cx, cy + span, btn_r, 0.0f, "A", tr("GAS"), true, true, false,
                             ActionTarget::AxisRoleAccel, 0});
        if (family->brake) {
            out.push_back(Widget{ControlId::TriggerL, cx + span, cy, btn_r, 0.0f, "B", tr("BRAKE"), true, false, false,
                                 ActionTarget::AxisRoleBrake, 0});
        }
        const bool has_gears = family->gears || family->shift || family->gate;
        if (has_gears) {
            out.push_back(Widget{ControlId::BtnLB, cx - span, cy,        btn_r, 0.0f, "X", tr("GEAR-"), true, false, false,
                                 ActionTarget::PadRole, role_u8(Config::PadRole::GearDown)});
            out.push_back(Widget{ControlId::BtnRB, cx,        cy - span, btn_r, 0.0f, "Y", tr("GEAR+"), true, false, false,
                                 ActionTarget::PadRole, role_u8(Config::PadRole::GearUp)});
        }

        std::vector<PillSpec> extras;
        if (has_flag(family, rom::InputFlags::Harley)) {
            extras.push_back({ControlId::BtnA, tr("MUSIC"), false, ActionTarget::PadRole, role_u8(Config::PadRole::Button1)});
            extras.push_back({ControlId::BtnB, tr("VIEW"),  false, ActionTarget::PadRole, role_u8(Config::PadRole::Button2)});
        } else if (family->key == "motoraid") {
            extras.push_back({ControlId::BtnA, tr("PUNCH"), false, ActionTarget::PadRole, role_u8(Config::PadRole::Button1)});
            extras.push_back({ControlId::BtnB, tr("KICK"),  false, ActionTarget::PadRole, role_u8(Config::PadRole::Button2)});
        } else {
            if (family->vr >= 2) {
                static constexpr const char* kVrLbl[4] = {"VR1", "VR2", "VR3", "VR4"};
                static constexpr ControlId   kVrIds[4] = {ControlId::BtnA, ControlId::BtnB, ControlId::BtnX, ControlId::BtnY};
                static constexpr Config::PadRole kVrRoles[4] = {
                    Config::PadRole::Button1, Config::PadRole::Button2,
                    Config::PadRole::Button3, Config::PadRole::Button4,
                };
                const u32 count = std::min<u32>(family->vr, 4);
                for (u32 i = 0; i < count; ++i) {
                    extras.push_back({kVrIds[i], kVrLbl[i], false, ActionTarget::PadRole, role_u8(kVrRoles[i])});
                }
            } else if (family->vr == 1 || has_flag(family, rom::InputFlags::ViewChange)) {
                extras.push_back({ControlId::BtnA, tr("VIEW"), false, ActionTarget::PadRole, role_u8(Config::PadRole::Button1)});
            }
            if (has_flag(family, rom::InputFlags::Handbrake)) {
                extras.push_back({ControlId::BtnB, tr("H.BRAKE"), false, ActionTarget::PadRole, role_u8(Config::PadRole::Button2)});
            }
        }

        // Put extras into free diamond slots first (e.g. Wave Runner without gears/brake),
        // and place remaining extras in a clean row directly above the diamond.
        size_t next_extra = 0;
        const auto try_slot = [&](float sx, float sy) {
            if (next_extra < extras.size()) {
                const PillSpec& e = extras[next_extra++];
                out.push_back(Widget{e.id, sx, sy, btn_r, 0.0f, e.label, nullptr, true, false, false, e.target, e.code});
            }
        };
        if (!family->brake) try_slot(cx + span, cy);
        if (!has_gears) {
            try_slot(cx - span, cy);
            try_slot(cx, cy - span);
        }
        if (next_extra < extras.size()) {
            add_pill_row(cx, row_y, row_w, row_h, &extras[next_extra], extras.size() - next_extra);
        }
        return out;
    }

    // Standard arcade / fighting / action / sports diamond tailored per title.
    struct FaceBtn {
        ControlId       id;
        const char*     label;
        bool            accent;
        Config::PadRole role;
    };
    std::vector<FaceBtn> faces;
    bool show_bat_pill = false;
    bool show_lb_rb    = false;

    if (has_flag(family, rom::InputFlags::Fighting)) {
        faces = {
            {ControlId::BtnA, tr("PUNCH"), true,  Config::PadRole::Button1},
            {ControlId::BtnB, tr("KICK"),  false, Config::PadRole::Button2},
            {ControlId::BtnX, tr("GUARD"), false, Config::PadRole::Button3},
            {ControlId::BtnY, tr("ESC"),   false, Config::PadRole::Button4},
        };
    } else if (fighting_m12(family)) {
        faces = {
            {ControlId::BtnA, tr("PUNCH"), true,  Config::PadRole::Button1},
            {ControlId::BtnB, tr("KICK"),  false, Config::PadRole::Button2},
            {ControlId::BtnX, tr("GUARD"), false, Config::PadRole::Button3},
        };
    } else if (has_flag(family, rom::InputFlags::SpikeOut)) {
        faces = {
            {ControlId::BtnA, tr("BEAT"),   true,  Config::PadRole::Button1},
            {ControlId::BtnB, tr("CHARGE"), false, Config::PadRole::Button2},
            {ControlId::BtnX, tr("SHIFT"),  false, Config::PadRole::Button3},
            {ControlId::BtnY, tr("JUMP"),   false, Config::PadRole::Button4},
        };
    } else if (has_flag(family, rom::InputFlags::Soccer)) {
        faces = {
            {ControlId::BtnA, tr("L.PASS"), true,  Config::PadRole::Button1},
            {ControlId::BtnB, tr("SHOOT"),  false, Config::PadRole::Button2},
            {ControlId::BtnX, tr("S.PASS"), false, Config::PadRole::Button3},
        };
    } else if (family != nullptr && family->key == "dynamcop") {
        faces = {
            {ControlId::BtnA, tr("PUNCH"), true,  Config::PadRole::Button1},
            {ControlId::BtnB, tr("KICK"),  false, Config::PadRole::Button2},
            {ControlId::BtnX, tr("JUMP"),  false, Config::PadRole::Button3},
        };
    } else if (family != nullptr && (family->key == "zerogun" || family->key == "zeroguna" || family->key == "pltkids")) {
        faces = {
            {ControlId::BtnA, tr("SHOT"), true,  Config::PadRole::Button1},
            {ControlId::BtnB, tr("BOMB"), false, Config::PadRole::Button2},
        };
    } else if (family != nullptr && family->bat) {
        faces = {
            {ControlId::BtnA, tr("SWING"), true,  Config::PadRole::Button1},
            {ControlId::BtnB, tr("BUNT"),  false, Config::PadRole::Button2},
            {ControlId::BtnX, tr("RUN"),   false, Config::PadRole::Button3},
        };
        show_bat_pill = true;
    } else if (is_gun_game(family)) {
        faces = {
            {ControlId::BtnA, tr("FIRE"), true, Config::PadRole::Button1},
            {ControlId::BtnB, family->missile ? tr("MSL") : tr("RELOAD"), false, Config::PadRole::Button2},
        };
    } else if (has_flag(family, rom::InputFlags::Buttons3)) {
        faces = {
            {ControlId::BtnA, "A", true,  Config::PadRole::Button1},
            {ControlId::BtnB, "B", false, Config::PadRole::Button2},
            {ControlId::BtnX, "X", false, Config::PadRole::Button3},
        };
    } else {
        faces = {
            {ControlId::BtnA, "A", true,  Config::PadRole::Button1},
            {ControlId::BtnB, "B", false, Config::PadRole::Button2},
            {ControlId::BtnX, "X", false, Config::PadRole::Button3},
            {ControlId::BtnY, "Y", false, Config::PadRole::Button4},
        };
        show_lb_rb = true;
    }

    const ImVec2 diamond_pos[4] = {
        {cx,        cy + span},
        {cx + span, cy},
        {cx - span, cy},
        {cx,        cy - span},
    };
    for (size_t i = 0; i < faces.size() && i < 4; ++i) {
        out.push_back(Widget{faces[i].id, diamond_pos[i].x, diamond_pos[i].y, btn_r, 0.0f,
                             faces[i].label, nullptr, true, faces[i].accent, false,
                             ActionTarget::PadRole, role_u8(faces[i].role)});
    }
    if (show_bat_pill) {
        const PillSpec bat[] = {
            {ControlId::TriggerR, tr("BAT"), false, ActionTarget::AxisRoleAccel, 0},
        };
        add_pill_row(cx, row_y, row_w, row_h, bat, 1);
    } else if (show_lb_rb) {
        const PillSpec shoulders[] = {
            {ControlId::BtnLB, "LB", false, ActionTarget::PadRole, role_u8(Config::PadRole::GearDown)},
            {ControlId::BtnRB, "RB", false, ActionTarget::PadRole, role_u8(Config::PadRole::GearUp)},
        };
        add_pill_row(cx, row_y, row_w, 32.0f * u, shoulders, 2);
    }
    return out;
}

bool TouchGamepad::handle_event(const SDL_Event& event, float screen_w, float screen_h,
                                const Config& config, const Gui::GameFamily* family,
                                bool pad_active)
{
    if (screen_w <= 0.0f || screen_h <= 0.0f) {
        return false;
    }
    m_last_w = screen_w;
    m_last_h = screen_h;

    if (event.type != SDL_EVENT_FINGER_DOWN
        && event.type != SDL_EVENT_FINGER_MOTION
        && event.type != SDL_EVENT_FINGER_UP
        && event.type != SDL_EVENT_FINGER_CANCELED) {
        return false;
    }

    const SDL_FingerID fid = event.tfinger.fingerID;
    const float px         = event.tfinger.x * screen_w;
    const float py         = event.tfinger.y * screen_h;

    if (event.type == SDL_EVENT_FINGER_UP || event.type == SDL_EVENT_FINGER_CANCELED) {
        if (m_gun_finger_down && fid == m_gun_aim_finger) {
            m_gun_finger_down = false;
            return true;
        }
        const auto it = std::find_if(m_fingers.begin(), m_fingers.end(),
                                     [fid](const FingerHold& f) { return f.finger_id == fid; });
        if (it != m_fingers.end()) {
            m_fingers.erase(it);
            return true;
        }
        return false;
    }

    if (event.type == SDL_EVENT_FINGER_MOTION) {
        if (m_gun_finger_down && fid == m_gun_aim_finger) {
            m_gun_aim_nx = std::clamp(event.tfinger.x, 0.0f, 1.0f);
            m_gun_aim_ny = std::clamp(event.tfinger.y, 0.0f, 1.0f);
            return true;
        }
        const auto it = std::find_if(m_fingers.begin(), m_fingers.end(),
                                     [fid](const FingerHold& f) { return f.finger_id == fid; });
        if (it != m_fingers.end()) {
            it->x = px;
            it->y = py;
            return true;
        }
        return false;
    }

    // SDL_EVENT_FINGER_DOWN
    const std::vector<Widget> widgets = build_layout(config, family, pad_active, screen_w, screen_h);
    for (const Widget& w : widgets) {
        if (hit_test(w, px, py)) {
            switch (w.id) {
                case ControlId::Menu:       m_menu_requested       = true; break;
                case ControlId::HudFps:     m_fps_toggle_requested = true; break;
                case ControlId::HudService: m_service_pulse        = 4;    break;
                case ControlId::HudTest:    m_test_pulse           = 4;    break;
                case ControlId::HudExit:    m_exit_requested       = true; break;
                case ControlId::HudSlot:    m_active_slot = (m_active_slot % 4) + 1; break;
                case ControlId::HudSave:    m_state_request = {StateAction::Kind::Save, m_active_slot}; break;
                case ControlId::HudLoad:    m_state_request = {StateAction::Kind::Load, m_active_slot}; break;
                default: break;
            }
            m_fingers.push_back(FingerHold{fid, w.id, px, py});
            return true;
        }
    }
    if (pad_active && use_direct_touch_gun(config, family)) {
        m_gun_finger_down = true;
        m_gun_aim_engaged = true;
        m_gun_aim_finger  = fid;
        m_gun_aim_nx      = std::clamp(event.tfinger.x, 0.0f, 1.0f);
        m_gun_aim_ny      = std::clamp(event.tfinger.y, 0.0f, 1.0f);
        return true;
    }
    return false;
}

void TouchGamepad::update_and_draw(const Config& config, Input* input,
                                   const Gui::GameFamily* family,
                                   bool in_game, bool pad_active,
                                   float screen_w, float screen_h, float ui_scale)
{
    static_cast<void>(ui_scale);
    if (input == nullptr) {
        return;
    }
    if (!in_game) {
        reset(input);
        return;
    }

    input->clear_virtual_pad();

    const std::vector<Widget> widgets = build_layout(config, family, pad_active, screen_w, screen_h);
    const auto held_widget = [&](ControlId id) -> const FingerHold* {
        for (const FingerHold& f : m_fingers) {
            if (f.control == id) return &f;
        }
        return nullptr;
    };

    const bool svc_active = held_widget(ControlId::HudService) != nullptr || m_service_pulse > 0;
    const bool tst_active = held_widget(ControlId::HudTest)    != nullptr || m_test_pulse > 0;
    if (m_service_pulse > 0) --m_service_pulse;
    if (m_test_pulse > 0)    --m_test_pulse;
    input->set_virtual_operator(svc_active, tst_active);

    if (pad_active) {
        input->set_virtual_active(true);

        const Config::PadLayout layout = Input::game_layout(
            config, family != nullptr ? family->key : std::string{},
            family != nullptr ? family->inputs : 0u);

        for (const Widget& w : widgets) {
            if (held_widget(w.id) == nullptr) continue;
            switch (w.target) {
                case ActionTarget::PadRole:
                    if (w.action_code < Config::kPadRoleCount) {
                        trigger_pad_binding(input, layout.buttons[0][w.action_code]);
                    }
                    break;
                case ActionTarget::PadButtonRaw:
                    input->set_virtual_button(static_cast<SDL_GamepadButton>(w.action_code), true);
                    break;
                case ActionTarget::PadAxisPlus:
                    input->set_virtual_axis(static_cast<SDL_GamepadAxis>(w.action_code), 32767);
                    break;
                case ActionTarget::AxisRoleAccel:
                    trigger_axis_role(input, layout, Config::PadAxisRole::Accel, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
                    break;
                case ActionTarget::AxisRoleBrake:
                    trigger_axis_role(input, layout, Config::PadAxisRole::Brake, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
                    break;
                case ActionTarget::None:
                    break;
            }
        }

        const auto apply_stick = [&](ControlId cid, SDL_GamepadAxis ax, SDL_GamepadAxis ay, bool also_dpad) {
            const FingerHold* f = held_widget(cid);
            if (f == nullptr) return;
            for (const Widget& w : widgets) {
                if (w.id != cid || w.w <= 1.0f) continue;
                float nx = (f->x - w.cx) / (w.w * 0.78f);
                float ny = (f->y - w.cy) / (w.w * 0.78f);
                const float len = std::sqrt(nx * nx + ny * ny);
                if (len > 1.0f) {
                    nx /= len;
                    ny /= len;
                }
                nx = std::clamp(nx, -1.0f, 1.0f);
                ny = std::clamp(ny, -1.0f, 1.0f);
                input->set_virtual_axis(ax, static_cast<s16>(nx * 32767.0f));
                if (ay != SDL_GAMEPAD_AXIS_INVALID) {
                    input->set_virtual_axis(ay, static_cast<s16>(ny * 32767.0f));
                }

                if (also_dpad) {
                    constexpr float kDpadThresh = 0.35f;
                    if (nx < -kDpadThresh) input->set_virtual_button(SDL_GAMEPAD_BUTTON_DPAD_LEFT, true);
                    if (nx >  kDpadThresh) input->set_virtual_button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, true);
                    if (ny < -kDpadThresh) input->set_virtual_button(SDL_GAMEPAD_BUTTON_DPAD_UP, true);
                    if (ny >  kDpadThresh) input->set_virtual_button(SDL_GAMEPAD_BUTTON_DPAD_DOWN, true);
                }
                break;
            }
        };

        const bool stick_also_dpad = !is_gun_game(family)
                                  && (is_twin_stick(family) || family == nullptr
                                      || (!family->aim && !is_racer(family)));
        const bool is_desert = family != nullptr && family->key == "desert";
        apply_stick(ControlId::LeftStick, SDL_GAMEPAD_AXIS_LEFTX,
                    is_desert ? SDL_GAMEPAD_AXIS_INVALID : SDL_GAMEPAD_AXIS_LEFTY,
                    stick_also_dpad);
        apply_stick(ControlId::RightStick, SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY, false);

        if (use_direct_touch_gun(config, family)) {
            input->set_virtual_touch_aim(true, m_gun_aim_engaged, m_gun_aim_nx, m_gun_aim_ny,
                                         m_gun_finger_down, screen_w, screen_h);
        } else {
            m_gun_finger_down = false;
            m_gun_aim_engaged = false;
            input->set_virtual_touch_aim(false, false, 0.5f, 0.5f, false, 0.0f, 0.0f);
        }
    } else {
        m_gun_finger_down = false;
        m_gun_aim_engaged = false;
    }

    input->set_suppress_touch_pointer(any_touch_claimed());

    // Render controls on ImGui's foreground draw list so they overlay the game.
    ImDrawList* dl    = ImGui::GetForegroundDrawList();
    ImFont*     font  = ImGui::GetFont();
    const float alpha = static_cast<float>(std::clamp(config.virtual_gamepad_opacity, 15u, 100u)) / 100.0f;
    // Note: ImGui::GetFontSize() is already scaled by FontScaleMain (ui_scale).
    const float fsize = std::max(16.0f, ImGui::GetFontSize() * 0.85f);

    for (const Widget& w : widgets) {
        const FingerHold* active = held_widget(w.id);
        const bool        down   = active != nullptr
                                || (w.id == ControlId::HudService && svc_active)
                                || (w.id == ControlId::HudTest    && tst_active);

        if (w.id == ControlId::LeftStick || w.id == ControlId::RightStick) {
            const float fill_a   = alpha * (down ? 0.30f : 0.16f);
            const float stroke_a = alpha * (down ? 0.90f : 0.65f);
            dl->AddCircleFilled(ImVec2(w.cx, w.cy), w.w, rgba_u32(18, 22, 30, fill_a), 48);
            dl->AddCircle(ImVec2(w.cx, w.cy), w.w, rgba_u32(238, 244, 255, stroke_a), 48, 2.5f);

            float kx = w.cx;
            float ky = w.cy;
            if (active != nullptr) {
                float dx = active->x - w.cx;
                float dy = active->y - w.cy;
                const float max_r = w.w * 0.62f;
                const float dist  = std::sqrt(dx * dx + dy * dy);
                if (dist > max_r && dist > 0.001f) {
                    dx = dx * (max_r / dist);
                    dy = dy * (max_r / dist);
                }
                kx += dx;
                ky += dy;
            }
            const float knob_r = w.w * 0.36f;
            dl->AddCircleFilled(ImVec2(kx, ky), knob_r,
                                rgba_u32(down ? 244 : 215, down ? 184 : 225, down ? 96 : 240, alpha * (down ? 0.88f : 0.68f)), 36);
            dl->AddCircle(ImVec2(kx, ky), knob_r, rgba_u32(255, 255, 255, alpha * 0.88f), 36, 1.8f);
            continue;
        }

        const bool is_hud_pill = !w.circle;
        const float fill_a     = is_hud_pill ? std::max(0.55f, alpha * 0.82f) : alpha * (w.accent ? (down ? 0.92f : 0.78f) : (down ? 0.56f : 0.34f));
        const float edge_a     = is_hud_pill ? 0.82f : alpha * (down ? 0.98f : 0.72f);
        const float text_a     = is_hud_pill ? 0.96f : alpha * (down ? 1.0f : 0.92f);

        ImU32 fill_c = rgba_u32(22, 27, 36, fill_a);
        ImU32 edge_c = rgba_u32(230, 238, 250, edge_a);
        ImU32 text_c = rgba_u32(245, 248, 255, text_a);
        if (w.danger) {
            fill_c = rgba_u32(down ? 205 : 175, 58, 52, std::max(0.75f, fill_a));
            edge_c = rgba_u32(235, 130, 120, 0.85f);
        } else if (w.accent) {
            fill_c = rgba_u32(down ? 250 : 238, down ? 205 : 185, down ? 130 : 108, fill_a);
            edge_c = rgba_u32(255, 232, 185, edge_a);
            text_c = rgba_u32(24, 24, 28, std::max(0.92f, text_a));
        } else if (down) {
            fill_c = rgba_u32(80, 155, 245, std::max(0.65f, fill_a));
            edge_c = rgba_u32(180, 225, 255, edge_a);
        }

        if (w.circle) {
            dl->AddCircleFilled(ImVec2(w.cx, w.cy), w.w, fill_c, 36);
            dl->AddCircle(ImVec2(w.cx, w.cy), w.w, edge_c, 36, 2.2f);
        } else {
            const ImVec2 p0(w.cx - w.w * 0.5f, w.cy - w.h * 0.5f);
            const ImVec2 p1(w.cx + w.w * 0.5f, w.cy + w.h * 0.5f);
            const float  r = w.h * 0.45f;
            dl->AddRectFilled(p0, p1, fill_c, r);
            if (!w.accent && !w.danger) {
                dl->AddRect(p0, p1, edge_c, r, 0, 1.8f);
            }
        }

        if (w.label != nullptr && w.label[0] != '\0') {
            if (w.sublabel != nullptr && w.sublabel[0] != '\0') {
                const float main_sz = fsize * 0.92f;
                const float sub_sz  = fsize * 0.54f;
                const ImVec2 ts1 = font->CalcTextSizeA(main_sz, FLT_MAX, 0.0f, w.label);
                const ImVec2 ts2 = font->CalcTextSizeA(sub_sz, FLT_MAX, 0.0f, w.sublabel);
                const float total_h = ts1.y + ts2.y * 0.9f;
                const float top     = w.cy - total_h * 0.5f;
                dl->AddText(font, main_sz, ImVec2(w.cx - ts1.x * 0.5f, top), text_c, w.label);
                dl->AddText(font, sub_sz, ImVec2(w.cx - ts2.x * 0.5f, top + ts1.y * 0.92f),
                            w.accent ? rgba_u32(36, 32, 26, 0.92f) : rgba_u32(225, 232, 245, alpha * 0.85f),
                            w.sublabel);
            } else {
                float lbl_size = w.circle ? fsize * 0.92f : fsize * 0.78f;
                ImVec2 ts      = font->CalcTextSizeA(lbl_size, FLT_MAX, 0.0f, w.label);
                const float max_w = w.circle ? w.w * 1.65f : w.w * 0.88f;
                const float max_h = w.circle ? w.w * 1.65f : w.h * 0.68f;
                if (ts.x > max_w && ts.x > 1.0f) {
                    lbl_size *= max_w / ts.x;
                    ts        = font->CalcTextSizeA(lbl_size, FLT_MAX, 0.0f, w.label);
                }
                if (ts.y > max_h && ts.y > 1.0f) {
                    lbl_size *= max_h / ts.y;
                    ts        = font->CalcTextSizeA(lbl_size, FLT_MAX, 0.0f, w.label);
                }
                dl->AddText(font, lbl_size, ImVec2(w.cx - ts.x * 0.5f, w.cy - ts.y * 0.5f), text_c, w.label);
            }
        }
    }
}

}  // namespace sm2::osd
