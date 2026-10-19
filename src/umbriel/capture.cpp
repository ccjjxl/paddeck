// SPDX-License-Identifier: MIT
#include "umbriel/capture.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>

#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>

#include <glibmm/dispatcher.h>
#include <sigc++/connection.h>
#include <wayland-client.h>

#include "ext-foreign-toplevel-list-v1-client.h"
#include "ext-image-capture-source-v1-client.h"
#include "ext-image-copy-capture-v1-client.h"

namespace spm {

namespace {

/// Widest preview we ever hand to the UI; the compositor always captures at
/// native size, so this is what keeps the per-frame CPU cost small. Every
/// thumbnail is letterboxed onto a canvas of exactly this size so that tiles in
/// the grid are uniform no matter the window's aspect ratio.
constexpr int kThumbnailWidth = 330;
constexpr int kThumbnailHeight = 186;

/// How often the visible windows are re-captured while the overlay is open.
constexpr auto kRefreshInterval = std::chrono::milliseconds(4000);

/// Give up on a window for a while after a failed capture.
constexpr auto kFailureBackoff = std::chrono::seconds(20);

constexpr int kSessionTimeoutMs = 1000;
constexpr int kFrameTimeoutMs = 3000;

/// Rank the shm formats we understand; lower is better.
int format_rank(std::uint32_t format) {
    switch (format) {
        case WL_SHM_FORMAT_ARGB8888:
            return 0;
        case WL_SHM_FORMAT_XRGB8888:
            return 1;
        case WL_SHM_FORMAT_ABGR8888:
            return 2;
        case WL_SHM_FORMAT_XBGR8888:
            return 3;
        default:
            return 100;
    }
}

std::uint32_t pick_format(const std::vector<std::uint32_t>& formats) {
    std::uint32_t best = 0;
    int best_rank = 100;
    for (std::uint32_t format : formats) {
        const int rank = format_rank(format);
        if (rank < best_rank) {
            best_rank = rank;
            best = format;
        }
    }
    return best;
}

}  // namespace

struct CaptureClient::Impl {
    /// Buffer constraints and frame progress for a single capture.
    struct Capture {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::vector<std::uint32_t> formats;
        bool constraints_done = false;
        bool stopped = false;
        bool frame_ready = false;
        bool frame_failed = false;
    };

    /// A live foreign toplevel, keyed by Umbriel's window id.
    struct Toplevel {
        Impl* owner = nullptr;
        ext_foreign_toplevel_handle_v1* handle = nullptr;
        std::string identifier;
        std::string title;
        std::string app_id;
        bool complete = false;
    };

    // ---- Wayland state (worker thread only) ----
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_shm* shm = nullptr;
    ext_foreign_toplevel_list_v1* toplevel_list = nullptr;
    ext_foreign_toplevel_image_capture_source_manager_v1* source_manager = nullptr;
    ext_image_copy_capture_manager_v1* capture_manager = nullptr;

    std::uint32_t shm_name = 0;
    std::uint32_t list_name = 0;
    std::uint32_t source_manager_name = 0;
    std::uint32_t capture_manager_name = 0;

    std::map<std::string, Toplevel*> by_id;
    std::map<ext_foreign_toplevel_handle_v1*, Toplevel*> by_handle;

    /// Grow-only shared memory buffer reused by every capture.
    int shm_fd = -1;
    void* shm_map = nullptr;
    std::size_t shm_size = 0;

    std::map<std::string, std::chrono::steady_clock::time_point> retry_after;

    // ---- cross-thread state ----
    std::thread thread;
    std::atomic<bool> stop_requested{false};
    std::atomic<bool> available{false};
    int wake_pipe[2] = {-1, -1};

    mutable std::mutex mutex;
    std::vector<std::string> targets;
    bool invalidate_flag = false;
    std::map<std::string, Thumbnail> pending;

    Glib::Dispatcher dispatcher;
    sigc::connection connection;
    FrameCallback callback;

    // ---------------------------------------------------------------- Wayland

    static void on_registry_global(void* data, wl_registry* /*registry*/, std::uint32_t name,
                                   const char* interface, std::uint32_t /*version*/) {
        auto* self = static_cast<Impl*>(data);
        if (std::strcmp(interface, wl_shm_interface.name) == 0) {
            self->shm_name = name;
        } else if (std::strcmp(interface, ext_foreign_toplevel_list_v1_interface.name) == 0) {
            self->list_name = name;
        } else if (std::strcmp(interface,
                                ext_foreign_toplevel_image_capture_source_manager_v1_interface
                                    .name) == 0) {
            self->source_manager_name = name;
        } else if (std::strcmp(interface, ext_image_copy_capture_manager_v1_interface.name) == 0) {
            self->capture_manager_name = name;
        }
    }

    static void on_registry_global_remove(void*, wl_registry*, std::uint32_t) {}

    static void on_list_toplevel(void* data, ext_foreign_toplevel_list_v1*,
                                 ext_foreign_toplevel_handle_v1* handle) {
        static_cast<Impl*>(data)->add_toplevel(handle);
    }

    static void on_list_finished(void* data, ext_foreign_toplevel_list_v1*) {
        static_cast<Impl*>(data)->toplevel_list = nullptr;
    }

    static void on_handle_closed(void* data, ext_foreign_toplevel_handle_v1*) {
        auto* toplevel = static_cast<Toplevel*>(data);
        toplevel->owner->forget_toplevel(*toplevel);
    }

    static void on_handle_done(void* data, ext_foreign_toplevel_handle_v1*) {
        auto* toplevel = static_cast<Toplevel*>(data);
        toplevel->complete = true;
        if (!toplevel->identifier.empty()) {
            toplevel->owner->by_id[toplevel->identifier] = toplevel;
        }
    }

    static void on_handle_title(void* data, ext_foreign_toplevel_handle_v1*, const char* title) {
        static_cast<Toplevel*>(data)->title = title != nullptr ? title : "";
    }

    static void on_handle_app_id(void* data, ext_foreign_toplevel_handle_v1*, const char* app_id) {
        static_cast<Toplevel*>(data)->app_id = app_id != nullptr ? app_id : "";
    }

    static void on_handle_identifier(void* data, ext_foreign_toplevel_handle_v1*,
                                     const char* identifier) {
        auto* toplevel = static_cast<Toplevel*>(data);
        const std::string id = identifier != nullptr ? identifier : "";
        if (id.empty() || id == toplevel->identifier) {
            return;
        }
        if (!toplevel->identifier.empty()) {
            toplevel->owner->by_id.erase(toplevel->identifier);
        }
        toplevel->identifier = id;
        toplevel->owner->by_id[id] = toplevel;
    }

    static void on_capture_buffer_size(void* data, ext_image_copy_capture_session_v1*,
                                       std::uint32_t width, std::uint32_t height) {
        auto* capture = static_cast<Capture*>(data);
        capture->width = width;
        capture->height = height;
    }

    static void on_capture_shm_format(void* data, ext_image_copy_capture_session_v1*,
                                      std::uint32_t format) {
        static_cast<Capture*>(data)->formats.push_back(format);
    }

    static void on_capture_dmabuf_device(void*, ext_image_copy_capture_session_v1*, wl_array*) {}

    static void on_capture_dmabuf_format(void*, ext_image_copy_capture_session_v1*, std::uint32_t,
                                         wl_array*) {}

    static void on_capture_done(void* data, ext_image_copy_capture_session_v1*) {
        static_cast<Capture*>(data)->constraints_done = true;
    }

    static void on_capture_stopped(void* data, ext_image_copy_capture_session_v1*) {
        static_cast<Capture*>(data)->stopped = true;
    }

    static void on_frame_transform(void*, ext_image_copy_capture_frame_v1*, std::uint32_t) {}

    static void on_frame_damage(void*, ext_image_copy_capture_frame_v1*, std::int32_t, std::int32_t,
                                std::int32_t, std::int32_t) {}

    static void on_frame_presentation_time(void*, ext_image_copy_capture_frame_v1*, std::uint32_t,
                                           std::uint32_t, std::uint32_t) {}

    static void on_frame_ready(void* data, ext_image_copy_capture_frame_v1*) {
        static_cast<Capture*>(data)->frame_ready = true;
    }

    static void on_frame_failed(void* data, ext_image_copy_capture_frame_v1*, std::uint32_t) {
        static_cast<Capture*>(data)->frame_failed = true;
    }

    const wl_registry_listener registry_listener = {
        &Impl::on_registry_global,
        &Impl::on_registry_global_remove,
    };
    const ext_foreign_toplevel_list_v1_listener list_listener = {
        &Impl::on_list_toplevel,
        &Impl::on_list_finished,
    };
    const ext_foreign_toplevel_handle_v1_listener handle_listener = {
        &Impl::on_handle_closed,
        &Impl::on_handle_done,
        &Impl::on_handle_title,
        &Impl::on_handle_app_id,
        &Impl::on_handle_identifier,
    };
    const ext_image_copy_capture_session_v1_listener capture_listener = {
        &Impl::on_capture_buffer_size,
        &Impl::on_capture_shm_format,
        &Impl::on_capture_dmabuf_device,
        &Impl::on_capture_dmabuf_format,
        &Impl::on_capture_done,
        &Impl::on_capture_stopped,
    };
    const ext_image_copy_capture_frame_v1_listener frame_listener = {
        &Impl::on_frame_transform,
        &Impl::on_frame_damage,
        &Impl::on_frame_presentation_time,
        &Impl::on_frame_ready,
        &Impl::on_frame_failed,
    };

    void add_toplevel(ext_foreign_toplevel_handle_v1* handle) {
        auto* toplevel = new Toplevel();
        toplevel->owner = this;
        toplevel->handle = handle;
        by_handle[handle] = toplevel;
        ext_foreign_toplevel_handle_v1_add_listener(handle, &handle_listener, toplevel);
    }

    void forget_toplevel(Toplevel& toplevel) {
        if (!toplevel.identifier.empty()) {
            auto it = by_id.find(toplevel.identifier);
            if (it != by_id.end() && it->second == &toplevel) {
                by_id.erase(it);
            }
        }
        by_handle.erase(toplevel.handle);
        ext_foreign_toplevel_handle_v1_destroy(toplevel.handle);
        delete &toplevel;
    }

    void forget_all() {
        while (!by_handle.empty()) {
            forget_toplevel(*by_handle.begin()->second);
        }
        by_id.clear();
    }

    bool setup() {
        display = wl_display_connect(nullptr);
        if (display == nullptr) {
            return false;
        }
        registry = wl_display_get_registry(display);
        wl_registry_add_listener(registry, &registry_listener, this);
        if (wl_display_roundtrip(display) < 0) {
            return false;
        }
        if (shm_name == 0 || list_name == 0 || source_manager_name == 0 ||
            capture_manager_name == 0) {
            return false;
        }

        shm = static_cast<wl_shm*>(
            wl_registry_bind(registry, shm_name, &wl_shm_interface, 1));
        source_manager = static_cast<ext_foreign_toplevel_image_capture_source_manager_v1*>(
            wl_registry_bind(registry, source_manager_name,
                             &ext_foreign_toplevel_image_capture_source_manager_v1_interface, 1));
        capture_manager = static_cast<ext_image_copy_capture_manager_v1*>(
            wl_registry_bind(registry, capture_manager_name,
                             &ext_image_copy_capture_manager_v1_interface, 1));
        toplevel_list = static_cast<ext_foreign_toplevel_list_v1*>(
            wl_registry_bind(registry, list_name, &ext_foreign_toplevel_list_v1_interface, 1));
        ext_foreign_toplevel_list_v1_add_listener(toplevel_list, &list_listener, this);
        wl_display_flush(display);

        // The initial toplevel list arrives in response to the bind; two
        // roundtrips make sure every handle got its identifier and done event.
        wl_display_roundtrip(display);
        wl_display_roundtrip(display);
        return true;
    }

    // ------------------------------------------------------------ event loop

    void wake() const {
        if (wake_pipe[1] >= 0) {
            const char byte = 1;
            const ssize_t ignored = ::write(wake_pipe[1], &byte, 1);
            (void)ignored;
        }
    }

    void drain_wake() const {
        char buffer[64];
        while (::read(wake_pipe[0], buffer, sizeof(buffer)) > 0) {
        }
    }

    /// Dispatch pending events, waiting up to `timeout_ms` for more.
    void pump(int timeout_ms) {
        wl_display_dispatch_pending(display);
        wl_display_flush(display);

        struct pollfd fds[2] = {
            {wl_display_get_fd(display), POLLIN, 0},
            {wake_pipe[0], POLLIN, 0},
        };
        const int ready = ::poll(fds, 2, timeout_ms);
        if (ready < 0) {
            return;
        }
        if ((fds[1].revents & POLLIN) != 0) {
            drain_wake();
        }
        if ((fds[0].revents & POLLIN) != 0) {
            wl_display_dispatch(display);
        } else {
            wl_display_dispatch_pending(display);
        }
    }

    template <typename Predicate>
    bool wait_for(Predicate done, int timeout_ms) {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        while (!done()) {
            if (stop_requested.load()) {
                return false;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
            pump(20);
        }
        return true;
    }

    // -------------------------------------------------------------- capturing

    bool ensure_shm(std::size_t size) {
        if (shm_map != nullptr && shm_size >= size) {
            return true;
        }
        if (shm_map != nullptr) {
            munmap(shm_map, shm_size);
            shm_map = nullptr;
        }
        if (shm_fd >= 0) {
            close(shm_fd);
            shm_fd = -1;
        }
        shm_size = 0;

        const int fd = memfd_create("scratchpads-umbriel-capture", MFD_CLOEXEC);
        if (fd < 0) {
            return false;
        }
        if (ftruncate(fd, static_cast<off_t>(size)) != 0) {
            close(fd);
            return false;
        }
        void* map = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (map == MAP_FAILED) {
            close(fd);
            return false;
        }
        shm_fd = fd;
        shm_map = map;
        shm_size = size;
        return true;
    }

    Thumbnail to_thumbnail(const std::string& id, const Capture& capture,
                           std::uint32_t format) const {
        Thumbnail thumbnail;
        thumbnail.window_id = id;

        const auto* base = static_cast<const unsigned char*>(shm_map);
        const std::size_t stride = static_cast<std::size_t>(capture.width) * 4;
        const bool bgr = format == WL_SHM_FORMAT_XRGB8888 || format == WL_SHM_FORMAT_ARGB8888;
        const bool has_alpha =
            format == WL_SHM_FORMAT_ARGB8888 || format == WL_SHM_FORMAT_ABGR8888;

        std::vector<unsigned char> rgba(static_cast<std::size_t>(capture.width) * capture.height *
                                        4);
        for (std::uint32_t y = 0; y < capture.height; ++y) {
            const unsigned char* source = base + static_cast<std::size_t>(y) * stride;
            unsigned char* target =
                rgba.data() + static_cast<std::size_t>(y) * capture.width * 4;
            for (std::uint32_t x = 0; x < capture.width; ++x) {
                const unsigned char* pixel = source + static_cast<std::size_t>(x) * 4;
                target[x * 4 + 0] = bgr ? pixel[2] : pixel[0];
                target[x * 4 + 1] = pixel[1];
                target[x * 4 + 2] = bgr ? pixel[0] : pixel[2];
                target[x * 4 + 3] = has_alpha ? pixel[3] : 255;
            }
        }

        auto full = Gdk::Pixbuf::create_from_data(
            rgba.data(), Gdk::Colorspace::RGB, true, 8, static_cast<int>(capture.width),
            static_cast<int>(capture.height), static_cast<int>(stride));
        if (!full) {
            return thumbnail;
        }

        // Scale to fit, never upscaling, then centre the result on a canvas of
        // exactly kThumbnailWidth x kThumbnailHeight.
        const double fit = std::min(
            {static_cast<double>(kThumbnailWidth) / full->get_width(),
             static_cast<double>(kThumbnailHeight) / full->get_height(), 1.0});
        const int width = std::max(1, static_cast<int>(full->get_width() * fit));
        const int height = std::max(1, static_cast<int>(full->get_height() * fit));
        auto scaled = full->scale_simple(width, height, Gdk::InterpType::BILINEAR);
        auto canvas = Gdk::Pixbuf::create(Gdk::Colorspace::RGB, true, 8, kThumbnailWidth,
                                          kThumbnailHeight);
        if (!scaled || !canvas) {
            return thumbnail;
        }
        canvas->fill(0x00000000);

        const int offset_x = (kThumbnailWidth - width) / 2;
        const int offset_y = (kThumbnailHeight - height) / 2;
        const int source_stride = scaled->get_rowstride();
        const int target_stride = canvas->get_rowstride();
        const auto* source = scaled->get_pixels();
        auto* target = canvas->get_pixels();
        for (int row = 0; row < height; ++row) {
            std::memcpy(target + static_cast<std::size_t>(offset_y + row) * target_stride +
                            static_cast<std::size_t>(offset_x) * 4,
                        source + static_cast<std::size_t>(row) * source_stride,
                        static_cast<std::size_t>(width) * 4);
        }

        thumbnail.pixbuf = canvas;
        thumbnail.ok = true;
        return thumbnail;
    }

    Thumbnail capture_one(const std::string& id, Toplevel& toplevel) {
        Thumbnail thumbnail;
        thumbnail.window_id = id;

        auto* source = ext_foreign_toplevel_image_capture_source_manager_v1_create_source(
            source_manager, toplevel.handle);
        if (source == nullptr) {
            return thumbnail;
        }
        auto* session =
            ext_image_copy_capture_manager_v1_create_session(capture_manager, source, 0);
        if (session == nullptr) {
            ext_image_capture_source_v1_destroy(source);
            return thumbnail;
        }

        Capture capture;
        ext_image_copy_capture_session_v1_add_listener(session, &capture_listener, &capture);
        const bool constrained =
            wait_for([&capture] { return capture.constraints_done || capture.stopped; },
                     kSessionTimeoutMs);
        const std::uint32_t format = pick_format(capture.formats);
        if (!constrained || capture.stopped || capture.width == 0 || capture.height == 0 ||
            format == 0) {
            ext_image_copy_capture_session_v1_destroy(session);
            ext_image_capture_source_v1_destroy(source);
            return thumbnail;
        }

        const std::size_t stride = static_cast<std::size_t>(capture.width) * 4;
        const std::size_t bytes = stride * capture.height;
        if (!ensure_shm(bytes)) {
            ext_image_copy_capture_session_v1_destroy(session);
            ext_image_capture_source_v1_destroy(source);
            return thumbnail;
        }

        wl_shm_pool* pool = wl_shm_create_pool(shm, shm_fd, static_cast<std::int32_t>(bytes));
        wl_buffer* buffer =
            wl_shm_pool_create_buffer(pool, 0, static_cast<std::int32_t>(capture.width),
                                      static_cast<std::int32_t>(capture.height),
                                      static_cast<std::int32_t>(stride),
                                      static_cast<std::int32_t>(format));
        auto* frame = ext_image_copy_capture_session_v1_create_frame(session);
        ext_image_copy_capture_frame_v1_add_listener(frame, &frame_listener, &capture);
        ext_image_copy_capture_frame_v1_attach_buffer(frame, buffer);
        ext_image_copy_capture_frame_v1_damage_buffer(frame, 0, 0,
                                                      static_cast<std::int32_t>(capture.width),
                                                      static_cast<std::int32_t>(capture.height));
        ext_image_copy_capture_frame_v1_capture(frame);

        const bool finished =
            wait_for([&capture] { return capture.frame_ready || capture.frame_failed; },
                     kFrameTimeoutMs);
        if (finished && capture.frame_ready) {
            thumbnail = to_thumbnail(id, capture, format);
        }

        ext_image_copy_capture_frame_v1_destroy(frame);
        wl_buffer_destroy(buffer);
        wl_shm_pool_destroy(pool);
        ext_image_copy_capture_session_v1_destroy(session);
        ext_image_capture_source_v1_destroy(source);
        return thumbnail;
    }

    void publish(Thumbnail thumbnail) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            pending[thumbnail.window_id] = std::move(thumbnail);
        }
        dispatcher.emit();
    }

    // ------------------------------------------------------------- main loop

    void run() {
        if (!setup()) {
            available = false;
            return;
        }
        available = true;

        auto last_pass = std::chrono::steady_clock::now() - kRefreshInterval;
        while (!stop_requested.load()) {
            std::vector<std::string> current;
            bool forced = false;
            {
                std::lock_guard<std::mutex> lock(mutex);
                current = targets;
                forced = invalidate_flag;
                invalidate_flag = false;
            }

            const auto now = std::chrono::steady_clock::now();
            if (!current.empty() && (forced || now - last_pass >= kRefreshInterval)) {
                for (const std::string& id : current) {
                    if (stop_requested.load()) {
                        break;
                    }
                    const auto retry = retry_after.find(id);
                    if (!forced && retry != retry_after.end() && now < retry->second) {
                        continue;
                    }
                    auto it = by_id.find(id);
                    if (it == by_id.end()) {
                        continue;
                    }
                    Thumbnail thumbnail = capture_one(id, *it->second);
                    if (thumbnail.ok) {
                        retry_after.erase(id);
                    } else {
                        retry_after[id] = std::chrono::steady_clock::now() + kFailureBackoff;
                    }
                    publish(std::move(thumbnail));
                    drain_wake();
                }
                last_pass = std::chrono::steady_clock::now();
            }

            pump(250);
        }

        forget_all();
        if (toplevel_list != nullptr) {
            ext_foreign_toplevel_list_v1_destroy(toplevel_list);
            toplevel_list = nullptr;
        }
        if (capture_manager != nullptr) {
            ext_image_copy_capture_manager_v1_destroy(capture_manager);
            capture_manager = nullptr;
        }
        if (source_manager != nullptr) {
            ext_foreign_toplevel_image_capture_source_manager_v1_destroy(source_manager);
            source_manager = nullptr;
        }
        if (shm != nullptr) {
            wl_shm_destroy(shm);
            shm = nullptr;
        }
        if (registry != nullptr) {
            wl_registry_destroy(registry);
            registry = nullptr;
        }
        if (display != nullptr) {
            wl_display_disconnect(display);
            display = nullptr;
        }
        if (shm_map != nullptr) {
            munmap(shm_map, shm_size);
            shm_map = nullptr;
        }
        if (shm_fd >= 0) {
            close(shm_fd);
            shm_fd = -1;
        }
    }
};

CaptureClient::CaptureClient() : m_impl(std::make_unique<Impl>()) {}

CaptureClient::~CaptureClient() { stop(); }

void CaptureClient::set_frame_callback(FrameCallback callback) {
    m_impl->callback = std::move(callback);
}

bool CaptureClient::start() {
    if (m_impl->thread.joinable()) {
        return m_impl->available.load();
    }
    if (pipe2(m_impl->wake_pipe, O_CLOEXEC | O_NONBLOCK) != 0) {
        return false;
    }
    // Wayland state is created here and then exclusively used by the worker.
    if (!m_impl->setup()) {
        close(m_impl->wake_pipe[0]);
        close(m_impl->wake_pipe[1]);
        m_impl->wake_pipe[0] = m_impl->wake_pipe[1] = -1;
        m_impl->available = false;
        return false;
    }
    m_impl->available = true;

    m_impl->connection = m_impl->dispatcher.connect([this] {
        std::map<std::string, Thumbnail> ready;
        {
            std::lock_guard<std::mutex> lock(m_impl->mutex);
            ready.swap(m_impl->pending);
        }
        if (!m_impl->callback) {
            return;
        }
        for (auto& [id, thumbnail] : ready) {
            m_impl->callback(std::move(thumbnail));
        }
    });

    m_impl->thread = std::thread([impl = m_impl.get()] { impl->run(); });
    return true;
}

bool CaptureClient::available() const { return m_impl->available.load(); }

void CaptureClient::set_targets(std::vector<std::string> window_ids) {
    std::sort(window_ids.begin(), window_ids.end());
    window_ids.erase(std::unique(window_ids.begin(), window_ids.end()), window_ids.end());
    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        if (m_impl->targets == window_ids) {
            return;
        }
        m_impl->targets = std::move(window_ids);
    }
    m_impl->wake();
}

void CaptureClient::invalidate() {
    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        m_impl->invalidate_flag = true;
    }
    m_impl->wake();
}

void CaptureClient::stop() {
    if (m_impl == nullptr) {
        return;
    }
    m_impl->stop_requested = true;
    m_impl->wake();
    if (m_impl->thread.joinable()) {
        m_impl->thread.join();
    }
    if (m_impl->connection.connected()) {
        m_impl->connection.disconnect();
    }
    if (m_impl->wake_pipe[0] >= 0) {
        close(m_impl->wake_pipe[0]);
        m_impl->wake_pipe[0] = -1;
    }
    if (m_impl->wake_pipe[1] >= 0) {
        close(m_impl->wake_pipe[1]);
        m_impl->wake_pipe[1] = -1;
    }
}

}  // namespace spm
