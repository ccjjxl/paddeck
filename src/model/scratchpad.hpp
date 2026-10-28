// SPDX-License-Identifier: MIT
// Grouping of configured scratchpads and the windows currently stored in them.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "model/types.hpp"

namespace paddeck {

/// A scratchpad as shown in the UI: configuration plus live membership.
struct Scratchpad {
    /// How Umbriel currently presents the members.
    enum class Presentation {
        hidden,  // no member is on screen
        all,     // every member is on screen
        single,  // the pad is stepping through its members, one at a time
    };

    std::string name;
    std::optional<std::string> spawn_when_empty;
    bool implicit = false;  // the implicit `default` scratchpad
    std::vector<WindowInfo> windows;

    int visible_count() const;
    Presentation presentation() const;
};

/// Build the ordered scratchpad list from configuration definitions and windows.
///
/// With no named definitions Umbriel exposes the implicit `default` scratchpad,
/// which is represented here as an implicit entry.
std::vector<Scratchpad> build_scratchpads(const std::vector<ScratchpadDef>& defs,
                                          const std::vector<WindowInfo>& windows);

/// Remembers the member order of every scratchpad across refreshes.
///
/// Umbriel lists windows most-recently-focused first, so the order of its
/// `windows` payload changes whenever focus moves - and stepping through a
/// scratchpad promotes the window it reveals to the front. Rebuilding the grid
/// straight from that listing would move the very tiles the keys act on, so the
/// order is remembered here instead: an existing member keeps its slot and a
/// window that joins the scratchpad is appended at the end.
class MemberOrder {
public:
    /// Reorder the members of every scratchpad in place.
    void apply(std::vector<Scratchpad>& scratchpads);

private:
    /// Scratchpad name -> window ids, in display order.
    std::map<std::string, std::vector<std::string>> m_order;
};

}  // namespace paddeck
