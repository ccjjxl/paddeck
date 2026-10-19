// SPDX-License-Identifier: MIT
#include "wayland/layer_shell.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

#include <gdk/wayland/gdkwayland.h>
#include <gtk/gtk.h>

#include "wayland/wl_shim.hpp"
#include "wlr-layer-shell-unstable-v1-client.h"
#include "xdg-shell-client.h"

namespace paddeck {

namespace {

struct LayerShellGlobal {
    struct wl_display* display = nullptr;
    struct zwlr_layer_shell_v1* shell = nullptr;
    bool checked = false;
};

LayerShellGlobal g_global;

void on_registry_global(void* data, struct wl_registry* registry, uint32_t name,
                        const char* interface, uint32_t version) {
    auto* global = static_cast<LayerShellGlobal*>(data);
    if (global->shell) return;
    if (std::strcmp(interface, zwlr_layer_shell_v1_interface.name) != 0) return;

    const uint32_t bind_version = std::min<uint32_t>(version, 4);
    global->shell = static_cast<struct zwlr_layer_shell_v1*>(
        wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface,
                         bind_version));
}

void on_registry_global_remove(void*, struct wl_registry*, uint32_t) {}

const struct wl_registry_listener kRegistryListener = {
    on_registry_global,
    on_registry_global_remove,
};

/// Bind zwlr_layer_shell_v1 on GDK's own Wayland connection.  Runs once; the
/// result is cached because supported() and the constructor both need it.
struct zwlr_layer_shell_v1* ensure_global() {
    if (g_global.checked) return g_global.shell;
    g_global.checked = true;

    GdkDisplay* gdk_display = gdk_display_get_default();
    if (!gdk_display || !GDK_IS_WAYLAND_DISPLAY(gdk_display)) return nullptr;

    g_global.display = gdk_wayland_display_get_wl_display(gdk_display);
    if (!g_global.display) return nullptr;

    struct wl_registry* registry = wl_display_get_registry(g_global.display);
    if (!registry) return nullptr;
    wl_registry_add_listener(registry, &kRegistryListener, &g_global);
    wl_display_roundtrip(g_global.display);
    wl_registry_destroy(registry);
    return g_global.shell;
}

}  // namespace

struct LayerShell::Impl {
    GtkWindow* window = nullptr;
    struct wl_display* display = nullptr;
    struct zwlr_layer_shell_v1* shell = nullptr;
    struct wl_surface* surface = nullptr;
    struct zwlr_layer_surface_v1* layer_surface = nullptr;

    struct wl_proxy* xdg_surface = nullptr;
    struct wl_proxy* xdg_toplevel = nullptr;
    struct wl_proxy* xdg_popup = nullptr;

    std::string name_space = "paddeck";
    uint32_t layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
    uint32_t anchor = 0;
    int exclusive_zone = 0;
    uint32_t keyboard = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE;
    int margin_top = 0;
    int margin_right = 0;
    int margin_bottom = 0;
    int margin_left = 0;

    bool configured = false;
    uint32_t serial = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

namespace {

std::vector<LayerShell::Impl*> g_instances;
bool g_hook_installed = false;

void create_layer_surface(LayerShell::Impl* impl);
void send_configure(LayerShell::Impl* impl);

void apply_margins(LayerShell::Impl* impl) {
    zwlr_layer_surface_v1_set_margin(impl->layer_surface, impl->margin_top,
                                     impl->margin_right, impl->margin_bottom,
                                     impl->margin_left);
}

void LayerShell_apply_config(LayerShell::Impl* impl) {
    if (!impl->layer_surface) return;
    zwlr_layer_surface_v1_set_anchor(impl->layer_surface, impl->anchor);
    zwlr_layer_surface_v1_set_exclusive_zone(impl->layer_surface,
                                             impl->exclusive_zone);
    zwlr_layer_surface_v1_set_keyboard_interactivity(impl->layer_surface,
                                                     impl->keyboard);
    apply_margins(impl);
}

void layer_surface_configure(void* data,
                             struct zwlr_layer_surface_v1*,
                             uint32_t serial, uint32_t width, uint32_t height) {
    auto* impl = static_cast<LayerShell::Impl*>(data);
    impl->serial = serial;
    impl->width = width;
    impl->height = height;
    impl->configured = true;
    send_configure(impl);
}

void layer_surface_closed(void* data, struct zwlr_layer_surface_v1*) {
    auto* impl = static_cast<LayerShell::Impl*>(data);
    if (impl->window) gtk_widget_set_visible(GTK_WIDGET(impl->window), FALSE);
}

const struct zwlr_layer_surface_v1_listener kLayerSurfaceListener = {
    layer_surface_configure,
    layer_surface_closed,
};

void send_configure(LayerShell::Impl* impl) {
    if (!impl->configured || !impl->xdg_surface || !impl->xdg_toplevel) return;

    const auto* toplevel_listener = static_cast<const struct xdg_toplevel_listener*>(
        wl_shim::proxy_implementation(impl->xdg_toplevel));
    if (toplevel_listener && toplevel_listener->configure) {
        struct wl_array states;
        wl_array_init(&states);
        auto* state = static_cast<uint32_t*>(
            wl_array_add(&states, sizeof(uint32_t)));
        if (state) *state = XDG_TOPLEVEL_STATE_ACTIVATED;
        state = static_cast<uint32_t*>(wl_array_add(&states, sizeof(uint32_t)));
        if (state) *state = XDG_TOPLEVEL_STATE_FULLSCREEN;

        toplevel_listener->configure(
            wl_shim::proxy_user_data(impl->xdg_toplevel),
            reinterpret_cast<struct xdg_toplevel*>(impl->xdg_toplevel),
            static_cast<int32_t>(impl->width), static_cast<int32_t>(impl->height),
            &states);
        wl_array_release(&states);
    }

    const auto* surface_listener = static_cast<const struct xdg_surface_listener*>(
        wl_shim::proxy_implementation(impl->xdg_surface));
    if (surface_listener && surface_listener->configure) {
        surface_listener->configure(
            wl_shim::proxy_user_data(impl->xdg_surface),
            reinterpret_cast<struct xdg_surface*>(impl->xdg_surface), impl->serial);
    }
}

void create_layer_surface(LayerShell::Impl* impl) {
    if (impl->layer_surface || !impl->shell || !impl->surface) return;

    impl->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        impl->shell, impl->surface, nullptr, impl->layer,
        impl->name_space.c_str());
    if (!impl->layer_surface) return;

    zwlr_layer_surface_v1_add_listener(impl->layer_surface,
                                       &kLayerSurfaceListener, impl);
    // 0x0 with anchors on all edges asks the compositor for the full output
    // size; the real size comes back in the configure event.
    zwlr_layer_surface_v1_set_size(impl->layer_surface, 0, 0);
    LayerShell_apply_config(impl);
}

void xdg_toplevel_destroyed(void* data, struct wl_proxy*) {
    auto* impl = static_cast<LayerShell::Impl*>(data);
    impl->xdg_toplevel = nullptr;
}

void xdg_popup_destroyed(void* data, struct wl_proxy*) {
    auto* impl = static_cast<LayerShell::Impl*>(data);
    impl->xdg_popup = nullptr;
}

void xdg_surface_destroyed(void* data, struct wl_proxy*) {
    auto* impl = static_cast<LayerShell::Impl*>(data);
    impl->xdg_surface = nullptr;
    impl->xdg_toplevel = nullptr;
    impl->xdg_popup = nullptr;
}

bool xdg_toplevel_request(void*, struct wl_proxy*, uint32_t, const struct wl_interface*,
                          uint32_t, uint32_t, union wl_argument*,
                          struct wl_proxy**) {
    // Title, app id, min/max size and friends have no meaning for a layer
    // surface.  Swallow them; the request never reaches the compositor.
    return true;
}

bool xdg_popup_request(void*, struct wl_proxy*, uint32_t, const struct wl_interface*,
                       uint32_t, uint32_t, union wl_argument*,
                       struct wl_proxy**) {
    return true;
}

bool xdg_surface_request(void* data, struct wl_proxy*, uint32_t opcode,
                         const struct wl_interface*, uint32_t, uint32_t,
                         union wl_argument* args, struct wl_proxy** ret_proxy) {
    auto* impl = static_cast<LayerShell::Impl*>(data);

    switch (opcode) {
        case XDG_SURFACE_GET_TOPLEVEL:
            impl->xdg_toplevel = wl_shim::create_proxy(
                impl->display, &xdg_toplevel_interface, 1, xdg_toplevel_request,
                xdg_toplevel_destroyed, impl);
            *ret_proxy = impl->xdg_toplevel;
            // A configure may have arrived before GDK asked for the toplevel.
            send_configure(impl);
            return true;
        case XDG_SURFACE_ACK_CONFIGURE:
            if (args && impl->layer_surface && args[0].u == impl->serial) {
                zwlr_layer_surface_v1_ack_configure(impl->layer_surface,
                                                    impl->serial);
            }
            return true;

        case XDG_SURFACE_GET_POPUP:
            // The overlay has no menus or tooltips, but GDK may still create
            // a popup.  Keep it local so no dangling parent is sent.
            impl->xdg_popup = wl_shim::create_proxy(
                impl->display, &xdg_popup_interface, 1, xdg_popup_request,
                xdg_popup_destroyed, impl);
            *ret_proxy = impl->xdg_popup;
            return true;

        default:
            // set_window_geometry and destroy do not need forwarding.
            return true;
    }
}

LayerShell::Impl* find_instance(struct wl_surface* surface) {
    for (LayerShell::Impl* impl : g_instances) {
        if (!impl->window || !surface) continue;
        GdkSurface* gdk_surface = gtk_native_get_surface(GTK_NATIVE(impl->window));
        if (!gdk_surface) continue;
        if (gdk_wayland_surface_get_wl_surface(gdk_surface) == surface) {
            return impl;
        }
    }
    return nullptr;
}

bool on_get_xdg_surface(void*, struct wl_proxy*, uint32_t,
                        const struct wl_interface* created_interface,
                        uint32_t, uint32_t,
                        union wl_argument* args, struct wl_proxy** ret_proxy) {
    if (!created_interface || !args) return false;
    if (std::strcmp(created_interface->name, "xdg_surface") != 0) return false;

    auto* surface = reinterpret_cast<struct wl_surface*>(args[1].o);
    LayerShell::Impl* impl = find_instance(surface);
    if (!impl) return false;

    impl->surface = surface;
    impl->xdg_surface = wl_shim::create_proxy(
        impl->display, &xdg_surface_interface, 1, xdg_surface_request,
        xdg_surface_destroyed, impl);
    *ret_proxy = impl->xdg_surface;
    create_layer_surface(impl);
    return true;
}

}  // namespace

bool LayerShell::supported() { return ensure_global() != nullptr; }

LayerShell::LayerShell(Gtk::Window& window) : m_impl(std::make_unique<Impl>()) {
    m_impl->window = GTK_WINDOW(window.gobj());
    m_impl->shell = ensure_global();
    m_impl->display = g_global.display;

    g_instances.push_back(m_impl.get());

    if (!g_hook_installed) {
        g_hook_installed = true;
        wl_shim::install_request_hook("xdg_surface",
                                      XDG_WM_BASE_GET_XDG_SURFACE,
                                      on_get_xdg_surface, nullptr);
    }
}

LayerShell::~LayerShell() {
    g_instances.erase(
        std::remove(g_instances.begin(), g_instances.end(), m_impl.get()),
        g_instances.end());

    if (m_impl->layer_surface) {
        // The layer surface must be destroyed while its wl_surface still
        // exists; if GTK already tore the surface down, leave it to the
        // compositor.
        if (m_impl->window &&
            gtk_native_get_surface(GTK_NATIVE(m_impl->window)) != nullptr) {
            zwlr_layer_surface_v1_destroy(m_impl->layer_surface);
        }
        m_impl->layer_surface = nullptr;
    }
}

void LayerShell::set_namespace(const std::string& name_space) {
    m_impl->name_space = name_space;
}

void LayerShell::set_layer(Layer layer) {
    m_impl->layer = static_cast<uint32_t>(layer);
}

void LayerShell::set_anchor(Edge edge, bool anchored) {
    if (anchored) {
        m_impl->anchor |= static_cast<uint32_t>(edge);
    } else {
        m_impl->anchor &= ~static_cast<uint32_t>(edge);
    }
    LayerShell_apply_config(m_impl.get());
}

void LayerShell::set_exclusive_zone(int zone) {
    m_impl->exclusive_zone = zone;
    LayerShell_apply_config(m_impl.get());
}

void LayerShell::set_keyboard_mode(KeyboardMode mode) {
    m_impl->keyboard = static_cast<uint32_t>(mode);
    LayerShell_apply_config(m_impl.get());
}

void LayerShell::set_margin(Edge edge, int margin) {
    switch (edge) {
        case EdgeTop:
            m_impl->margin_top = margin;
            break;
        case EdgeBottom:
            m_impl->margin_bottom = margin;
            break;
        case EdgeLeft:
            m_impl->margin_left = margin;
            break;
        case EdgeRight:
            m_impl->margin_right = margin;
            break;
    }
    LayerShell_apply_config(m_impl.get());
}

}  // namespace paddeck
