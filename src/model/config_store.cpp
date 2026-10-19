// SPDX-License-Identifier: MIT
#include "model/config_store.hpp"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <sstream>

#include <toml++/toml.h>

namespace fs = std::filesystem;

namespace spm {

namespace {

/// Expand a leading `~` and `$VAR` / `${VAR}` references.
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
}

bool ConfigStore::load(std::string* error) {
    m_defs.clear();
    m_loaded.clear();
    m_loaded_set.clear();

    if (m_primary.empty()) {
        // No configuration: Umbriel runs on the implicit `default` scratchpad.
        return true;
    }

    parse_file(m_primary, error);
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
            m_defs.push_back(std::move(def));
        }
    }

    const auto* include = tbl["include"].as_table();
    if (include == nullptr) {
        return;
    }

    const auto collect = [&](const toml::node_view<const toml::node>& files_view) {
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
            if (!fs::exists(resolved, exists_ec)) {
                continue;
            }
            parse_file(resolved.string(), error);
        }
    };

    collect((*include)["files"]);
    collect((*include)["optional"]["files"]);
}

}  // namespace spm
