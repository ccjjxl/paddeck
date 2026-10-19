// SPDX-License-Identifier: MIT
// Read-only discovery of the Umbriel configuration: which scratchpads exist.
//
// The manager only ever reads configuration; scratchpads are declared by hand
// in the Umbriel configuration and never written back.
#pragma once

#include <set>
#include <string>
#include <vector>

#include "model/types.hpp"

namespace paddeck {

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

private:
    void resolve_primary();
    void parse_file(const std::string& path, std::string* error);

    std::string m_explicit;
    std::string m_primary;
    std::vector<ScratchpadDef> m_defs;
    std::vector<std::string> m_loaded;
    std::set<std::string> m_loaded_set;
};

}  // namespace paddeck
