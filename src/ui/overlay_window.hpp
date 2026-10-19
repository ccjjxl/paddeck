// SPDX-License-Identifier: MIT
// The gtk4-layer-shell overlay that manages Umbriel scratchpads.
#pragma once

#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <gtkmm/applicationwindow.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/centerbox.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/entry.h>
#include <gtkmm/label.h>
#include <gtkmm/popover.h>
#include <gtkmm/scrolledwindow.h>

#include "model/config_store.hpp"
#include "model/scratchpad.hpp"
#include "model/types.hpp"
#include "umbriel/ipc.hpp"

namespace spm {

class OverlayWindow : public Gtk::ApplicationWindow {
public:
    OverlayWindow(UmbrielIpc& ipc, ConfigStore& config, bool use_layer_shell);
    ~OverlayWindow() override;

    /// Present the overlay, or hide it when it is already visible.
    void toggle_visible();

private:
    using Action = std::pair<Glib::ustring, std::function<void()>>;

    /// One row of the per-scratchpad "⋮" menu.
    struct MenuEntry {
        Glib::ustring label;
        std::function<void()> activate;
        bool danger = false;
        bool separator_before = false;
    };

    void build_ui();
    void setup_layer_shell();

    void apply_theme(const Theme& theme);
    void on_windows(std::vector<WindowInfo> windows);
    void on_connection(bool connected);
    void on_action_result(std::string action, bool ok, std::string error);

    void reload_config();
    void rebuild();

    Gtk::Widget* build_scratchpad_card(const Scratchpad& sp);
    Gtk::Widget* build_window_row(const WindowInfo& w, const Scratchpad& sp);
    Gtk::Widget* build_action_menu(std::vector<MenuEntry> entries);
    void clear_menus();
    void run(std::string action);
    void set_status(const Glib::ustring& message, bool error);

    void hide_prompt();
    void clear_prompt_body();
    void set_prompt_actions(std::vector<Action> actions);
    void show_new_scratchpad_form();
    void confirm_delete(const Scratchpad& sp);
    void commit_new_scratchpad(const std::string& name, const std::string& spawn);
    void perform_add(const std::string& name, const std::string& spawn);
    void after_config_change(const Glib::ustring& message);
    void reveal_source(const Scratchpad& sp);
    static void reveal_file(const std::string& path);

    bool matches_filter(const WindowInfo& w) const;
    bool scratchpad_has_matches(const Scratchpad& sp) const;

    UmbrielIpc& m_ipc;
    ConfigStore& m_config;

    std::vector<WindowInfo> m_windows;
    std::vector<Scratchpad> m_scratchpads;
    Theme m_theme;
    std::set<std::string> m_collapsed;
    Glib::ustring m_filter;
    bool m_connected = false;

    Glib::RefPtr<Gtk::CssProvider> m_css;

    Gtk::CenterBox* m_backdrop = nullptr;
    Gtk::Box* m_card = nullptr;
    Gtk::Box* m_list = nullptr;
    Gtk::Box* m_prompt = nullptr;
    Gtk::Label* m_prompt_label = nullptr;
    Gtk::Box* m_prompt_body = nullptr;
    Gtk::Box* m_prompt_actions = nullptr;
    Gtk::Entry* m_search = nullptr;
    Gtk::Label* m_status = nullptr;
    Gtk::Label* m_connection = nullptr;
    Gtk::ScrolledWindow* m_scroll = nullptr;
    std::vector<Gtk::Popover*> m_menu_popovers;  // owned by us; see clear_menus()
};

}  // namespace spm
