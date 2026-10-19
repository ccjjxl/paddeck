// SPDX-License-Identifier: MIT
// Client for the Umbriel IPC socket: live window/theme events and actions.
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <glibmm/dispatcher.h>
#include <sigc++/sigc++.h>

#include "model/types.hpp"

namespace paddeck {

/// Path of the Umbriel IPC socket, following UMBRIEL_SOCKET and XDG defaults.
std::string umbriel_socket_path();

/// Send one action and wait for its reply. Blocking; use a worker thread.
bool umbriel_send_action(const std::string& action, std::string* error);

/// Send one query, for example `windows`, and return its `ok` payload.
bool umbriel_query(const std::string& command, nlohmann::json* payload, std::string* error);

/// Maintains a `subscribe windows,theme` connection and a queue for actions.
///
/// All sigc++ signals are emitted on the thread that called start() (the GTK
/// main thread). Construction must also happen there.
class UmbrielIpc {
public:
    UmbrielIpc() = default;
    ~UmbrielIpc();

    UmbrielIpc(const UmbrielIpc&) = delete;
    UmbrielIpc& operator=(const UmbrielIpc&) = delete;

    void start();
    void stop();

    /// Queue an action, for example `scratchpad-toggle:terminal`.
    void send_action(std::string action);

    sigc::signal<void(std::vector<WindowInfo>)> signal_windows;
    sigc::signal<void(Theme)> signal_theme;
    sigc::signal<void(bool)> signal_connection;
    sigc::signal<void(std::string, bool, std::string)> signal_action_result;

private:
    struct ActionResult {
        std::string action;
        bool ok = false;
        std::string error;
    };

    void subscriber_loop();
    void action_loop();
    void wait_for(std::chrono::milliseconds duration);
    void set_connected(bool connected);
    void handle_line(const std::string& line);

    void on_windows_dispatched();
    void on_theme_dispatched();
    void on_connection_dispatched();
    void on_action_dispatched();

    std::atomic<bool> m_run{false};
    std::thread m_subscriber;
    std::thread m_worker;

    std::mutex m_cv_mutex;
    std::condition_variable m_cv;

    std::mutex m_windows_mutex;
    std::vector<WindowInfo> m_windows;
    bool m_windows_dirty = false;

    std::mutex m_theme_mutex;
    Theme m_theme;
    bool m_theme_dirty = false;

    std::mutex m_connection_mutex;
    bool m_connected = false;
    bool m_connection_dirty = false;

    std::mutex m_action_mutex;
    std::deque<std::string> m_action_queue;
    std::deque<ActionResult> m_action_results;

    Glib::Dispatcher m_windows_dispatcher;
    Glib::Dispatcher m_theme_dispatcher;
    Glib::Dispatcher m_connection_dispatcher;
    Glib::Dispatcher m_action_dispatcher;

    sigc::connection m_windows_connection;
    sigc::connection m_theme_connection;
    sigc::connection m_connection_connection;
    sigc::connection m_action_connection;
};

}  // namespace paddeck
