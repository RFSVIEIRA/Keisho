// ██╗  ██╗███████╗██╗███████╗██╗  ██╗ ██████╗
// ██║ ██╔╝██╔════╝██║██╔════╝██║  ██║██╔═══██╗
// █████╔╝ █████╗  ██║███████╗███████║██║   ██║
// ██╔═██╗ ██╔══╝  ██║╚════██║██╔══██║██║   ██║
// ██║  ██╗███████╗██║███████║██║  ██║╚██████╔╝
// ╚═╝  ╚═╝╚══════╝╚═╝╚══════╝╚═╝  ╚═╝ ╚═════╝
//            継 承  ( k e i s h ō )
//
// On-screen multi-touch virtual gamepad and quick menu button.
#pragma once

#include "core/config.h"
#include "core/types.h"
#include "osd/gui.h"

#include <SDL3/SDL.h>

#include <array>
#include <string>
#include <vector>

namespace sm2::osd {

class Input;

/// Renders and samples an on-screen multi-touch gamepad tailored to the running
/// game's cabinet controls, plus an on-screen Menu trigger during gameplay.
class TouchGamepad {
public:
    /// Returns true if the virtual gamepad overlay should be visible and active.
    [[nodiscard]] static bool is_active(const Config& config, const Input& input);

    /// Attach the host SDL window so safe-area insets (navigation bar / display
    /// cutouts) can be respected when laying out controls.
    void set_window(SDL_Window* window) { m_window = window; }

    /// Clear any held virtual inputs (e.g. when opening Settings or backgrounding).
    void reset(Input* input);

    /// Process SDL multi-touch events while in gameplay. Returns true if the
    /// touch event landed on a virtual control (so pointer aiming can ignore it).
    bool handle_event(const SDL_Event& event, float screen_w, float screen_h,
                      const Config& config, const Gui::GameFamily* family,
                      bool pad_active);

    /// Push the current virtual button/axis state into `input` and draw the
    /// controls + quick menu pill via ImGui's foreground/background draw list.
    void update_and_draw(const Config& config, Input* input,
                         const Gui::GameFamily* family,
                         bool in_game, bool pad_active,
                         float screen_w, float screen_h, float ui_scale);

    struct StateAction {
        enum class Kind { None, Save, Load };
        Kind kind = Kind::None;
        int  slot = 1;
    };

    /// True once after the user taps the on-screen Menu/Settings button.
    [[nodiscard]] bool take_menu_request();

    /// True once after the user taps the on-screen FPS pill.
    [[nodiscard]] bool take_fps_toggle_request();

    /// True once after the user taps the on-screen Exit button.
    [[nodiscard]] bool take_exit_request();

    /// Save or load quick-slot request from the in-game HUD bar.
    [[nodiscard]] StateAction take_state_request();

    /// True while any touch finger is currently held on a virtual control.
    [[nodiscard]] bool any_touch_claimed() const { return !m_fingers.empty() || m_gun_finger_down; }

    struct ControlOption {
        u32         bit = 0;
        std::string label;
    };

    /// Returns the toggleable on-screen controls that `family` uses (or Default
    /// when `family` is null), with the game's own button labels.
    [[nodiscard]] static std::vector<ControlOption> describe_controls(
        const Config& config, const Gui::GameFamily* family);

private:
    enum class ControlId {
        None,
        Menu,
        HudFps,
        HudService,
        HudTest,
        HudExit,
        HudView,
        HudSave,
        HudSlot,
        HudLoad,
        Coin,
        Start,
        LeftStick,
        RightStick,
        BtnA,
        BtnB,
        BtnX,
        BtnY,
        BtnLB,
        BtnRB,
        TriggerL,
        TriggerR,
        DpadUp,
        DpadDown,
        DpadLeft,
        DpadRight,
    };

    enum class ActionTarget : u8 {
        None,
        PadRole,
        PadButtonRaw,
        PadAxisPlus,
        AxisRoleAccel,
        AxisRoleBrake,
    };

    struct Widget {
        ControlId    id          = ControlId::None;
        float        cx          = 0.0f;
        float        cy          = 0.0f;
        float        w           = 0.0f;  ///< radius for circles/sticks, width for pills
        float        h           = 0.0f;  ///< height for pills (0 = circle with radius w)
        const char*  label       = "";
        const char*  sublabel    = nullptr;
        bool         circle      = true;
        bool         accent      = false;
        bool         danger      = false;
        ActionTarget target      = ActionTarget::None;
        u8           action_code = 0;
    };

    struct FingerHold {
        SDL_FingerID finger_id = 0;
        ControlId    control   = ControlId::None;
        float        x         = 0.0f;
        float        y         = 0.0f;
    };

    [[nodiscard]] static std::vector<Widget> build_raw_layout(const Config& config,
                                                              const Gui::GameFamily* family,
                                                              bool pad_active,
                                                              float w, float h,
                                                              char* slot_label_buf,
                                                              size_t slot_label_size,
                                                              int active_slot,
                                                              SDL_Window* window = nullptr);

    [[nodiscard]] std::vector<Widget> build_layout(const Config& config,
                                                   const Gui::GameFamily* family,
                                                   bool pad_active,
                                                   float w, float h) const;

    [[nodiscard]] static u32 control_bit(ControlId id);

    [[nodiscard]] static bool hit_test(const Widget& w, float px, float py);

    std::vector<FingerHold> m_fingers;
    bool                    m_gun_finger_down      = false;
    bool                    m_gun_aim_engaged      = false;
    SDL_FingerID            m_gun_aim_finger       = 0;
    float                   m_gun_aim_nx           = 0.5f;
    float                   m_gun_aim_ny           = 0.5f;
    bool                    m_menu_requested       = false;
    bool                    m_fps_toggle_requested = false;
    bool                    m_exit_requested       = false;
    StateAction             m_state_request{};
    int                     m_active_slot          = 1;
    mutable char            m_slot_label[24]       = "SLOT 1";
    SDL_Window*             m_window               = nullptr;
    float                   m_last_w               = 0.0f;
    float                   m_last_h               = 0.0f;
    u32                     m_service_pulse        = 0;
    u32                     m_test_pulse           = 0;
};

}  // namespace sm2::osd
