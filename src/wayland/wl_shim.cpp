// SPDX-License-Identifier: MIT
#include "wayland/wl_shim.hpp"

#include <dlfcn.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

namespace paddeck::wl_shim {
namespace {

using MarshalArrayFlagsFn = struct wl_proxy* (*)(struct wl_proxy*, uint32_t,
                                                 const struct wl_interface*,
                                                 uint32_t, uint32_t,
                                                 union wl_argument*);
using ProxyDestroyFn = void (*)(struct wl_proxy*);
using ProxyAddListenerFn = int (*)(struct wl_proxy*, void (**)(void), void*);
using ProxyGetVersionFn = uint32_t (*)(struct wl_proxy*);
using ProxySetQueueFn = void (*)(struct wl_proxy*, struct wl_event_queue*);
using ProxyGetQueueFn = struct wl_event_queue* (*)(const struct wl_proxy*);
using ProxyGetUserDataFn = void* (*)(struct wl_proxy*);
using ProxySetUserDataFn = void (*)(struct wl_proxy*, void*);
using ProxyGetDisplayFn = struct wl_display* (*)(struct wl_proxy*);

MarshalArrayFlagsFn g_marshal_array_flags = nullptr;
ProxyDestroyFn g_proxy_destroy = nullptr;
ProxyAddListenerFn g_proxy_add_listener = nullptr;
ProxyGetVersionFn g_proxy_get_version = nullptr;
ProxySetQueueFn g_proxy_set_queue = nullptr;
ProxyGetQueueFn g_proxy_get_queue = nullptr;
ProxyGetUserDataFn g_proxy_get_user_data = nullptr;
ProxySetUserDataFn g_proxy_set_user_data = nullptr;
ProxyGetDisplayFn g_proxy_get_display = nullptr;
std::atomic<bool> g_resolved{false};
std::once_flag g_resolve_once;

void* resolve_symbol(const char* name) {
    // Look the symbol up on the library itself rather than with RTLD_NEXT:
    // RTLD_NEXT is relative to the calling object and would be ambiguous for
    // the main executable, where these very overrides live.
    void* handle = dlopen("libwayland-client.so.0", RTLD_NOW | RTLD_NOLOAD);
    if (handle) {
        if (void* symbol = dlsym(handle, name)) return symbol;
    }
    return dlsym(RTLD_NEXT, name);
}

void resolve() {
    if (g_resolved.load(std::memory_order_acquire)) return;
    std::call_once(g_resolve_once, [] {
        g_marshal_array_flags = reinterpret_cast<MarshalArrayFlagsFn>(
            resolve_symbol("wl_proxy_marshal_array_flags"));
        g_proxy_destroy = reinterpret_cast<ProxyDestroyFn>(
            resolve_symbol("wl_proxy_destroy"));
        g_proxy_add_listener = reinterpret_cast<ProxyAddListenerFn>(
            resolve_symbol("wl_proxy_add_listener"));
        g_proxy_get_version = reinterpret_cast<ProxyGetVersionFn>(
            resolve_symbol("wl_proxy_get_version"));
        g_proxy_set_queue = reinterpret_cast<ProxySetQueueFn>(
            resolve_symbol("wl_proxy_set_queue"));
        g_proxy_get_queue = reinterpret_cast<ProxyGetQueueFn>(
            resolve_symbol("wl_proxy_get_queue"));
        g_proxy_get_user_data = reinterpret_cast<ProxyGetUserDataFn>(
            resolve_symbol("wl_proxy_get_user_data"));
        g_proxy_set_user_data = reinterpret_cast<ProxySetUserDataFn>(
            resolve_symbol("wl_proxy_set_user_data"));
        g_proxy_get_display = reinterpret_cast<ProxyGetDisplayFn>(
            resolve_symbol("wl_proxy_get_display"));

        if (!g_marshal_array_flags || !g_proxy_destroy ||
            !g_proxy_add_listener) {
            std::fprintf(stderr,
                         "paddeck: cannot resolve libwayland symbols; layer "
                         "shell is unavailable\n");
            std::abort();
        }
        g_resolved.store(true, std::memory_order_release);
    });
}

struct FakeProxy {
    const struct wl_interface* interface = nullptr;
    uint32_t version = 1;
    struct wl_display* display = nullptr;
    struct wl_event_queue* queue = nullptr;
    void* data = nullptr;
    RequestHandler request = nullptr;
    DestroyHandler destroy = nullptr;
    const void* implementation = nullptr;
    void* user_data = nullptr;
    bool destroyed = false;
};

std::vector<FakeProxy*> g_fake_proxies;
// The capture client runs on its own thread and marshals requests on its own
// connection, so it also runs through these overrides; the list of stand-ins
// has to be safe to read from both threads.
std::mutex g_fake_proxies_mutex;

FakeProxy* find_fake(struct wl_proxy* proxy) {
    if (!proxy) return nullptr;
    auto* candidate = reinterpret_cast<FakeProxy*>(proxy);
    std::lock_guard<std::mutex> lock(g_fake_proxies_mutex);
    for (FakeProxy* fake : g_fake_proxies) {
        if (fake == candidate) return fake;
    }
    return nullptr;
}

void destroy_fake(FakeProxy* fake) {
    if (fake->destroyed) return;
    fake->destroyed = true;
    if (fake->destroy) {
        fake->destroy(fake->data, reinterpret_cast<struct wl_proxy*>(fake));
    }
    // The memory is intentionally kept: GTK occasionally destroys a proxy
    // through wl_proxy_destroy() after its destroy request, and a stale pointer
    // must stay recognisable rather than being forwarded to libwayland.
    // Layer-shell windows are rare, so the few bytes are not worth the risk.
}

struct Hook {
    const char* interface_name;
    uint32_t opcode;
    RequestHandler handler;
    void* data;
};

std::vector<Hook> g_hooks;

}  // namespace

struct wl_proxy* create_proxy(struct wl_display* display,
                              const struct wl_interface* interface,
                              uint32_t version,
                              RequestHandler handler,
                              DestroyHandler destroy,
                              void* data) {
    auto* fake = new FakeProxy;
    fake->interface = interface;
    fake->version = version;
    fake->display = display;
    fake->request = handler;
    fake->destroy = destroy;
    fake->data = data;
    {
        std::lock_guard<std::mutex> lock(g_fake_proxies_mutex);
        g_fake_proxies.push_back(fake);
    }
    return reinterpret_cast<struct wl_proxy*>(fake);
}

const void* proxy_implementation(struct wl_proxy* proxy) {
    FakeProxy* fake = find_fake(proxy);
    return fake ? fake->implementation : nullptr;
}

void* proxy_user_data(struct wl_proxy* proxy) {
    FakeProxy* fake = find_fake(proxy);
    return fake ? fake->user_data : nullptr;
}

void install_request_hook(const char* interface_name, uint32_t opcode,
                          RequestHandler handler, void* data) {
    g_hooks.push_back(Hook{interface_name, opcode, handler, data});
}

}  // namespace paddeck::wl_shim

// The overrides below must be visible to libwayland itself (its public
// wl_proxy_marshal_flags() funnels through wl_proxy_marshal_array_flags), so
// they are exported via the linker's dynamic list.
extern "C" {

__attribute__((visibility("default"))) struct wl_proxy*
wl_proxy_marshal_array_flags(struct wl_proxy* proxy, uint32_t opcode,
                             const struct wl_interface* created_interface,
                             uint32_t created_version, uint32_t flags,
                             union wl_argument* args) {
    using namespace paddeck::wl_shim;
    resolve();

    if (FakeProxy* fake = find_fake(proxy)) {
        if (fake->destroyed) return nullptr;
        struct wl_proxy* ret = nullptr;
        if (fake->request) {
            fake->request(fake->data, proxy, opcode, created_interface,
                          created_version, flags, args, &ret);
        }
        if (flags & WL_MARSHAL_FLAG_DESTROY) {
            destroy_fake(fake);
            return nullptr;
        }
        if (!ret && created_interface) {
            // The request created an object the shim does not model; return a
            // stand-in so the caller never sees a null new id.
            ret = create_proxy(fake->display, created_interface, created_version,
                               nullptr, nullptr, nullptr);
        }
        return ret;
    }

    for (const Hook& hook : g_hooks) {
        if (hook.opcode != opcode || !created_interface) continue;
        if (std::strcmp(hook.interface_name, created_interface->name) != 0) {
            continue;
        }
        struct wl_proxy* ret = nullptr;
        if (hook.handler(hook.data, proxy, opcode, created_interface,
                         created_version, flags, args, &ret)) {
            return ret;
        }
    }

    return g_marshal_array_flags(proxy, opcode, created_interface,
                                 created_version, flags, args);
}

__attribute__((visibility("default"))) void wl_proxy_destroy(
    struct wl_proxy* proxy) {
    using namespace paddeck::wl_shim;
    resolve();
    if (FakeProxy* fake = find_fake(proxy)) {
        destroy_fake(fake);
        return;
    }
    g_proxy_destroy(proxy);
}

__attribute__((visibility("default"))) int wl_proxy_add_listener(
    struct wl_proxy* proxy, void (**implementation)(void), void* data) {
    using namespace paddeck::wl_shim;
    resolve();
    if (FakeProxy* fake = find_fake(proxy)) {
        fake->implementation = reinterpret_cast<const void*>(implementation);
        fake->user_data = data;
        return 0;
    }
    return g_proxy_add_listener(proxy, implementation, data);
}

__attribute__((visibility("default"))) uint32_t wl_proxy_get_version(
    struct wl_proxy* proxy) {
    using namespace paddeck::wl_shim;
    resolve();
    if (FakeProxy* fake = find_fake(proxy)) return fake->version;
    return g_proxy_get_version ? g_proxy_get_version(proxy) : 1;
}

__attribute__((visibility("default"))) void wl_proxy_set_queue(
    struct wl_proxy* proxy, struct wl_event_queue* queue) {
    using namespace paddeck::wl_shim;
    resolve();
    if (FakeProxy* fake = find_fake(proxy)) {
        fake->queue = queue;
        return;
    }
    if (g_proxy_set_queue) g_proxy_set_queue(proxy, queue);
}

__attribute__((visibility("default"))) struct wl_event_queue*
wl_proxy_get_queue(const struct wl_proxy* proxy) {
    using namespace paddeck::wl_shim;
    resolve();
    if (FakeProxy* fake = find_fake(const_cast<struct wl_proxy*>(proxy))) {
        return fake->queue;
    }
    return g_proxy_get_queue ? g_proxy_get_queue(proxy) : nullptr;
}

__attribute__((visibility("default"))) void* wl_proxy_get_user_data(
    struct wl_proxy* proxy) {
    using namespace paddeck::wl_shim;
    resolve();
    if (FakeProxy* fake = find_fake(proxy)) return fake->user_data;
    return g_proxy_get_user_data ? g_proxy_get_user_data(proxy) : nullptr;
}

__attribute__((visibility("default"))) void wl_proxy_set_user_data(
    struct wl_proxy* proxy, void* user_data) {
    using namespace paddeck::wl_shim;
    resolve();
    if (FakeProxy* fake = find_fake(proxy)) {
        fake->user_data = user_data;
        return;
    }
    if (g_proxy_set_user_data) g_proxy_set_user_data(proxy, user_data);
}

__attribute__((visibility("default"))) struct wl_display*
wl_proxy_get_display(struct wl_proxy* proxy) {
    using namespace paddeck::wl_shim;
    resolve();
    if (FakeProxy* fake = find_fake(proxy)) return fake->display;
    return g_proxy_get_display ? g_proxy_get_display(proxy) : nullptr;
}

}  // extern "C"
