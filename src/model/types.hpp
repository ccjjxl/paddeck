// SPDX-License-Identifier: MIT
// Data types shared across the scratchpad manager.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace paddeck {

/// A window as reported by `umbriel windows` / `subscribe windows`.
struct WindowInfo {
    std::string id;
    std::string app_id;
    std::string title;
    std::string workspace;
    std::string output;
    std::string scratchpad;  // empty when the window is not stored in a scratchpad
    long pid = 0;
    bool visible = false;
    bool focused = false;
    // Holds keyboard focus right now. Unlike `focused` - which is workspace-local
    // and therefore always false for windows parked in a scratchpad - this is the
    // seat-global activation Umbriel reports, so it is what the overlay marks.
    bool active = false;
    bool floating = false;
    bool pinned = false;
    bool fullscreen = false;
    bool urgent = false;
};

/// Parse a window entry (tolerates missing fields).
void from_json(const nlohmann::json& j, WindowInfo& w);

/// Parse the `ok` payload of a `windows` query or the `data` of a windows event.
std::vector<WindowInfo> parse_windows(const nlohmann::json& j);

/// Palette mirrored from Umbriel's `theme` event.
struct Theme {
    std::string background = "#1E1A24FF";
    std::string text_primary = "#F5EDE6FF";
    std::string text_muted = "#C8B8A8FF";
    std::string accent_primary = "#E8A87CFF";
    std::string accent_secondary = "#8FBF6AFF";
    std::string warning = "#E8C87AFF";
    std::string error = "#D96B6BFF";
    std::string backdrop = "#3A3040FF";
    std::string shadow = "#0000007F";
    std::string border_focused = "#E8A87CFF";
    std::string border_unfocused = "#3A3040FF";
    int corner_radius = 10;
};

void from_json(const nlohmann::json& j, Theme& t);

/// A `[[scratchpad]]` entry found in the Umbriel configuration.
struct ScratchpadDef {
    std::string name;
    std::optional<std::string> spawn_when_empty;
};

/// Replace bytes that are not valid UTF-8 so the value is safe for GTK.
std::string to_valid_utf8(const std::string& in);

}  // namespace paddeck
