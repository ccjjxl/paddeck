// SPDX-License-Identifier: MIT
// The gtk4-layer-shell overlay: a keyboard-driven scratchpad switcher.
//
// The overlay lists every scratchpad with a strip of live window previews. The
// selection is the whole interface: whatever is selected is what the keybindings
// act on.
//
//   j / k / ↑ / ↓ / Tab   move the selection
//   space                 toggle the selected scratchpad (show / hide)
//   h / l / ← / →         show only the previous / next window of the selection
//   enter                 focus a window of the selection, then dismiss
//   1 .. 9                jump straight to the n-th scratchpad and toggle it
//   esc                   dismiss the overlay
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <gtkmm/applicationwindow.h>
#include <gtkmm/box.h>
#include <gtkmm/centerbox.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/label.h>
#include <gtkmm/picture.h>
#include <gtkmm/scrolledwindow.h>

#include "model/config_store.hpp"
#include "model/scratchpad.hpp"
#include "model/types.hpp"
#include "umbriel/capture.hpp"
#include "umbriel/ipc.hpp"
#include "wayland/layer_shell.hpp"

namespace paddeck {

class OverlayWindow : public Gtk::ApplicationWindow {
public:
    OverlayWindow(UmbrielIpc& ipc, ConfigStore& config, bool use_layer_shell);
    ~OverlayWindow() override;

    /// Present the overlay, or hide it when it is already visible.
    void toggle_visible();

private:
    /// The live widgets of one window preview tile, so an asynchronously
    /// captured image can be dropped into the right place.
    struct ThumbSlot {
        Gtk::Picture* picture = nullptr;
        Gtk::Widget* placeholder = nullptr;
    };

    void build_ui();
    void setup_layer_shell();

    void apply_theme(const Theme& theme);
    void on_windows(std::vector<WindowInfo> windows);
    void on_connection(bool connected);
    void on_action_result(std::string action, bool ok, std::string error);
    void on_thumbnail(Thumbnail thumbnail);

    void reload_config();
    void rebuild();

    bool handle_key(guint keyval);
    void select_index(int index);
    void move_selection(int delta);
    void apply_selection();
    void toggle_selected();
    void cycle_member_of_selection(int delta);
    void focus_selected();
    void select_and_toggle(int index);

    const Scratchpad* selected() const;

    void update_capture_targets();
    void run(std::string action);
    void set_status(const Glib::ustring& message, bool error);

    Gtk::Widget* build_scratchpad_card(const Scratchpad& sp, int index);
    Gtk::Widget* build_window_tile(const WindowInfo& w);

    UmbrielIpc& m_ipc;
    ConfigStore& m_config;
    CaptureClient m_capture;
    std::unique_ptr<LayerShell> m_layer_shell;

    std::vector<WindowInfo> m_windows;
    std::vector<Scratchpad> m_scratchpads;
    /// Keeps each card's tiles in a stable order even though Umbriel lists
    /// windows most-recently-focused first.
    MemberOrder m_member_order;
    Theme m_theme;
    bool m_connected = false;
    /// Index into m_scratchpads; the scratchpad the keys act on.
    int m_selected = 0;

    /// Last captured image per window id; reused when the list is rebuilt so
    /// tiles do not flash empty.
    std::map<std::string, Glib::RefPtr<Gdk::Pixbuf>> m_thumb_cache;
    /// Tiles of the current rebuild, keyed by window id.
    std::map<std::string, ThumbSlot> m_thumb_slots;
    /// One card per scratchpad, in list order.
    std::vector<Gtk::Widget*> m_rows;

    Glib::RefPtr<Gtk::CssProvider> m_css;

    Gtk::CenterBox* m_backdrop = nullptr;
    Gtk::Box* m_card = nullptr;
    Gtk::Box* m_list = nullptr;
    Gtk::Label* m_status = nullptr;
    Gtk::Label* m_connection = nullptr;
    Gtk::ScrolledWindow* m_scroll = nullptr;
};

}  // namespace paddeck
