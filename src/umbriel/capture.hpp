// SPDX-License-Identifier: MIT
// Per-window image capture for the scratchpad overlay.
//
// Umbriel is a wlroots compositor and exposes the ext-image-copy-capture-v1
// family of protocols, which can capture the pixels of an individual toplevel -
// including toplevels that are currently hidden inside a scratchpad. The
// protocol identifiers handed out by ext-foreign-toplevel-list-v1 are exactly
// the window ids Umbriel reports over IPC, so the two views can be matched
// without guessing.
//
// Capturing runs on a worker thread owning its own Wayland connection; finished
// thumbnails are handed to the GTK main thread through a Glib::Dispatcher.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <gdkmm/pixbuf.h>
#include <glibmm/refptr.h>

namespace paddeck {

/// One captured window image, already scaled down for display.
struct Thumbnail {
    std::string window_id;
    Glib::RefPtr<Gdk::Pixbuf> pixbuf;  ///< null when `ok` is false
    bool ok = false;
};

class CaptureClient {
public:
    using FrameCallback = std::function<void(Thumbnail)>;

    CaptureClient();
    ~CaptureClient();

    CaptureClient(const CaptureClient&) = delete;
    CaptureClient& operator=(const CaptureClient&) = delete;

    /// Connect to the compositor and start the worker thread.
    /// Returns false when the required protocols are unavailable; the caller
    /// should then fall back to a text-only presentation.
    bool start();

    bool available() const;

    /// Windows that should stay fresh. An empty list suspends capturing.
    void set_targets(std::vector<std::string> window_ids);

    /// Force the next pass to re-capture every target.
    void invalidate();

    /// Set before start(); invoked on the thread running the GTK main loop.
    void set_frame_callback(FrameCallback callback);

    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace paddeck
