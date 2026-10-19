// SPDX-License-Identifier: MIT
#include "ui/theme.hpp"

#include <cctype>
#include <cstdio>
#include <sstream>

namespace spm {

namespace {

int hex_pair(const std::string& s, std::size_t index, int fallback) {
    if (index + 2 > s.size()) {
        return fallback;
    }
    const std::string pair = s.substr(index, 2);
    if (!std::isxdigit(static_cast<unsigned char>(pair[0])) ||
        !std::isxdigit(static_cast<unsigned char>(pair[1]))) {
        return fallback;
    }
    return std::stoi(pair, nullptr, 16);
}

struct Rgba {
    int r = 0;
    int g = 0;
    int b = 0;
    double a = 1.0;
};

Rgba parse_color(const std::string& value) {
    Rgba out;
    std::string hex = value;
    if (!hex.empty() && hex.front() == '#') {
        hex.erase(hex.begin());
    }
    if (hex.size() >= 8) {
        out.r = hex_pair(hex, 0, 0);
        out.g = hex_pair(hex, 2, 0);
        out.b = hex_pair(hex, 4, 0);
        out.a = hex_pair(hex, 6, 255) / 255.0;
    } else if (hex.size() >= 6) {
        out.r = hex_pair(hex, 0, 0);
        out.g = hex_pair(hex, 2, 0);
        out.b = hex_pair(hex, 4, 0);
    } else if (hex.size() >= 3) {
        out.r = hex_pair(hex, 0, 0);
        out.r = (out.r << 4) | out.r;
        out.g = hex_pair(hex, 1, 0);
        out.g = (out.g << 4) | out.g;
        out.b = hex_pair(hex, 2, 0);
        out.b = (out.b << 4) | out.b;
    }
    return out;
}

/// GTK's CSS parser does not accept `#rrggbbaa`, so convert to `rgba(...)`.
std::string css_color(const std::string& value, double alpha_multiplier = 1.0) {
    const Rgba c = parse_color(value);
    double alpha = c.a * alpha_multiplier;
    if (alpha < 0.0) {
        alpha = 0.0;
    }
    if (alpha > 1.0) {
        alpha = 1.0;
    }
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "rgba(%d,%d,%d,%.3f)", c.r, c.g, c.b, alpha);
    return buffer;
}

std::string px(int value) { return std::to_string(value) + "px"; }

}  // namespace

std::string build_css(const Theme& t) {
    const int radius = t.corner_radius > 0 ? t.corner_radius : 10;
    const int inner = radius > 4 ? radius - 4 : 4;

    // The accent green is the theme's "this one is live" colour, the same one
    // the `visible` badge uses, so the selection reads without a legend.
    const std::string selected_edge = css_color(t.accent_secondary);

    std::ostringstream css;
    css << "window.spm-window, .spm-backdrop { background: transparent; }\n";
    css << ".spm-backdrop { background-color: rgba(0,0,0,0.45); }\n";

    css << ".spm-card {"
        << " background-color: " << css_color(t.background) << ";"
        << " border: 1px solid " << css_color(t.border_unfocused, 1.6) << ";"
        << " border-radius: " << px(radius) << ";"
        << " padding: 16px;"
        << " box-shadow: 0 12px 40px " << css_color(t.shadow) << ";"
        << "}\n";

    css << ".spm-title { color: " << css_color(t.text_primary)
        << "; font-weight: bold; font-size: 1.2em; }\n";
    css << ".spm-subtle { color: " << css_color(t.text_muted) << "; font-size: 0.86em; }\n";
    css << ".spm-mono { font-family: monospace; font-size: 0.82em; }\n";
    css << ".spm-hint { color: " << css_color(t.text_muted, 0.8)
        << "; font-size: 0.78em; font-family: monospace; }\n";

    // The selected card is the whole interface: everything the keys do acts on
    // it, so it gets a green border and a slightly brighter surface. The
    // border stays 1px in both states, so moving the selection never shifts
    // the tiles.
    css << ".spm-sp { background-color: " << css_color(t.text_primary, 0.045)
        << "; border: 1px solid " << css_color(t.border_unfocused, 1.4) << ";"
        << " border-radius: " << px(inner) << "; padding: 10px; }\n";
    css << ".spm-sp:focus, .spm-sp:focus-visible { outline: none; }\n";
    css << ".spm-sp-selected { background-color: " << css_color(t.text_primary, 0.075)
        << "; border-color: " << selected_edge << "; }\n";
    css << ".spm-sp-name { color: " << css_color(t.text_primary)
        << "; font-weight: bold; font-size: 1.0em; }\n";
    css << ".spm-sp-selected .spm-sp-name { color: " << selected_edge << "; }\n";

    css << ".spm-badge { border-radius: 999px; padding: 0px 8px; font-size: 0.76em;"
        << " font-weight: bold; box-shadow: 0 1px 4px rgba(0,0,0,0.45); }\n";
    css << ".spm-badge-on { background-color: " << css_color(t.accent_secondary);
    css << "; color: " << css_color(t.background) << "; }\n";
    css << ".spm-badge-off { background-color: " << css_color(t.text_muted, 0.16);
    css << "; color: " << css_color(t.text_muted) << "; }\n";
    css << ".spm-badge-urgent { background-color: " << css_color(t.warning);
    css << "; color: " << css_color(t.background) << "; }\n";

    // Window preview grid: one tile per window, showing the captured image.
    css << ".spm-tile { background-color: " << css_color(t.text_primary, 0.05)
        << "; border: 1px solid " << css_color(t.border_unfocused, 1.5) << ";"
        << " border-radius: " << px(inner) << "; }\n";
    css << ".spm-tile-visible { border-color: " << css_color(t.accent_primary, 0.5) << "; }\n";
    css << ".spm-tile-focused { border-color: " << css_color(t.accent_secondary, 0.9) << "; }\n";
    css << ".spm-tile-urgent { border-color: " << css_color(t.warning, 0.9) << "; }\n";
    css << ".spm-tile-shot { background-color: rgba(0,0,0,0.30); border-radius: "
        << px(inner) << "; }\n";
    css << ".spm-tile-monogram { color: " << css_color(t.text_primary, 0.30)
        << "; font-weight: bold; font-size: 2.6em; }\n";
    css << ".spm-tile-bar { background-color: " << css_color(t.background, 0.94)
        << "; padding: 5px 8px; border-bottom-left-radius: " << px(inner)
        << "; border-bottom-right-radius: " << px(inner) << "; }\n";
    css << ".spm-tile-title { color: " << css_color(t.text_primary) << "; font-size: 0.86em; }\n";
    css << ".spm-tile-meta { color: " << css_color(t.text_muted) << "; font-size: 0.72em; }\n";

    css << ".spm-error { color: " << css_color(t.error) << "; }\n";
    css << ".spm-ok { color: " << css_color(t.accent_secondary) << "; }\n";

    css << ".spm-empty { color: " << css_color(t.text_muted)
        << "; font-style: italic; font-size: 0.86em; padding: 2px 4px; }\n";

    css << "scrolledwindow.spm-scroll { background: none; border: none; }\n";
    css << "scrolledwindow.spm-scroll > viewport { background: none; }\n";

    return css.str();
}

}  // namespace spm
