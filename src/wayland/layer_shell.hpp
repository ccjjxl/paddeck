// SPDX-License-Identifier: MIT
#pragma once

#include <memory>
#include <string>

#include <gtkmm/window.h>

namespace paddeck {

/// A small, self-contained implementation of the wlr-layer-shell protocol for
/// the overlay window.
///
/// GTK's Wayland backend insists on giving every toplevel an xdg_toplevel role,
/// and a wl_surface can only have one role.  The implementation therefore
/// intercepts the xdg_surface role assignment with wl_shim and substitutes a
/// local stand-in, while the real surface becomes a zwlr_layer_surface_v1.
///
/// Configuration (namespace, layer, anchors, keyboard mode, ...) is stored and
/// applied when GTK realizes the window, so the setters can be called from the
/// window constructor before presentation.
class LayerShell {
public:
    enum Edge : unsigned {
        EdgeTop = 1u,
        EdgeBottom = 2u,
        EdgeLeft = 4u,
        EdgeRight = 8u,
    };

    enum class Layer : unsigned {
        Background = 0,
        Bottom = 1,
        Top = 2,
        Overlay = 3,
    };

    enum class KeyboardMode : unsigned {
        None = 0,
        Exclusive = 1,
        OnDemand = 2,
    };

    /// True when the running compositor speaks wlr-layer-shell.
    static bool supported();

    /// Attach layer-shell behaviour to @p window.  Must be constructed before
    /// the window is first presented.
    explicit LayerShell(Gtk::Window& window);
    ~LayerShell();

    LayerShell(const LayerShell&) = delete;
    LayerShell& operator=(const LayerShell&) = delete;

    void set_namespace(const std::string& name_space);
    void set_layer(Layer layer);
    void set_anchor(Edge edge, bool anchored);
    void set_exclusive_zone(int zone);
    void set_keyboard_mode(KeyboardMode mode);
    void set_margin(Edge edge, int margin);

private:
    // Defined in layer_shell.cpp; exposed only so the free functions there can
    // hold a pointer to it.
public:
    struct Impl;

private:
    std::unique_ptr<Impl> m_impl;
};

}  // namespace paddeck
