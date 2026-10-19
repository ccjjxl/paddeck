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

    css << "button.spm-icon { background: none; border: none; box-shadow: none;"
        << " color: " << css_color(t.text_muted) << "; padding: 2px 8px; min-height: 22px;"
        << " border-radius: 6px; }\n";
    css << "button.spm-icon:hover { background-color: " << css_color(t.text_primary, 0.12)
        << "; color: " << css_color(t.text_primary) << "; }\n";
    css << "button.spm-icon:active { background-color: " << css_color(t.text_primary, 0.20)
        << "; }\n";

    css << "button.spm-primary { background: none; border: 1px solid "
        << css_color(t.accent_primary, 0.7) << "; box-shadow: none;"
        << " color: " << css_color(t.accent_primary) << "; padding: 1px 10px;"
        << " min-height: 22px; border-radius: 6px; font-size: 0.86em; }\n";
    css << "button.spm-primary:hover { background-color: " << css_color(t.accent_primary, 0.18)
        << "; }\n";
    css << "button.spm-primary:active { background-color: " << css_color(t.accent_primary, 0.30)
        << "; }\n";

    css << "entry.spm-search { background-color: " << css_color(t.text_primary, 0.06)
        << "; color: " << css_color(t.text_primary) << "; border: 1px solid "
        << css_color(t.border_unfocused, 1.6) << "; border-radius: " << px(inner) << ";"
        << " padding: 4px 8px; }\n";
    css << "entry.spm-search:focus { border-color: " << css_color(t.accent_primary) << "; }\n";

    css << ".spm-sp { background-color: " << css_color(t.text_primary, 0.045)
        << "; border: 1px solid " << css_color(t.border_unfocused, 1.4) << ";"
        << " border-radius: " << px(inner) << "; padding: 10px; }\n";
    css << ".spm-sp-name { color: " << css_color(t.text_primary)
        << "; font-weight: bold; font-size: 1.0em; }\n";

    css << ".spm-badge { border-radius: 999px; padding: 0px 8px; font-size: 0.76em;"
        << " font-weight: bold; }\n";
    css << ".spm-badge-on { background-color: " << css_color(t.accent_secondary);
    css << "; color: " << css_color(t.background) << "; }\n";
    css << ".spm-badge-off { background-color: " << css_color(t.text_muted, 0.16);
    css << "; color: " << css_color(t.text_muted) << "; }\n";

    css << ".spm-win { background-color: " << css_color(t.text_primary, 0.05)
        << "; border-radius: " << px(inner > 6 ? inner - 2 : inner) << "; padding: 7px 9px;"
        << " border-left: 3px solid transparent; }\n";
    css << ".spm-win-visible { border-left-color: " << css_color(t.accent_primary) << "; }\n";
    css << ".spm-win-focused { border-left-color: " << css_color(t.accent_secondary) << "; }\n";
    css << ".spm-win-title { color: " << css_color(t.text_primary) << "; font-size: 0.94em; }\n";
    css << ".spm-win-meta { color: " << css_color(t.text_muted) << "; font-size: 0.78em; }\n";
    css << ".spm-urgent { color: " << css_color(t.warning) << "; }\n";
    css << ".spm-error { color: " << css_color(t.error) << "; }\n";
    css << ".spm-ok { color: " << css_color(t.accent_secondary) << "; }\n";

    css << ".spm-empty { color: " << css_color(t.text_muted)
        << "; font-style: italic; font-size: 0.86em; padding: 2px 4px; }\n";

    css << "scrolledwindow.spm-scroll { background: none; border: none; }\n";
    css << "scrolledwindow.spm-scroll > viewport { background: none; }\n";

    css << "popover.spm-menu { background: none; border: none; box-shadow: none; }\n";
    css << "popover.spm-menu > contents { background-color: " << css_color(t.background)
        << "; border: 1px solid " << css_color(t.border_unfocused, 1.7) << ";"
        << " border-radius: " << px(inner) << "; padding: 6px;"
        << " box-shadow: 0 12px 32px " << css_color(t.shadow) << "; }\n";
    css << "popover.spm-menu > arrow { background: none; border: none; box-shadow: none; }\n";
    css << ".spm-menu-box { min-width: 214px; }\n";
    css << "button.spm-menu-item { background: none; border: none; box-shadow: none;"
        << " color: " << css_color(t.text_primary) << "; padding: 6px 10px; min-height: 26px;"
        << " border-radius: 6px; font-size: 0.92em; }\n";
    css << "button.spm-menu-item:hover { background-color: " << css_color(t.text_primary, 0.10)
        << "; }\n";
    css << "button.spm-menu-item:active { background-color: " << css_color(t.text_primary, 0.16)
        << "; }\n";
    css << "button.spm-menu-item-danger { color: " << css_color(t.error) << "; }\n";
    css << "button.spm-menu-item-danger:hover { background-color: " << css_color(t.error, 0.16)
        << "; }\n";
    css << "button.spm-menu-item-danger:active { background-color: " << css_color(t.error, 0.24)
        << "; }\n";
    css << "separator.spm-menu-separator { background-color: " << css_color(t.text_primary, 0.12)
        << "; min-height: 1px; }\n";
    css << "button.spm-menu-button { font-weight: bold; padding: 2px 7px; }\n";

    return css.str();
}

}  // namespace spm
