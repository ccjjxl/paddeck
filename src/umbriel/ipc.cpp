// SPDX-License-Identifier: MIT
#include "umbriel/ipc.hpp"

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

namespace paddeck {

namespace {

int connect_socket(const std::string& path) {
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return -1;
    }
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path)) {
        ::close(fd);
        return -1;
    }
    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

bool write_all(int fd, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

/// Read a single newline-terminated line with an overall timeout.
bool read_line(int fd, int timeout_ms, std::string* line) {
    std::string buf;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (buf.find('\n') == std::string::npos) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   deadline - std::chrono::steady_clock::now())
                                   .count();
        if (remaining <= 0) {
            return false;
        }
        pollfd p{fd, POLLIN, 0};
        const int r = ::poll(&p, 1, static_cast<int>(remaining));
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (r == 0) {
            return false;
        }
        char tmp[16384];
        const ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
        if (n <= 0) {
            return false;
        }
        buf.append(tmp, static_cast<std::size_t>(n));
    }
    const std::size_t nl = buf.find('\n');
    *line = buf.substr(0, nl);
    return true;
}

}  // namespace

std::string umbriel_socket_path() {
    if (const char* socket = std::getenv("UMBRIEL_SOCKET"); socket != nullptr && *socket != '\0') {
        return socket;
    }
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    const char* display = std::getenv("WAYLAND_DISPLAY");
    const std::string dir = (runtime != nullptr && *runtime != '\0') ? runtime : "/run/user/1000";
    const std::string name = (display != nullptr && *display != '\0') ? display : "wayland-0";
    return dir + "/umbriel-" + name + ".sock";
}

namespace {

/// Send one request and parse Umbriel's one-line JSON reply.
bool exchange(const nlohmann::json& request, nlohmann::json* reply, std::string* error) {
    const std::string path = umbriel_socket_path();
    const int fd = connect_socket(path);
    if (fd < 0) {
        if (error != nullptr) {
            *error = "Cannot reach the Umbriel socket at " + path;
        }
        return false;
    }

    if (!write_all(fd, request.dump() + "\n")) {
        ::close(fd);
        if (error != nullptr) {
            *error = "Failed to send the request to Umbriel";
        }
        return false;
    }

    std::string line;
    const bool got_reply = read_line(fd, 5000, &line);
    ::close(fd);
    if (!got_reply) {
        if (error != nullptr) {
            *error = "Umbriel did not answer in time";
        }
        return false;
    }

    auto parsed = nlohmann::json::parse(line, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        if (error != nullptr) {
            *error = "Umbriel sent an unreadable reply";
        }
        return false;
    }
    *reply = std::move(parsed);
    return true;
}

/// Copy an `err` reply into `error`; returns true when the reply is an error.
bool take_reply_error(const nlohmann::json& reply, std::string* error) {
    const auto it = reply.find("err");
    if (it == reply.end() || !it->is_string()) {
        return false;
    }
    if (error != nullptr) {
        *error = it->get<std::string>();
    }
    return true;
}

}  // namespace

nlohmann::json UmbrielAction::request() const {
    nlohmann::json request{{"cmd", "action"}, {"action", name}};
    if (scratchpad) {
        request["scratchpad"] = *scratchpad;
    }
    if (window) {
        request["window"] = *window;
    }
    return request;
}

std::string UmbrielAction::legacy_message() const {
    std::string message = name;
    if (scratchpad) {
        message += ":" + *scratchpad;
    } else if (window) {
        message += ":" + *window;
    }
    return message;
}

std::string UmbrielAction::label() const {
    std::string text = name;
    if (scratchpad) {
        text += " " + *scratchpad;
    } else if (window) {
        text += " " + *window;
    }
    return text;
}

UmbrielAction scratchpad_action(std::string name, std::string scratchpad) {
    UmbrielAction action;
    action.name = std::move(name);
    action.scratchpad = std::move(scratchpad);
    return action;
}

UmbrielAction window_action(std::string name, std::string window) {
    UmbrielAction action;
    action.name = std::move(name);
    action.window = std::move(window);
    return action;
}

bool umbriel_send_action(const UmbrielAction& action, std::string* error) {
    nlohmann::json reply;
    if (!exchange(action.request(), &reply, error)) {
        return false;
    }
    std::string reply_error;
    if (!take_reply_error(reply, &reply_error)) {
        return true;
    }

    // Umbriel before the typed `action` request only accepted `msg` with a
    // `name:argument` string. Keep working there after the new request is
    // rejected as an unknown command.
    if (reply_error == "unknown command: action") {
        nlohmann::json legacy_reply;
        const nlohmann::json legacy_request{
            {"cmd", "msg"}, {"arg", action.legacy_message()}};
        if (!exchange(legacy_request, &legacy_reply, error)) {
            return false;
        }
        return !take_reply_error(legacy_reply, error);
    }
    if (error != nullptr) {
        *error = std::move(reply_error);
    }
    return false;
}

bool umbriel_query(const std::string& command, nlohmann::json* payload, std::string* error) {
    nlohmann::json reply;
    if (!exchange(nlohmann::json{{"cmd", command}}, &reply, error)) {
        return false;
    }
    if (take_reply_error(reply, error)) {
        return false;
    }
    const auto ok = reply.find("ok");
    if (ok == reply.end()) {
        if (error != nullptr) {
            *error = "Umbriel replied without an `ok` field";
        }
        return false;
    }
    if (payload != nullptr) {
        *payload = *ok;
    }
    return true;
}

UmbrielIpc::~UmbrielIpc() { stop(); }

void UmbrielIpc::start() {
    if (m_run.exchange(true)) {
        return;
    }
    m_windows_connection = m_windows_dispatcher.connect(sigc::mem_fun(*this, &UmbrielIpc::on_windows_dispatched));
    m_theme_connection = m_theme_dispatcher.connect(sigc::mem_fun(*this, &UmbrielIpc::on_theme_dispatched));
    m_connection_connection = m_connection_dispatcher.connect(sigc::mem_fun(*this, &UmbrielIpc::on_connection_dispatched));
    m_action_connection = m_action_dispatcher.connect(sigc::mem_fun(*this, &UmbrielIpc::on_action_dispatched));

    m_subscriber = std::thread([this] { subscriber_loop(); });
    m_worker = std::thread([this] { action_loop(); });
}

void UmbrielIpc::stop() {
    if (!m_run.exchange(false)) {
        return;
    }
    m_cv.notify_all();
    if (m_subscriber.joinable()) {
        m_subscriber.join();
    }
    if (m_worker.joinable()) {
        m_worker.join();
    }
    m_windows_connection.disconnect();
    m_theme_connection.disconnect();
    m_connection_connection.disconnect();
    m_action_connection.disconnect();
}

void UmbrielIpc::wait_for(std::chrono::milliseconds duration) {
    std::unique_lock<std::mutex> lock(m_cv_mutex);
    m_cv.wait_for(lock, duration, [this] { return !m_run.load(); });
}

void UmbrielIpc::send_action(UmbrielAction action) {
    {
        std::lock_guard<std::mutex> lock(m_action_mutex);
        m_action_queue.push_back(std::move(action));
    }
    m_cv.notify_all();
}

void UmbrielIpc::set_connected(bool connected) {
    {
        std::lock_guard<std::mutex> lock(m_connection_mutex);
        if (m_connected == connected) {
            return;
        }
        m_connected = connected;
        m_connection_dirty = true;
    }
    m_connection_dispatcher.emit();
}

void UmbrielIpc::subscriber_loop() {
    while (m_run.load()) {
        const std::string path = umbriel_socket_path();
        const int fd = connect_socket(path);
        if (fd < 0) {
            set_connected(false);
            wait_for(std::chrono::milliseconds(1000));
            continue;
        }

        const std::string request =
            nlohmann::json{{"cmd", "subscribe"}, {"events", {"windows", "theme"}}}.dump() + "\n";
        if (!write_all(fd, request)) {
            ::close(fd);
            set_connected(false);
            wait_for(std::chrono::milliseconds(1000));
            continue;
        }
        set_connected(true);

        std::string buffer;
        while (m_run.load()) {
            pollfd p{fd, POLLIN, 0};
            const int r = ::poll(&p, 1, 400);
            if (r < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break;
            }
            if (r == 0) {
                continue;
            }
            if ((p.revents & (POLLHUP | POLLERR | POLLNVAL)) != 0) {
                break;
            }
            char tmp[65536];
            const ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
            if (n == 0) {
                break;
            }
            if (n < 0) {
                if (errno == EINTR || errno == EAGAIN) {
                    continue;
                }
                break;
            }
            buffer.append(tmp, static_cast<std::size_t>(n));
            std::size_t nl = 0;
            while ((nl = buffer.find('\n')) != std::string::npos) {
                const std::string line = buffer.substr(0, nl);
                buffer.erase(0, nl + 1);
                if (!line.empty()) {
                    handle_line(line);
                }
            }
        }
        ::close(fd);
        if (m_run.load()) {
            set_connected(false);
            wait_for(std::chrono::milliseconds(1000));
        }
    }
}

void UmbrielIpc::handle_line(const std::string& line) {
    const auto parsed = nlohmann::json::parse(line, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        return;
    }
    const auto event = parsed.find("event");
    if (event == parsed.end() || !event->is_string()) {
        return;
    }
    const std::string name = event->get<std::string>();
    const auto data = parsed.find("data");
    if (data == parsed.end()) {
        return;
    }

    if (name == "windows") {
        {
            std::lock_guard<std::mutex> lock(m_windows_mutex);
            m_windows = parse_windows(*data);
            m_windows_dirty = true;
        }
        m_windows_dispatcher.emit();
    } else if (name == "theme") {
        {
            std::lock_guard<std::mutex> lock(m_theme_mutex);
            from_json(*data, m_theme);
            m_theme_dirty = true;
        }
        m_theme_dispatcher.emit();
    }
}

void UmbrielIpc::action_loop() {
    while (true) {
        std::optional<UmbrielAction> action;
        {
            std::lock_guard<std::mutex> lock(m_action_mutex);
            if (!m_action_queue.empty()) {
                action = std::move(m_action_queue.front());
                m_action_queue.pop_front();
            }
        }

        if (!action) {
            if (!m_run.load()) {
                break;
            }
            {
                std::unique_lock<std::mutex> lock(m_cv_mutex);
                m_cv.wait_for(lock, std::chrono::milliseconds(100), [this] {
                    return !m_run.load() || !m_action_queue.empty();
                });
            }
            continue;
        }

        std::string error;
        const bool ok = umbriel_send_action(*action, &error);
        {
            std::lock_guard<std::mutex> lock(m_action_mutex);
            m_action_results.push_back(
                ActionResult{action->label(), ok, std::move(error)});
        }
        m_action_dispatcher.emit();
    }
}

void UmbrielIpc::on_windows_dispatched() {
    std::vector<WindowInfo> windows;
    {
        std::lock_guard<std::mutex> lock(m_windows_mutex);
        if (!m_windows_dirty) {
            return;
        }
        m_windows_dirty = false;
        windows = m_windows;
    }
    signal_windows.emit(std::move(windows));
}

void UmbrielIpc::on_theme_dispatched() {
    Theme theme;
    {
        std::lock_guard<std::mutex> lock(m_theme_mutex);
        if (!m_theme_dirty) {
            return;
        }
        m_theme_dirty = false;
        theme = m_theme;
    }
    signal_theme.emit(theme);
}

void UmbrielIpc::on_connection_dispatched() {
    bool connected = false;
    {
        std::lock_guard<std::mutex> lock(m_connection_mutex);
        if (!m_connection_dirty) {
            return;
        }
        m_connection_dirty = false;
        connected = m_connected;
    }
    signal_connection.emit(connected);
}

void UmbrielIpc::on_action_dispatched() {
    std::deque<ActionResult> results;
    {
        std::lock_guard<std::mutex> lock(m_action_mutex);
        results.swap(m_action_results);
    }
    for (const auto& result : results) {
        signal_action_result.emit(result.action, result.ok, result.error);
    }
}

}  // namespace paddeck
