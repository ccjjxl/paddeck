// SPDX-License-Identifier: MIT
//
// A very small libwayland request interposer.
//
// GTK's Wayland backend always gives a toplevel the xdg_toplevel role before it
// commits anything.  A layer-shell surface must have the layer_surface role
// instead, and a wl_surface can only ever have one role, so there is no way to
// ask GDK to "skip" the xdg_toplevel.  This shim intercepts the one request
// that assigns the role (xdg_wm_base.get_xdg_surface) and hands GDK a local
// stand-in xdg_surface instead.  Everything GDK sends to that stand-in is
// ignored, while configure events are replayed from the real layer surface.
//
// Only the handful of wl_proxy accessors GTK actually uses are interposed, so
// the opaque struct wl_proxy is never dereferenced here.
#pragma once

#include <cstdint>

#include <wayland-client.h>

namespace paddeck::wl_shim {

/// Passed as the opcode to install_request_hook() to match every request of
/// the interface, not just one.
inline constexpr uint32_t kAnyOpcode = 0xffffffffu;

using RequestHandler = bool (*)(
    void* data,
    struct wl_proxy* proxy,
    uint32_t opcode,
    const struct wl_interface* created_interface,
    uint32_t created_version,
    uint32_t flags,
    union wl_argument* args,
    struct wl_proxy** ret_proxy);

using DestroyHandler = void (*)(void* data, struct wl_proxy* proxy);

/// Intercept requests that create an object of @p interface_name (for example
/// "xdg_surface").  The handler returns true when it handled the request.
void install_request_hook(const char* interface_name, uint32_t opcode,
                          RequestHandler handler, void* data);

/// Create a client-side proxy that is never sent to the compositor.
struct wl_proxy* create_proxy(struct wl_display* display,
                              const struct wl_interface* interface,
                              uint32_t version,
                              RequestHandler handler,
                              DestroyHandler destroy,
                              void* data);

/// The listener and user data registered on a fake proxy with
/// wl_proxy_add_listener; used to replay events into GDK.
const void* proxy_implementation(struct wl_proxy* proxy);
void* proxy_user_data(struct wl_proxy* proxy);

}  // namespace paddeck::wl_shim
