// SPDX-License-Identifier: MIT
#include "ui/overlay_window.hpp"

#include <algorithm>

#include <gdk/gdkkeysyms.h>
#include <gtk4-layer-shell/gtk4-layer-shell.h>

#include <gdkmm/display.h>
#include <giomm/appinfo.h>
#include <gtkmm/button.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/gestureclick.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/popover.h>
#include <gtkmm/styleprovider.h>
#include <pangomm.h>

#include "ui/theme.hpp"

namespace spm {

namespace {

Gtk::Button* icon_button(const Glib::ustring& label, const Glib::ustring& tooltip) {
    auto* button = Gtk::make_managed<Gtk::Button>(label);
    button->add_css_class("spm-icon");
    button->set_tooltip_text(tooltip);
    button->set_focus_on_click(false);
    return button;
}

Gtk::Button* primary_button(const Glib::ustring& label, const Glib::ustring& tooltip) {
    auto* button = Gtk::make_managed<Gtk::Button>(label);
    button->add_css_class("spm-primary");
    button->set_tooltip_text(tooltip);
    button->set_focus_on_click(false);
    return button;
}

Gtk::Label* subtle_label(const Glib::ustring& text) {
    auto* label = Gtk::make_managed<Gtk::Label>(text);
    label->add_css_class("spm-subtle");
    label->set_xalign(0.0f);
    return label;
}

Glib::ustring plural(std::size_t count, const char* singular, const char* many) {
    return std::to_string(count) + " " + (count == 1 ? singular : many);
}

void clear_box(Gtk::Box& box) {
    while (Gtk::Widget* child = box.get_first_child()) {
        box.remove(*child);
    }
}

}  // namespace

OverlayWindow::OverlayWindow(UmbrielIpc& ipc, ConfigStore& config, bool use_layer_shell)
    : m_ipc(ipc), m_config(config) {
    build_ui();

    m_css = Gtk::CssProvider::create();
    if (auto display = Gdk::Display::get_default()) {
        Gtk::StyleProvider::add_provider_for_display(
            display, m_css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    apply_theme(m_theme);

    if (use_layer_shell && gtk_layer_is_supported()) {
        setup_layer_shell();
    } else {
        set_decorated(true);
        set_resizable(true);
        set_default_size(600, 720);
    }

    m_ipc.signal_windows.connect(sigc::mem_fun(*this, &OverlayWindow::on_windows));
    m_ipc.signal_theme.connect(sigc::mem_fun(*this, &OverlayWindow::apply_theme));
    m_ipc.signal_connection.connect(sigc::mem_fun(*this, &OverlayWindow::on_connection));
    m_ipc.signal_action_result.connect(sigc::mem_fun(*this, &OverlayWindow::on_action_result));

    set_hide_on_close(true);
    reload_config();
    rebuild();
}

void OverlayWindow::setup_layer_shell() {
    auto* window = GTK_WINDOW(gobj());
    gtk_layer_init_for_window(window);
    gtk_layer_set_namespace(window, "scratchpad-manager");
    gtk_layer_set_layer(window, GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
    gtk_layer_set_exclusive_zone(window, 0);
    gtk_layer_set_keyboard_mode(window, GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
}

void OverlayWindow::build_ui() {
    set_title("Scratchpads");
    set_decorated(false);
    add_css_class("spm-window");

    m_backdrop = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
    m_backdrop->add_css_class("spm-backdrop");
    set_child(*m_backdrop);

    m_card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
    m_card->add_css_class("spm-card");
    m_card->set_halign(Gtk::Align::CENTER);
    m_card->set_valign(Gtk::Align::CENTER);
    m_card->set_size_request(560, -1);
    m_backdrop->append(*m_card);

    auto* header = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    auto* title = Gtk::make_managed<Gtk::Label>("Scratchpads");
    title->add_css_class("spm-title");
    title->set_halign(Gtk::Align::START);
    title->set_hexpand(true);
    header->append(*title);

    m_connection = subtle_label("connecting…");
    m_connection->set_halign(Gtk::Align::END);
    header->append(*m_connection);

    auto* refresh = icon_button("⟳", "Reload configuration and window list");
    refresh->signal_clicked().connect([this] {
        reload_config();
        rebuild();
        set_status("Reloaded configuration.", false);
    });
    header->append(*refresh);

    auto* close = icon_button("✕", "Close (Esc)");
    close->signal_clicked().connect([this] { hide(); });
    header->append(*close);

    m_card->append(*header);

    auto* subtitle = Gtk::make_managed<Gtk::Label>(
        "Show, hide, focus and rearrange windows stored in Umbriel scratchpads.");
    subtitle->add_css_class("spm-subtle");
    subtitle->set_xalign(0.0f);
    subtitle->set_wrap(true);
    m_card->append(*subtitle);

    m_search = Gtk::make_managed<Gtk::Entry>();
    m_search->add_css_class("spm-search");
    m_search->set_placeholder_text("Filter windows by title or app id…");
    m_search->set_hexpand(true);
    m_search->signal_changed().connect([this] {
        m_filter = m_search->get_text().lowercase();
        rebuild();
    });
    m_card->append(*m_search);

    m_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    m_scroll->add_css_class("spm-scroll");
    m_scroll->set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    m_scroll->set_vexpand(true);
    m_scroll->set_min_content_height(240);
    m_list = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    m_list->set_margin_end(6);
    m_scroll->set_child(*m_list);
    m_card->append(*m_scroll);

    m_prompt = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
    m_prompt->add_css_class("spm-sp");
    m_prompt->set_visible(false);
    m_prompt_label = Gtk::make_managed<Gtk::Label>();
    m_prompt_label->set_xalign(0.0f);
    m_prompt_label->set_wrap(true);
    m_prompt->append(*m_prompt_label);
    m_prompt_body = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    m_prompt->append(*m_prompt_body);
    m_prompt_actions = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    m_prompt->append(*m_prompt_actions);
    m_card->append(*m_prompt);

    auto* footer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    auto* create = primary_button("＋ New scratchpad", "Create a new named scratchpad");
    create->signal_clicked().connect([this] { show_new_scratchpad_form(); });
    footer->append(*create);

    auto* reload = icon_button("Reload config", "Apply configuration changes now");
    reload->signal_clicked().connect([this] {
        after_config_change("Reloaded the Umbriel configuration.");
    });
    footer->append(*reload);

    auto* reveal = icon_button("Open config", "Open the main configuration in your editor");
    reveal->signal_clicked().connect(
        [this] { reveal_file(m_config.primary_config()); });
    footer->append(*reveal);

    m_status = Gtk::make_managed<Gtk::Label>();
    m_status->add_css_class("spm-subtle");
    m_status->set_hexpand(true);
    m_status->set_halign(Gtk::Align::END);
    m_status->set_ellipsize(Pango::EllipsizeMode::END);
    m_status->set_single_line_mode(true);
    footer->append(*m_status);
    m_card->append(*footer);

    auto key = Gtk::EventControllerKey::create();
    key->signal_key_pressed().connect(
        [this](guint keyval, guint /*keycode*/, Gdk::ModifierType /*state*/) {
            if (keyval == GDK_KEY_Escape) {
                if (m_prompt->get_visible()) {
                    hide_prompt();
                } else {
                    hide();
                }
                return true;
            }
            return false;
        },
        false);
    add_controller(key);

    auto gesture = Gtk::GestureClick::create();
    gesture->signal_pressed().connect([this](int /*n_press*/, double x, double y) {
        // Only dismiss when the press landed on the dimmed area, not the card.
        Gtk::Widget* picked = m_backdrop->pick(x, y, Gtk::PickFlags::DEFAULT);
        if (picked != nullptr && (picked == m_card || picked->is_ancestor(*m_card))) {
            return;
        }
        hide();
    });
    m_backdrop->add_controller(gesture);
}

void OverlayWindow::toggle_visible() {
    if (get_visible()) {
        hide();
        return;
    }
    reload_config();
    rebuild();
    present();
    if (m_search != nullptr) {
        m_search->grab_focus();
    }
}

void OverlayWindow::apply_theme(const Theme& theme) {
    m_theme = theme;
    if (m_css != nullptr) {
        m_css->load_from_string(build_css(m_theme));
    }
}

void OverlayWindow::on_windows(std::vector<WindowInfo> windows) {
    m_windows = std::move(windows);
    rebuild();
}

void OverlayWindow::on_connection(bool connected) {
    m_connected = connected;
    if (m_connection == nullptr) {
        return;
    }
    m_connection->set_text(connected ? "connected" : "reconnecting…");
    m_connection->remove_css_class("spm-subtle");
    m_connection->remove_css_class("spm-ok");
    m_connection->remove_css_class("spm-error");
    m_connection->add_css_class(connected ? "spm-ok" : "spm-error");
}

void OverlayWindow::on_action_result(std::string action, bool ok, std::string error) {
    if (ok) {
        set_status("✓ " + action, false);
    } else {
        set_status(error.empty() ? ("Failed: " + action) : error, true);
    }
}

void OverlayWindow::reload_config() {
    std::string error;
    if (!m_config.load(&error) && !error.empty()) {
        set_status(error, true);
    }
}

void OverlayWindow::set_status(const Glib::ustring& message, bool error) {
    if (m_status == nullptr) {
        return;
    }
    m_status->set_text(message);
    m_status->remove_css_class("spm-subtle");
    m_status->remove_css_class("spm-ok");
    m_status->remove_css_class("spm-error");
    m_status->add_css_class(error ? "spm-error" : "spm-ok");
}

void OverlayWindow::run(std::string action) {
    m_ipc.send_action(std::move(action));
}

bool OverlayWindow::matches_filter(const WindowInfo& w) const {
    if (m_filter.empty()) {
        return true;
    }
    if (Glib::ustring(w.title).lowercase().find(m_filter) != Glib::ustring::npos) {
        return true;
    }
    return Glib::ustring(w.app_id).lowercase().find(m_filter) != Glib::ustring::npos;
}

bool OverlayWindow::scratchpad_has_matches(const Scratchpad& sp) const {
    return std::any_of(sp.windows.begin(), sp.windows.end(),
                       [this](const WindowInfo& w) { return matches_filter(w); });
}

void OverlayWindow::rebuild() {
    if (m_list == nullptr) {
        return;
    }
    m_scratchpads = build_scratchpads(m_config.defs(), m_windows);
    clear_box(*m_list);

    bool any = false;
    for (const auto& sp : m_scratchpads) {
        if (!m_filter.empty() && !scratchpad_has_matches(sp)) {
            continue;
        }
        m_list->append(*build_scratchpad_card(sp));
        any = true;
    }

    if (!any) {
        auto* empty = Gtk::make_managed<Gtk::Label>(
            m_filter.empty() ? "No scratchpads are configured."
                             : "No windows match the filter.");
        empty->add_css_class("spm-empty");
        empty->set_xalign(0.0f);
        m_list->append(*empty);
    }
}

Gtk::Widget* OverlayWindow::build_action_menu(const std::string& /*name*/,
                                              std::vector<Action> actions) {
    auto* button = Gtk::make_managed<Gtk::MenuButton>();
    button->set_label("⋮");
    button->add_css_class("spm-icon");
    button->set_focus_on_click(false);

    auto* popover = Gtk::make_managed<Gtk::Popover>();
    popover->set_has_arrow(false);
    popover->add_css_class("spm-menu");
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
    popover->set_child(*box);

    for (auto& [label, handler] : actions) {
        auto* item = Gtk::make_managed<Gtk::Button>(label);
        item->add_css_class("spm-icon");
        item->set_halign(Gtk::Align::FILL);
        item->set_focus_on_click(false);
        item->signal_clicked().connect([popover, fn = std::move(handler)] {
            popover->popdown();
            fn();
        });
        box->append(*item);
    }

    button->set_popover(*popover);
    return button;
}

Gtk::Widget* OverlayWindow::build_scratchpad_card(const Scratchpad& sp) {
    const bool collapsed = m_collapsed.count(sp.name) > 0;
    const int visible = sp.visible_count();
    const std::string name = sp.name;

    auto* outer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
    outer->add_css_class("spm-sp");

    auto* header = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);

    auto* fold = icon_button(collapsed ? "▸" : "▾", collapsed ? "Expand" : "Collapse");
    fold->signal_clicked().connect([this, name, collapsed] {
        if (collapsed) {
            m_collapsed.erase(name);
        } else {
            m_collapsed.insert(name);
        }
        rebuild();
    });
    header->append(*fold);

    auto* name_label = Gtk::make_managed<Gtk::Label>(sp.name);
    name_label->add_css_class("spm-sp-name");
    name_label->add_css_class("spm-mono");
    header->append(*name_label);

    if (sp.implicit) {
        auto* tag = subtle_label("implicit");
        header->append(*tag);
    }

    auto* badge = Gtk::make_managed<Gtk::Label>();
    badge->add_css_class("spm-badge");
    if (sp.windows.empty()) {
        badge->set_text("empty");
        badge->add_css_class("spm-badge-off");
    } else if (visible > 0) {
        badge->set_text(visible == static_cast<int>(sp.windows.size()) ? "visible"
                                                                       : "partly visible");
        badge->add_css_class("spm-badge-on");
    } else {
        badge->set_text("hidden");
        badge->add_css_class("spm-badge-off");
    }
    header->append(*badge);

    header->append(*subtle_label(plural(sp.windows.size(), "window", "windows")));

    auto* spacer = Gtk::make_managed<Gtk::Box>();
    spacer->set_hexpand(true);
    header->append(*spacer);

    auto* toggle = primary_button(visible > 0 ? "Hide" : "Show",
                                  "scratchpad-toggle:" + sp.name);
    toggle->signal_clicked().connect([this, name] { run("scratchpad-toggle:" + name); });
    header->append(*toggle);

    auto* previous = icon_button("◂", "Show only the previous window");
    previous->signal_clicked().connect(
        [this, name] { run("scratchpad-window-show-previous:" + name); });
    header->append(*previous);

    auto* next = icon_button("▸", "Show only the next window");
    next->signal_clicked().connect(
        [this, name] { run("scratchpad-window-show-next:" + name); });
    header->append(*next);

    std::vector<Action> menu;
    menu.emplace_back("Focus next window", [this, name] { run("scratchpad-focus-next:" + name); });
    menu.emplace_back("Store focused window here",
                      [this, name] { run("window-toggle-scratchpad:" + name); });
    menu.emplace_back("Move focused window here",
                      [this, name] { run("window-move-to-scratchpad:" + name); });
    menu.emplace_back("Reveal in configuration", [this, sp] { reveal_source(sp); });
    if (sp.managed) {
        menu.emplace_back("Delete scratchpad…", [this, sp] { confirm_delete(sp); });
    }
    header->append(*build_action_menu(sp.name, std::move(menu)));

    outer->append(*header);

    if (collapsed) {
        return outer;
    }

    if (sp.spawn_when_empty) {
        auto* hint = subtle_label("spawn_when_empty: " + *sp.spawn_when_empty);
        hint->add_css_class("spm-mono");
        hint->set_ellipsize(Pango::EllipsizeMode::END);
        hint->set_single_line_mode(true);
        outer->append(*hint);
    }

    int shown = 0;
    for (const auto& window : sp.windows) {
        if (!matches_filter(window)) {
            continue;
        }
        outer->append(*build_window_row(window, sp));
        ++shown;
    }

    if (shown == 0) {
        Glib::ustring text = "Empty.";
        if (!m_filter.empty()) {
            text = "No windows match the filter.";
        } else if (sp.spawn_when_empty) {
            text = "Empty — showing it runs the command above.";
        } else if (sp.implicit) {
            text = "Empty — move a window here with Mod+Shift+Space.";
        }
        auto* empty = Gtk::make_managed<Gtk::Label>(text);
        empty->add_css_class("spm-empty");
        empty->set_xalign(0.0f);
        empty->set_wrap(true);
        outer->append(*empty);
    }

    return outer;
}

Gtk::Widget* OverlayWindow::build_window_row(const WindowInfo& w, const Scratchpad& sp) {
    auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    row->add_css_class("spm-win");
    if (w.focused) {
        row->add_css_class("spm-win-focused");
    } else if (w.visible) {
        row->add_css_class("spm-win-visible");
    }

    auto* text = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 1);
    text->set_hexpand(true);

    auto* title = Gtk::make_managed<Gtk::Label>();
    title->set_text(w.title.empty() ? Glib::ustring("(untitled)") : Glib::ustring(w.title));
    title->add_css_class("spm-win-title");
    title->set_xalign(0.0f);
    title->set_ellipsize(Pango::EllipsizeMode::END);
    title->set_single_line_mode(true);
    if (w.urgent) {
        title->add_css_class("spm-urgent");
    }
    text->append(*title);

    Glib::ustring meta = w.app_id.empty() ? Glib::ustring("unknown app") : Glib::ustring(w.app_id);
    if (!w.output.empty()) {
        meta += "  ·  " + w.output;
    }
    if (!w.workspace.empty()) {
        meta += "  ·  " + w.workspace;
    }
    if (w.focused) {
        meta += "  ·  focused";
    } else if (w.visible) {
        meta += "  ·  visible";
    }
    if (w.pinned) {
        meta += "  ·  pinned";
    }
    if (w.floating) {
        meta += "  ·  floating";
    }
    auto* meta_label = Gtk::make_managed<Gtk::Label>(meta);
    meta_label->add_css_class("spm-win-meta");
    meta_label->set_xalign(0.0f);
    meta_label->set_ellipsize(Pango::EllipsizeMode::END);
    meta_label->set_single_line_mode(true);
    text->append(*meta_label);
    row->append(*text);

    auto* focus = primary_button("Focus", "window-focus:" + w.id);
    focus->signal_clicked().connect([this, id = w.id] { run("window-focus:" + id); });
    row->append(*focus);

    auto* unstore = icon_button("Unstore", "Return this window to its workspace");
    unstore->signal_clicked().connect([this, id = w.id, name = sp.name] {
        run("window-focus:" + id);
        run("window-restore-from-scratchpad:" + name);
    });
    row->append(*unstore);

    auto* close = icon_button("✕", "Close this window");
    close->signal_clicked().connect([this, id = w.id] { run("window-close:" + id); });
    row->append(*close);

    return row;
}

void OverlayWindow::hide_prompt() {
    if (m_prompt != nullptr) {
        m_prompt->set_visible(false);
    }
    clear_prompt_body();
    if (m_prompt_actions != nullptr) {
        clear_box(*m_prompt_actions);
    }
}

void OverlayWindow::clear_prompt_body() {
    if (m_prompt_body != nullptr) {
        clear_box(*m_prompt_body);
    }
}

void OverlayWindow::set_prompt_actions(std::vector<Action> actions) {
    clear_box(*m_prompt_actions);
    for (auto& [label, handler] : actions) {
        auto* button = primary_button(label, label);
        button->signal_clicked().connect([fn = std::move(handler)] { fn(); });
        m_prompt_actions->append(*button);
    }
}

void OverlayWindow::show_new_scratchpad_form() {
    std::string reason;
    if (!m_config.can_edit(&reason)) {
        hide_prompt();
        set_status(reason, true);
        return;
    }

    clear_prompt_body();
    m_prompt_label->set_text(
        "Create a named scratchpad. Declaring any named scratchpad replaces Umbriel's implicit "
        "\"default\" one, and windows stored in it return to their saved workspaces.");

    auto* name = Gtk::make_managed<Gtk::Entry>();
    name->add_css_class("spm-search");
    name->set_placeholder_text("name — letters, digits, - _ . (\"default\" is reserved)");
    m_prompt_body->append(*name);

    auto* spawn = Gtk::make_managed<Gtk::Entry>();
    spawn->add_css_class("spm-search");
    spawn->set_placeholder_text("optional: command to run when toggled while empty");
    m_prompt_body->append(*spawn);

    set_prompt_actions({
        {"Create",
         [this, name, spawn] {
             commit_new_scratchpad(std::string(name->get_text()), std::string(spawn->get_text()));
         }},
        {"Cancel", [this] { hide_prompt(); }},
    });

    m_prompt->set_visible(true);
    name->grab_focus();
}

void OverlayWindow::commit_new_scratchpad(const std::string& name, const std::string& spawn) {
    if (name.empty()) {
        set_status("Enter a name for the scratchpad.", true);
        return;
    }
    if (m_config.name_exists(name)) {
        set_status("A scratchpad named \"" + name + "\" already exists.", true);
        return;
    }

    if (!m_config.managed_include_present()) {
        const Glib::ustring text =
            "Scratchpads created here are stored in\n" + m_config.managed_path() +
            "\n\nThat file will be added to " + m_config.primary_config() +
            " (a .scratchpad-manager.bak backup is written first). Continue?";
        set_prompt_actions({
            {"Continue", [this, name, spawn] { perform_add(name, spawn); }},
            {"Cancel", [this] { hide_prompt(); }},
        });
        m_prompt_label->set_text(text);
        m_prompt->set_visible(true);
        return;
    }

    perform_add(name, spawn);
}

void OverlayWindow::perform_add(const std::string& name, const std::string& spawn) {
    std::optional<std::string> spawn_command;
    if (!spawn.empty()) {
        spawn_command = spawn;
    }

    std::string error;
    if (!m_config.add_scratchpad(name, spawn_command, &error)) {
        set_status(error, true);
        return;
    }
    after_config_change("Created scratchpad \"" + name + "\".");
}

void OverlayWindow::confirm_delete(const Scratchpad& sp) {
    if (!sp.managed) {
        set_status("Only scratchpads created here can be removed.", true);
        return;
    }
    const std::string name = sp.name;
    clear_prompt_body();
    m_prompt_label->set_text("Delete scratchpad \"" + name + "\"? It is removed from " +
                             m_config.managed_path() +
                             ". Windows stored in it are restored by Umbriel.");
    set_prompt_actions({
        {"Delete",
         [this, name] {
             std::string error;
             if (!m_config.remove_scratchpad(name, &error)) {
                 set_status(error, true);
                 return;
             }
             after_config_change("Removed scratchpad \"" + name + "\".");
         }},
        {"Cancel", [this] { hide_prompt(); }},
    });
    m_prompt->set_visible(true);
}

void OverlayWindow::after_config_change(const Glib::ustring& message) {
    std::string error;
    m_config.trigger_reload(&error);
    reload_config();
    rebuild();
    hide_prompt();
    set_status(message, false);
}

void OverlayWindow::reveal_source(const Scratchpad& sp) {
    std::string path;
    if (sp.managed) {
        path = m_config.managed_path();
    } else if (!sp.sources.empty()) {
        path = sp.sources.front().file;
    } else {
        path = m_config.primary_config();
    }
    if (path.empty()) {
        set_status("No configuration file to open.", true);
        return;
    }
    reveal_file(path);
}

void OverlayWindow::reveal_file(const std::string& path) {
    if (path.empty()) {
        return;
    }
    try {
        Gio::AppInfo::launch_default_for_uri("file://" + path);
    } catch (const Glib::Error&) {
        // No handler configured; ignore.
    }
}

}  // namespace spm
