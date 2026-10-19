// SPDX-License-Identifier: MIT
#include "model/types.hpp"

#include <glib.h>

namespace paddeck {

namespace {

std::string get_string(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_string()) {
        return {};
    }
    return to_valid_utf8(it->get<std::string>());
}

bool get_bool(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_boolean() && it->get<bool>();
}

long get_long(const nlohmann::json& j, const char* key) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_number_integer()) {
        return 0;
    }
    return it->get<long>();
}

}  // namespace

std::string to_valid_utf8(const std::string& in) {
    if (g_utf8_validate(in.c_str(), static_cast<gssize>(in.size()), nullptr)) {
        return in;
    }
    char* fixed = g_utf8_make_valid(in.c_str(), static_cast<gssize>(in.size()));
    std::string out = fixed != nullptr ? fixed : std::string{};
    g_free(fixed);
    return out;
}

void from_json(const nlohmann::json& j, WindowInfo& w) {
    if (!j.is_object()) {
        return;
    }
    w.id = get_string(j, "id");
    w.app_id = get_string(j, "app_id");
    w.title = get_string(j, "title");
    w.workspace = get_string(j, "workspace");
    w.output = get_string(j, "output");
    w.scratchpad = get_string(j, "scratchpad");
    w.pid = get_long(j, "pid");
    w.visible = get_bool(j, "visible");
    w.focused = get_bool(j, "focused");
    w.floating = get_bool(j, "floating");
    w.pinned = get_bool(j, "pinned");
    w.fullscreen = get_bool(j, "fullscreen");
    w.urgent = get_bool(j, "urgent");
}

std::vector<WindowInfo> parse_windows(const nlohmann::json& j) {
    std::vector<WindowInfo> windows;
    if (!j.is_array()) {
        return windows;
    }
    windows.reserve(j.size());
    for (const auto& item : j) {
        WindowInfo w;
        from_json(item, w);
        if (!w.id.empty()) {
            windows.push_back(std::move(w));
        }
    }
    return windows;
}

void from_json(const nlohmann::json& j, Theme& t) {
    if (!j.is_object()) {
        return;
    }
    auto color = [](const nlohmann::json& obj, const char* key, std::string& target) {
        const auto it = obj.find(key);
        if (it != obj.end() && it->is_string()) {
            target = it->get<std::string>();
        }
    };
    color(j, "background", t.background);
    color(j, "text_primary", t.text_primary);
    color(j, "text_muted", t.text_muted);
    color(j, "accent_primary", t.accent_primary);
    color(j, "accent_secondary", t.accent_secondary);
    color(j, "warning", t.warning);
    color(j, "error", t.error);
    color(j, "backdrop", t.backdrop);
    color(j, "shadow", t.shadow);

    const auto border = j.find("border");
    if (border != j.end() && border->is_object()) {
        color(*border, "focused", t.border_focused);
        color(*border, "unfocused", t.border_unfocused);
    }

    const auto radius = j.find("corner_radius");
    if (radius != j.end() && radius->is_number_integer()) {
        t.corner_radius = radius->get<int>();
    }
}

}  // namespace paddeck
