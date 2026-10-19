// SPDX-License-Identifier: MIT
// Grouping of configured scratchpads and the windows currently stored in them.
#pragma once

#include <string>
#include <vector>

#include "model/types.hpp"

namespace spm {

/// A scratchpad as shown in the UI: configuration plus live membership.
struct Scratchpad {
    std::string name;
    std::optional<std::string> spawn_when_empty;
    std::vector<ScratchpadDef> sources;  // where the name is declared
    bool implicit = false;               // the implicit `default` scratchpad
    bool managed = false;                // fully declared in the managed file
    bool defined = false;                // declared in some config file
    std::vector<WindowInfo> windows;

    bool any_visible() const;
    int visible_count() const;
};

/// Build the ordered scratchpad list from configuration definitions and windows.
///
/// With no named definitions Umbriel exposes the implicit `default` scratchpad,
/// which is represented here as an implicit entry.
std::vector<Scratchpad> build_scratchpads(const std::vector<ScratchpadDef>& defs,
                                          const std::vector<WindowInfo>& windows);

}  // namespace spm
