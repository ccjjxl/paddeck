// SPDX-License-Identifier: MIT
// Discovery and editing of the Umbriel configuration for scratchpads.
#pragma once

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "model/types.hpp"

namespace spm {

class ConfigStore {
public:
    /// @param explicit_path value of --config; empty to use the standard search.
    explicit ConfigStore(std::string explicit_path = {});

    /// Parse the active configuration, following includes, and collect
    /// `[[scratchpad]]` definitions. Safe to call repeatedly.
    /// @return false and fills @p error on a hard failure (unreadable file).
    bool load(std::string* error);

    const std::vector<ScratchpadDef>& defs() const { return m_defs; }
    const std::vector<std::string>& loaded_files() const { return m_loaded; }

    /// The main configuration file, or empty when none exists.
    const std::string& primary_config() const { return m_primary; }

    /// The manager-owned include file that holds GUI-created scratchpads.
    const std::string& managed_path() const { return m_managed; }

    /// True when every scratchpad name in @p name already exists.
    bool name_exists(const std::string& name) const;

    /// True when the managed file is pulled in by the active configuration.
    bool managed_include_present() const { return m_include_present; }

    /// Add `scratchpads-managed.toml` to the main configuration's includes,
    /// preserving comments. Creates a backup first. No-op when already present.
    bool ensure_include_installed(std::string* error);

    /// Append a scratchpad to the managed file.
    bool add_scratchpad(const std::string& name,
                        const std::optional<std::string>& spawn_when_empty,
                        std::string* error);

    /// Remove a scratchpad from the managed file.
    bool remove_scratchpad(const std::string& name, std::string* error);

    /// Whether CRUD is possible (managed file next to a writable main config).
    bool can_edit(std::string* reason) const;

    /// Ask Umbriel to reload its configuration.
    static bool trigger_reload(std::string* error);

private:
    struct ManagedEntry {
        std::string name;
        std::optional<std::string> spawn_when_empty;
    };

    void resolve_primary();
    void parse_file(const std::string& path, std::string* error);
    std::vector<ManagedEntry> read_managed() const;
    bool write_managed(const std::vector<ManagedEntry>& entries, std::string* error) const;
    bool install_include(std::string* error);

    std::string m_explicit;
    std::string m_primary;
    std::string m_managed;
    std::vector<ScratchpadDef> m_defs;
    std::vector<std::string> m_loaded;
    std::set<std::string> m_loaded_set;
    std::vector<std::string> m_includes;
    bool m_include_present = false;
};

}  // namespace spm
