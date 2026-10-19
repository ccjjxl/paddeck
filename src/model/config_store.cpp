// SPDX-License-Identifier: MIT
#include "model/config_store.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>

#include <toml++/toml.h>

#include <unistd.h>

#include "umbriel/ipc.hpp"

namespace fs = std::filesystem;

namespace spm {

namespace {

constexpr const char* kManagedFileName = "scratchpads-managed.toml";

std::string expand_path(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size();) {
        const char c = raw[i];
        if (c == '~' && (i == 0 || raw[i - 1] == '/') &&
            (i + 1 == raw.size() || raw[i + 1] == '/')) {
            const char* home = std::getenv("HOME");
            out += home != nullptr ? home : "";
            ++i;
            continue;
        }
        if (c == '$') {
            std::size_t j = i + 1;
            std::string name;
            if (j < raw.size() && raw[j] == '{') {
                const std::size_t close = raw.find('}', j);
                if (close == std::string::npos) {
                    out += c;
                    ++i;
                    continue;
                }
                name = raw.substr(j + 1, close - j - 1);
                j = close + 1;
            } else {
                while (j < raw.size() &&
                       (std::isalnum(static_cast<unsigned char>(raw[j])) != 0 || raw[j] == '_')) {
                    name += raw[j];
                    ++j;
                }
            }
            if (name.empty()) {
                out += c;
                ++i;
                continue;
            }
            const char* value = std::getenv(name.c_str());
            if (value != nullptr) {
                out += value;
            }
            i = j;
            continue;
        }
        out += c;
        ++i;
    }
    return out;
}

std::string read_file(const std::string& path, bool* ok) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        *ok = false;
        return {};
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    *ok = true;
    return text;
}

bool write_file(const std::string& path, const std::string& text, std::string* error) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error != nullptr) {
            *error = "cannot write " + path;
        }
        return false;
    }
    out << text;
    out.flush();
    if (!out) {
        if (error != nullptr) {
            *error = "failed while writing " + path;
        }
        return false;
    }
    return true;
}

std::string toml_basic_string(const std::string& s) {
    std::string out = "\"";
    for (const char c : s) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out += c;
                break;
        }
    }
    out += "\"";
    return out;
}

bool valid_name(const std::string& name) {
    if (name.empty() || name == "default") {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return std::isalnum(c) != 0 || c == '-' || c == '_' || c == '.';
    });
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

/// Insert @p item into the `files` array of the `[include]` table, preserving the
/// surrounding text and comments. Returns false when there is no `[include]` table.
bool insert_include_item(const std::string& text, const std::string& item, std::string* out) {
    std::vector<std::pair<std::size_t, std::size_t>> lines;
    for (std::size_t pos = 0; pos <= text.size();) {
        const std::size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) {
            lines.emplace_back(pos, text.size());
            break;
        }
        lines.emplace_back(pos, nl);
        pos = nl + 1;
        if (pos > text.size()) {
            break;
        }
    }

    std::size_t header = lines.size();
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto [a, b] = lines[i];
        if (trim(std::string_view(text).substr(a, b - a)) == "[include]") {
            header = i;
            break;
        }
    }
    if (header == lines.size()) {
        return false;
    }

    std::size_t section_end = lines.size();
    for (std::size_t i = header + 1; i < lines.size(); ++i) {
        const auto [a, b] = lines[i];
        const std::string_view t = trim(std::string_view(text).substr(a, b - a));
        if (!t.empty() && t.front() == '[') {
            section_end = i;
            break;
        }
    }

    // Locate `files = [` inside the section.
    std::size_t array_open = std::string::npos;
    for (std::size_t i = header + 1; i < section_end; ++i) {
        const auto [a, b] = lines[i];
        std::string_view line = std::string_view(text).substr(a, b - a);
        const std::string_view t = trim(line);
        if (!t.starts_with("files")) {
            continue;
        }
        std::size_t k = 5;
        while (k < t.size() && (t[k] == ' ' || t[k] == '\t')) {
            ++k;
        }
        if (k >= t.size() || t[k] != '=') {
            continue;
        }
        const std::size_t eq = a + static_cast<std::size_t>(t.data() - line.data()) + k;
        const std::size_t open = text.find('[', eq);
        if (open != std::string::npos) {
            array_open = open;
        }
        break;
    }

    if (array_open == std::string::npos) {
        // No `files` key: add one directly after the header line.
        const std::size_t insert_at = lines[header].second;
        *out = text.substr(0, insert_at) + "\nfiles = [ " + item + " ]" + text.substr(insert_at);
        return true;
    }

    // Find the matching ']', skipping quoted strings.
    std::size_t close = std::string::npos;
    bool in_string = false;
    for (std::size_t i = array_open + 1; i < text.size(); ++i) {
        const char c = text[i];
        if (in_string) {
            if (c == '\\') {
                ++i;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
        } else if (c == ']') {
            close = i;
            break;
        }
    }
    if (close == std::string::npos) {
        return false;
    }

    // Preserve trailing whitespace/newlines between the last item and ']'.
    std::size_t last = close;
    while (last > array_open && (text[last - 1] == ' ' || text[last - 1] == '\t' ||
                                 text[last - 1] == '\n' || text[last - 1] == '\r')) {
        --last;
    }
    const std::string tail = text.substr(last, close - last);
    std::string head = text.substr(0, last);
    const char before = last > array_open ? text[last - 1] : '[';
    if (before == '[') {
        head += " " + item + " ";
    } else if (before == ',') {
        head += " " + item;
    } else {
        head += ", " + item;
    }
    *out = head + tail + text.substr(close);
    return true;
}

}  // namespace

ConfigStore::ConfigStore(std::string explicit_path) : m_explicit(std::move(explicit_path)) {
    resolve_primary();
}

void ConfigStore::resolve_primary() {
    // An explicit path wins.
    if (!m_explicit.empty()) {
        const std::string expanded = expand_path(m_explicit);
        if (fs::exists(expanded)) {
            m_primary = fs::weakly_canonical(expanded).string();
        }
    }

    const auto try_dir = [&](const std::string& dir) {
        if (!m_primary.empty()) {
            return;
        }
        const fs::path candidate = fs::path(dir) / "umbriel" / "config.toml";
        std::error_code ec;
        if (fs::exists(candidate, ec)) {
            m_primary = fs::weakly_canonical(candidate, ec).string();
        }
    };

    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && *xdg != '\0') {
        try_dir(xdg);
    } else if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        try_dir(std::string(home) + "/.config");
    }

    if (const char* dirs = std::getenv("XDG_CONFIG_DIRS"); dirs != nullptr && *dirs != '\0') {
        std::stringstream ss(dirs);
        std::string dir;
        while (std::getline(ss, dir, ':') && m_primary.empty()) {
            if (!dir.empty()) {
                try_dir(dir);
            }
        }
    }

    try_dir("/usr/share");

    if (m_primary.empty()) {
        m_managed.clear();
        return;
    }
    m_managed = (fs::path(m_primary).parent_path() / kManagedFileName).string();
}

bool ConfigStore::load(std::string* error) {
    m_defs.clear();
    m_loaded.clear();
    m_loaded_set.clear();
    m_includes.clear();
    m_include_present = false;

    if (m_primary.empty()) {
        // No configuration: Umbriel runs on the implicit `default` scratchpad.
        return true;
    }

    parse_file(m_primary, error);

    // Is the managed file part of the active include set?
    std::error_code ec;
    const fs::path managed_canonical = fs::weakly_canonical(m_managed, ec);
    for (const auto& inc : m_includes) {
        if (fs::weakly_canonical(inc, ec) == managed_canonical) {
            m_include_present = true;
            break;
        }
    }

    return true;
}

void ConfigStore::parse_file(const std::string& path, std::string* error) {
    std::error_code ec;
    const std::string canonical = fs::weakly_canonical(path, ec).string();
    if (!ec && !canonical.empty()) {
        if (!m_loaded_set.insert(canonical).second) {
            return;
        }
        m_loaded.push_back(canonical);
    } else {
        m_loaded.push_back(path);
    }

    toml::table tbl;
    try {
        tbl = toml::parse_file(path);
    } catch (const toml::parse_error& e) {
        if (error != nullptr) {
            std::ostringstream os;
            os << path << ": " << e.description();
            *error = os.str();
        }
        return;
    }

    if (const auto* sp = tbl["scratchpad"].as_array(); sp != nullptr) {
        for (const auto& node : *sp) {
            const auto* t = node.as_table();
            if (t == nullptr) {
                continue;
            }
            ScratchpadDef def;
            def.name = (*t)["name"].value_or<std::string>("");
            if (const auto v = (*t)["spawn_when_empty"].value<std::string>(); v.has_value()) {
                def.spawn_when_empty = *v;
            }
            def.file = path;
            def.line = static_cast<int>(node.source().begin.line);
            std::error_code dir_ec;
            def.managed = fs::equivalent(fs::path(path), fs::path(m_managed), dir_ec) && !dir_ec;
            m_defs.push_back(std::move(def));
        }
    }

    const auto* include = tbl["include"].as_table();
    if (include == nullptr) {
        return;
    }

    const auto collect = [&](const toml::node_view<const toml::node>& files_view, bool optional) {
        const auto* arr = files_view.as_array();
        if (arr == nullptr) {
            return;
        }
        for (const auto& entry : *arr) {
            const auto value = entry.value<std::string>();
            if (!value.has_value()) {
                continue;
            }
            const std::string expanded = expand_path(*value);
            fs::path resolved(expanded);
            if (resolved.is_relative()) {
                resolved = fs::path(path).parent_path() / resolved;
            }
            std::error_code exists_ec;
            const bool exists = fs::exists(resolved, exists_ec);
            m_includes.push_back(resolved.string());
            if (!exists) {
                if (optional) {
                    continue;
                }
                continue;
            }
            parse_file(resolved.string(), error);
        }
    };

    collect((*include)["files"], false);
    collect((*include)["optional"]["files"], true);
}

bool ConfigStore::name_exists(const std::string& name) const {
    return std::any_of(m_defs.begin(), m_defs.end(),
                       [&](const ScratchpadDef& d) { return d.name == name; });
}

bool ConfigStore::can_edit(std::string* reason) const {
    if (m_primary.empty()) {
        if (reason != nullptr) {
            *reason = "No Umbriel configuration file was found. Create one first, "
                      "for example ~/.config/umbriel/config.toml.";
        }
        return false;
    }
    const fs::path dir = fs::path(m_primary).parent_path();
    if (::access(dir.c_str(), W_OK) != 0) {
        if (reason != nullptr) {
            *reason = "Configuration directory is not writable: " + dir.string() +
                      "\nCreate a user configuration to manage scratchpads.";
        }
        return false;
    }
    return true;
}

bool ConfigStore::ensure_include_installed(std::string* error) {
    if (managed_include_present()) {
        return true;
    }
    return install_include(error);
}

bool ConfigStore::install_include(std::string* error) {
    if (m_primary.empty()) {
        if (error != nullptr) {
            *error = "No Umbriel configuration file found.";
        }
        return false;
    }

    std::string reason;
    if (!can_edit(&reason)) {
        if (error != nullptr) {
            *error = reason;
        }
        return false;
    }

    bool ok = false;
    const std::string text = read_file(m_primary, &ok);
    if (!ok) {
        if (error != nullptr) {
            *error = "Cannot read " + m_primary;
        }
        return false;
    }

    std::string updated;
    if (!insert_include_item(text, toml_basic_string(kManagedFileName), &updated)) {
        updated = text;
        if (!updated.empty() && updated.back() != '\n') {
            updated += '\n';
        }
        updated += "\n[include]\nfiles = [ " + toml_basic_string(kManagedFileName) + " ]\n";
    }

    const std::string backup = m_primary + ".scratchpad-manager.bak";
    std::error_code ec;
    if (!fs::exists(backup, ec)) {
        fs::copy_file(m_primary, backup, fs::copy_options::overwrite_existing, ec);
    }

    if (!write_file(m_primary, updated, error)) {
        return false;
    }

    m_include_present = true;
    m_includes.push_back(m_managed);
    return true;
}

std::vector<ConfigStore::ManagedEntry> ConfigStore::read_managed() const {
    std::vector<ManagedEntry> entries;
    std::error_code ec;
    if (m_managed.empty() || !fs::exists(m_managed, ec)) {
        return entries;
    }
    toml::table tbl;
    try {
        tbl = toml::parse_file(m_managed);
    } catch (const toml::parse_error&) {
        return entries;
    }
    const auto* arr = tbl["scratchpad"].as_array();
    if (arr == nullptr) {
        return entries;
    }
    for (const auto& node : *arr) {
        const auto* t = node.as_table();
        if (t == nullptr) {
            continue;
        }
        ManagedEntry entry;
        entry.name = (*t)["name"].value_or<std::string>("");
        if (const auto v = (*t)["spawn_when_empty"].value<std::string>(); v.has_value()) {
            entry.spawn_when_empty = *v;
        }
        if (!entry.name.empty()) {
            entries.push_back(std::move(entry));
        }
    }
    return entries;
}

bool ConfigStore::write_managed(const std::vector<ManagedEntry>& entries, std::string* error) const {
    if (m_managed.empty()) {
        if (error != nullptr) {
            *error = "No managed configuration path.";
        }
        return false;
    }
    std::ostringstream os;
    os << "# Managed by scratchpads-umbriel. The GUI rewrites this file; avoid hand edits.\n";
    for (const auto& entry : entries) {
        os << "\n[[scratchpad]]\n";
        os << "name = " << toml_basic_string(entry.name) << "\n";
        if (entry.spawn_when_empty.has_value()) {
            os << "spawn_when_empty = " << toml_basic_string(*entry.spawn_when_empty) << "\n";
        }
    }
    return write_file(m_managed, os.str(), error);
}

bool ConfigStore::add_scratchpad(const std::string& name,
                                 const std::optional<std::string>& spawn_when_empty,
                                 std::string* error) {
    if (!can_edit(error)) {
        return false;
    }
    if (!valid_name(name)) {
        if (error != nullptr) {
            *error = name == "default"
                         ? "\"default\" is reserved by Umbriel."
                         : "Use letters, digits, '-', '_' or '.' only.";
        }
        return false;
    }
    if (name_exists(name)) {
        if (error != nullptr) {
            *error = "A scratchpad named \"" + name + "\" already exists.";
        }
        return false;
    }

    auto entries = read_managed();
    ManagedEntry entry;
    entry.name = name;
    if (spawn_when_empty.has_value() && !spawn_when_empty->empty()) {
        entry.spawn_when_empty = *spawn_when_empty;
    }
    entries.push_back(std::move(entry));

    if (!write_managed(entries, error)) {
        return false;
    }
    if (!ensure_include_installed(error)) {
        return false;
    }
    return true;
}

bool ConfigStore::remove_scratchpad(const std::string& name, std::string* error) {
    if (!can_edit(error)) {
        return false;
    }
    auto entries = read_managed();
    const auto before = entries.size();
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [&](const ManagedEntry& e) { return e.name == name; }),
                  entries.end());
    if (entries.size() == before) {
        if (error != nullptr) {
            *error = "\"" + name + "\" is not defined in the managed file.";
        }
        return false;
    }
    return write_managed(entries, error);
}

bool ConfigStore::trigger_reload(std::string* error) {
    return umbriel_send_action("config-reload", error);
}

}  // namespace spm
