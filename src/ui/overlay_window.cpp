// SPDX-License-Identifier: MIT
#include "ui/overlay_window.hpp"

#include <algorithm>
#include <cctype>

#include <gdk/gdkkeysyms.h>

#include <gdkmm/display.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/gestureclick.h>
#include <gtkmm/grid.h>
#include <gtkmm/overlay.h>
#include <gtkmm/styleprovider.h>
#include <gtkmm/version.h>
#include <pangomm.h>

#include "ui/theme.hpp"

namespace paddeck {

namespace {

/// Preview tiles per row. Fixed so every scratchpad keeps the same layout, and
/// every tile the same size, whatever the window count.
constexpr int kColumns = 3;

/// Must match the canvas CaptureClient scales thumbnails onto.
constexpr int kTileWidth = 220;
constexpr int kTileHeight = 124;

/// The always-visible key legend; the overlay is keyboard-only, so the
/// bindings have to be discoverable without a manual.
constexpr const char* kHint =
    "j/k select · space toggle · ←/→ member · enter focus · 1-9 jump · esc close";

Gtk::Label* subtle_label(const Glib::ustring& text) {
    auto* label = Gtk::make_managed<Gtk::Label>(text);
    label->add_css_class("pd-subtle");
    label->set_xalign(0.0f);
    return label;
}

Glib::ustring plural(std::size_t count, const char* singular, const char* many) {
    return std::to_string(count) + " " + (count == 1 ? singular : many);
}

/// A two-letter stand-in shown while a window has no captured image.
Glib::ustring window_monogram(const WindowInfo& w) {
    const std::string& source = w.app_id.empty() ? w.title : w.app_id;
    Glib::ustring monogram;
    bool at_word_start = true;
    for (char ch : source) {
        const bool separator = ch == '-' || ch == '_' || ch == '.' || ch == ' ' || ch == ':';
        if (at_word_start && !separator) {
            monogram += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            if (monogram.size() >= 2) {
                break;
            }
        }
        at_word_start = separator;
    }
    if (monogram.empty()) {
        monogram = "▢";
    }
    return monogram;
}

/// The one-line secondary caption under a window title.
Glib::ustring window_meta(const WindowInfo& w) {
    Glib::ustring meta = w.app_id.empty() ? Glib::ustring("unknown app") : Glib::ustring(w.app_id);
    if (!w.output.empty()) {
        meta += "  ·  " + w.output;
    }
    if (w.pinned) {
        meta += "  ·  pinned";
    }
    if (w.floating) {
        meta += "  ·  floating";
    }
    return meta;
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

    if (use_layer_shell && LayerShell::supported()) {
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

    // Window previews need ext-image-copy-capture-v1; without it the overlay
    // still works, it just stays textual.
    m_capture.set_frame_callback(sigc::mem_fun(*this, &OverlayWindow::on_thumbnail));
    m_capture.start();
    if (!m_capture.available()) {
        set_status("Window previews are unavailable on this compositor.", true);
    }

    property_visible().signal_changed().connect([this] {
        if (get_visible()) {
            m_capture.invalidate();
            update_capture_targets();
        } else {
            m_capture.set_targets({});
        }
    });

    set_hide_on_close(true);
    reload_config();
    rebuild();
}

OverlayWindow::~OverlayWindow() {
    m_capture.stop();
}

void OverlayWindow::setup_layer_shell() {
    m_layer_shell = std::make_unique<LayerShell>(*this);
    m_layer_shell->set_namespace("paddeck");
    m_layer_shell->set_layer(LayerShell::Layer::Overlay);
    m_layer_shell->set_anchor(LayerShell::EdgeTop, true);
    m_layer_shell->set_anchor(LayerShell::EdgeBottom, true);
    m_layer_shell->set_anchor(LayerShell::EdgeLeft, true);
    m_layer_shell->set_anchor(LayerShell::EdgeRight, true);
    m_layer_shell->set_exclusive_zone(0);
    m_layer_shell->set_keyboard_mode(LayerShell::KeyboardMode::Exclusive);
}

void OverlayWindow::build_ui() {
    set_title("Scratchpads");
    set_decorated(false);
    add_css_class("pd-window");

    m_backdrop = Gtk::make_managed<Gtk::CenterBox>();
    m_backdrop->add_css_class("pd-backdrop");
    set_child(*m_backdrop);

    m_card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 10);
    m_card->add_css_class("pd-card");
    m_card->set_halign(Gtk::Align::CENTER);
    m_card->set_valign(Gtk::Align::CENTER);
    m_card->set_size_request(760, -1);
    m_backdrop->set_center_widget(*m_card);

    auto* header = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    auto* title = Gtk::make_managed<Gtk::Label>("Scratchpads");
    title->add_css_class("pd-title");
    title->set_halign(Gtk::Align::START);
    title->set_hexpand(true);
    header->append(*title);

    m_connection = subtle_label("○ connecting…");
    m_connection->set_halign(Gtk::Align::END);
    header->append(*m_connection);
    m_card->append(*header);

    m_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    m_scroll->add_css_class("pd-scroll");
    m_scroll->set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    // Hug the content, but never grow past a comfortable maximum.
    m_scroll->set_propagate_natural_height(true);
    m_scroll->set_max_content_height(640);
    m_scroll->set_min_content_height(0);
    m_scroll->set_vexpand(false);
    m_list = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    m_list->set_margin_end(6);
    m_scroll->set_child(*m_list);
    m_card->append(*m_scroll);

    // Grabbing focus on a card is what scrolls it into view, so the selection
    // can be moved with the list scrolled anywhere. Re-apply once the list is
    // mapped, when the first grab actually works.
    m_list->signal_map().connect([this] { apply_selection(); });

    auto* footer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    auto* hint = subtle_label(kHint);
    hint->add_css_class("pd-hint");
    hint->set_hexpand(true);
    hint->set_ellipsize(Pango::EllipsizeMode::END);
    hint->set_single_line_mode(true);
    footer->append(*hint);

    m_status = subtle_label("");
    m_status->set_halign(Gtk::Align::END);
    m_status->set_ellipsize(Pango::EllipsizeMode::END);
    m_status->set_single_line_mode(true);
    m_status->set_max_width_chars(42);
    footer->append(*m_status);
    m_card->append(*footer);

    // Keys are handled in the capture phase so the list never steals them for
    // its own focus navigation.
    auto key = Gtk::EventControllerKey::create();
    key->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    key->signal_key_pressed().connect(
        [this](guint keyval, guint /*keycode*/, Gdk::ModifierType /*state*/) {
            return handle_key(keyval);
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
    apply_selection();
}

void OverlayWindow::apply_theme(const Theme& theme) {
    m_theme = theme;
    if (m_css != nullptr) {
#if GTKMM_CHECK_VERSION(4, 12, 0)
        m_css->load_from_string(build_css(m_theme));
#else
        // gtkmm < 4.12 has no load_from_string(); load_from_data() does the
        // same thing, and is only deprecated from 4.12 onwards.
        m_css->load_from_data(build_css(m_theme));
#endif
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
    m_connection->set_text(connected ? "● connected" : "○ reconnecting…");
    m_connection->remove_css_class("pd-subtle");
    m_connection->remove_css_class("pd-ok");
    m_connection->remove_css_class("pd-error");
    m_connection->add_css_class(connected ? "pd-ok" : "pd-error");
}

void OverlayWindow::on_action_result(std::string action, bool ok, std::string error) {
    // Success is already visible in the badges and previews; only failures need
    // to be spelled out.
    if (!ok) {
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
    m_status->remove_css_class("pd-subtle");
    m_status->remove_css_class("pd-ok");
    m_status->remove_css_class("pd-error");
    m_status->add_css_class(error ? "pd-error" : "pd-ok");
}

void OverlayWindow::run(std::string action) {
    m_ipc.send_action(std::move(action));
}

const Scratchpad* OverlayWindow::selected() const {
    if (m_selected < 0 || m_selected >= static_cast<int>(m_scratchpads.size())) {
        return nullptr;
    }
    return &m_scratchpads[static_cast<std::size_t>(m_selected)];
}

void OverlayWindow::rebuild() {
    if (m_list == nullptr) {
        return;
    }
    m_scratchpads = build_scratchpads(m_config.defs(), m_windows);
    // Slots point into the widget tree that is about to be torn down.
    m_thumb_slots.clear();
    m_rows.clear();
    clear_box(*m_list);

    // Drop cached previews of windows that no longer exist.
    for (auto it = m_thumb_cache.begin(); it != m_thumb_cache.end();) {
        const bool alive = std::any_of(
            m_windows.begin(), m_windows.end(),
            [&id = it->first](const WindowInfo& w) { return w.id == id; });
        it = alive ? std::next(it) : m_thumb_cache.erase(it);
    }

    for (std::size_t i = 0; i < m_scratchpads.size(); ++i) {
        auto* row = build_scratchpad_card(m_scratchpads[i], static_cast<int>(i));
        m_rows.push_back(row);
        m_list->append(*row);
    }

    if (m_scratchpads.empty()) {
        auto* empty = Gtk::make_managed<Gtk::Label>("No scratchpads are configured.");
        empty->add_css_class("pd-empty");
        empty->set_xalign(0.0f);
        m_list->append(*empty);
    }

    if (m_selected >= static_cast<int>(m_scratchpads.size())) {
        m_selected = m_scratchpads.empty() ? 0 : static_cast<int>(m_scratchpads.size()) - 1;
    }
    apply_selection();
    update_capture_targets();
}

void OverlayWindow::apply_selection() {
    for (std::size_t i = 0; i < m_rows.size(); ++i) {
        Gtk::Widget* row = m_rows[i];
        if (row == nullptr) {
            continue;
        }
        if (static_cast<int>(i) == m_selected) {
            row->add_css_class("pd-sp-selected");
            // Focus is what makes the scrolled window keep the selection visible.
            row->grab_focus();
        } else {
            row->remove_css_class("pd-sp-selected");
        }
    }
}

void OverlayWindow::select_index(int index) {
    if (m_scratchpads.empty()) {
        return;
    }
    const int last = static_cast<int>(m_scratchpads.size()) - 1;
    m_selected = std::clamp(index, 0, last);
    apply_selection();
}

void OverlayWindow::move_selection(int delta) {
    const int count = static_cast<int>(m_scratchpads.size());
    if (count == 0) {
        return;
    }
    int next = (m_selected + delta) % count;
    if (next < 0) {
        next += count;
    }
    select_index(next);
}

void OverlayWindow::toggle_selected() {
    const Scratchpad* sp = selected();
    if (sp == nullptr) {
        return;
    }
    if (sp->windows.empty() && !sp->spawn_when_empty) {
        set_status("Nothing is stored in \"" + sp->name + "\".", false);
        return;
    }
    run("scratchpad-toggle:" + sp->name);
    set_status("Toggled \"" + sp->name + "\".", false);
}

void OverlayWindow::cycle_member_of_selection(int delta) {
    const Scratchpad* sp = selected();
    if (sp == nullptr || sp->windows.empty()) {
        return;
    }
    run(std::string(delta < 0 ? "scratchpad-window-show-previous:"
                              : "scratchpad-window-show-next:") +
        sp->name);
}

void OverlayWindow::focus_selected() {
    const Scratchpad* sp = selected();
    if (sp == nullptr || sp->windows.empty()) {
        return;
    }
    // Prefer the window Umbriel already considers focused, then any visible one,
    // otherwise just the first: `window-focus` reveals it from a hidden
    // scratchpad, so any member is a valid target.
    const WindowInfo* target = nullptr;
    for (const auto& w : sp->windows) {
        if (w.focused) {
            target = &w;
            break;
        }
    }
    if (target == nullptr) {
        for (const auto& w : sp->windows) {
            if (w.visible) {
                target = &w;
                break;
            }
        }
    }
    if (target == nullptr) {
        target = &sp->windows.front();
    }
    run("window-focus:" + target->id);
    hide();
}

void OverlayWindow::select_and_toggle(int index) {
    if (index < 0 || index >= static_cast<int>(m_scratchpads.size())) {
        return;
    }
    select_index(index);
    toggle_selected();
}

bool OverlayWindow::handle_key(guint keyval) {
    switch (keyval) {
        case GDK_KEY_j:
        case GDK_KEY_J:
        case GDK_KEY_Down:
        case GDK_KEY_Tab:
            move_selection(1);
            return true;
        case GDK_KEY_k:
        case GDK_KEY_K:
        case GDK_KEY_Up:
        case GDK_KEY_ISO_Left_Tab:
            move_selection(-1);
            return true;
        case GDK_KEY_Home:
            select_index(0);
            return true;
        case GDK_KEY_End:
            select_index(static_cast<int>(m_scratchpads.size()) - 1);
            return true;
        case GDK_KEY_space:
            toggle_selected();
            return true;
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            focus_selected();
            return true;
        case GDK_KEY_h:
        case GDK_KEY_Left:
            cycle_member_of_selection(-1);
            return true;
        case GDK_KEY_l:
        case GDK_KEY_Right:
            cycle_member_of_selection(1);
            return true;
        case GDK_KEY_Escape:
            hide();
            return true;
        default:
            break;
    }
    if (keyval >= GDK_KEY_1 && keyval <= GDK_KEY_9) {
        select_and_toggle(static_cast<int>(keyval - GDK_KEY_1));
        return true;
    }
    return false;
}

void OverlayWindow::update_capture_targets() {
    if (!get_visible()) {
        m_capture.set_targets({});
        return;
    }
    std::vector<std::string> ids;
    ids.reserve(m_thumb_slots.size());
    for (const auto& [id, slot] : m_thumb_slots) {
        ids.push_back(id);
    }
    m_capture.set_targets(std::move(ids));
}

void OverlayWindow::on_thumbnail(Thumbnail thumbnail) {
    if (!thumbnail.ok || !thumbnail.pixbuf) {
        // The tile keeps its monogram placeholder; there is nothing to show.
        return;
    }
    m_thumb_cache[thumbnail.window_id] = thumbnail.pixbuf;
    auto slot = m_thumb_slots.find(thumbnail.window_id);
    if (slot == m_thumb_slots.end()) {
        return;
    }
    if (slot->second.picture != nullptr) {
        slot->second.picture->set_pixbuf(thumbnail.pixbuf);
    }
    if (slot->second.placeholder != nullptr) {
        slot->second.placeholder->set_visible(false);
    }
}

Gtk::Widget* OverlayWindow::build_scratchpad_card(const Scratchpad& sp, int index) {
    const int visible = sp.visible_count();

    auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
    row->add_css_class("pd-sp");
    // Focusable so grab_focus() can scroll the selection into view; Tab never
    // reaches it because the key controller consumes it.
    row->set_can_focus(true);

    // Clicking a card makes it the selection, so mouse and keyboard agree on
    // what the keybindings will act on.
    auto gesture = Gtk::GestureClick::create();
    gesture->signal_pressed().connect([this, index](int, double, double) { select_index(index); });
    row->add_controller(gesture);

    auto* header = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);

    auto* name = Gtk::make_managed<Gtk::Label>(sp.name);
    name->add_css_class("pd-sp-name");
    name->add_css_class("pd-mono");
    header->append(*name);

    if (sp.implicit) {
        header->append(*subtle_label("implicit"));
    }

    auto* spacer = Gtk::make_managed<Gtk::Box>();
    spacer->set_hexpand(true);
    header->append(*spacer);

    header->append(*subtle_label(plural(sp.windows.size(), "window", "windows")));

    auto* badge = Gtk::make_managed<Gtk::Label>();
    badge->add_css_class("pd-badge");
    if (sp.windows.empty()) {
        badge->set_text("empty");
        badge->add_css_class("pd-badge-off");
    } else if (visible > 0) {
        badge->set_text(visible == static_cast<int>(sp.windows.size()) ? "visible"
                                                                      : "partly visible");
        badge->add_css_class("pd-badge-on");
    } else {
        badge->set_text("hidden");
        badge->add_css_class("pd-badge-off");
    }
    header->append(*badge);

    row->append(*header);

    if (!sp.windows.empty()) {
        auto* grid = Gtk::make_managed<Gtk::Grid>();
        grid->add_css_class("pd-grid");
        // Columns are sized to the tiles (a fixed preview size) rather than
        // stretched, so a tile is never wider than the image it shows and every
        // row lines up with the next.
        grid->set_row_spacing(8);
        grid->set_column_spacing(8);
        grid->set_valign(Gtk::Align::START);

        int shown = 0;
        for (const auto& window : sp.windows) {
            grid->attach(*build_window_tile(window), shown % kColumns, shown / kColumns, 1, 1);
            ++shown;
        }
        row->append(*grid);
    } else {
        Glib::ustring text = "Empty.";
        if (sp.spawn_when_empty) {
            text = "Empty — toggling it runs: " + *sp.spawn_when_empty;
        } else if (sp.implicit) {
            text = "Empty — move a window here with Mod+Shift+Space.";
        }
        auto* empty = Gtk::make_managed<Gtk::Label>(text);
        empty->add_css_class("pd-empty");
        empty->set_xalign(0.0f);
        empty->set_ellipsize(Pango::EllipsizeMode::END);
        empty->set_single_line_mode(true);
        row->append(*empty);
    }

    return row;
}

Gtk::Widget* OverlayWindow::build_window_tile(const WindowInfo& w) {
    const std::string id = w.id;

    auto* tile = Gtk::make_managed<Gtk::Overlay>();
    tile->add_css_class("pd-tile");
    if (w.urgent) {
        tile->add_css_class("pd-tile-urgent");
    } else if (w.focused) {
        tile->add_css_class("pd-tile-focused");
    } else if (w.visible) {
        tile->add_css_class("pd-tile-visible");
    }
    // The tile - not the picture - carries the fixed size, so a window that has
    // no capture yet (or never will) still gets a full-size tile instead of
    // collapsing onto its caption.
    tile->set_size_request(kTileWidth, kTileHeight);
    // Never let a tile expand beyond the preview it shows.
    tile->set_halign(Gtk::Align::START);

    // Clicking a preview jumps to that window and dismisses the overlay, the
    // same thing `enter` does for the selection.
    auto gesture = Gtk::GestureClick::create();
    gesture->signal_released().connect([this, id](int, double, double) {
        run("window-focus:" + id);
        hide();
    });
    tile->add_controller(gesture);

    auto* picture = Gtk::make_managed<Gtk::Picture>();
    picture->add_css_class("pd-tile-shot");
    picture->set_content_fit(Gtk::ContentFit::CONTAIN);
    picture->set_can_shrink(true);
    tile->set_child(*picture);

    // Shown until a captured image arrives, or forever when capture is not
    // possible for this window. Just a monogram: anything longer would be
    // clipped by the caption bar the tile is showing behind.
    auto* placeholder = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
    placeholder->add_css_class("pd-tile-placeholder");
    placeholder->set_halign(Gtk::Align::CENTER);
    placeholder->set_valign(Gtk::Align::CENTER);

    auto* monogram = Gtk::make_managed<Gtk::Label>(window_monogram(w));
    monogram->add_css_class("pd-tile-monogram");
    monogram->set_xalign(0.5f);
    placeholder->append(*monogram);
    tile->add_overlay(*placeholder);

    if (w.urgent || w.focused || w.visible) {
        auto* pill = Gtk::make_managed<Gtk::Label>(
            w.urgent ? "urgent" : (w.focused ? "focused" : "visible"));
        pill->add_css_class("pd-badge");
        pill->add_css_class(w.urgent ? "pd-badge-urgent" : "pd-badge-on");
        pill->set_halign(Gtk::Align::START);
        pill->set_valign(Gtk::Align::START);
        pill->set_margin_start(8);
        pill->set_margin_top(8);
        tile->add_overlay(*pill);
    }

    auto* bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    bar->add_css_class("pd-tile-bar");

    auto* text = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    text->set_hexpand(true);

    auto* title = Gtk::make_managed<Gtk::Label>();
    title->set_text(w.title.empty() ? Glib::ustring("(untitled)") : Glib::ustring(w.title));
    title->add_css_class("pd-tile-title");
    title->set_xalign(0.0f);
    title->set_ellipsize(Pango::EllipsizeMode::END);
    title->set_single_line_mode(true);
    text->append(*title);

    auto* meta = Gtk::make_managed<Gtk::Label>(window_meta(w));
    meta->add_css_class("pd-tile-meta");
    meta->set_xalign(0.0f);
    meta->set_ellipsize(Pango::EllipsizeMode::END);
    meta->set_single_line_mode(true);
    text->append(*meta);
    bar->append(*text);

    bar->set_valign(Gtk::Align::END);
    tile->add_overlay(*bar);

    const auto cached = m_thumb_cache.find(id);
    if (cached != m_thumb_cache.end() && cached->second) {
        picture->set_pixbuf(cached->second);
        placeholder->set_visible(false);
    }
    m_thumb_slots[id] = ThumbSlot{picture, placeholder};
    return tile;
}

}  // namespace paddeck
