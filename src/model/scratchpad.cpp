// SPDX-License-Identifier: MIT
#include "model/scratchpad.hpp"

#include <algorithm>
#include <unordered_map>

namespace spm {

int Scratchpad::visible_count() const {
    return static_cast<int>(std::count_if(
        windows.begin(), windows.end(), [](const WindowInfo& w) { return w.visible; }));
}

std::vector<Scratchpad> build_scratchpads(const std::vector<ScratchpadDef>& defs,
                                          const std::vector<WindowInfo>& windows) {
    std::vector<Scratchpad> result;
    std::unordered_map<std::string, std::size_t> index;

    auto ensure = [&](const std::string& name) -> Scratchpad& {
        const auto it = index.find(name);
        if (it != index.end()) {
            return result[it->second];
        }
        Scratchpad sp;
        sp.name = name;
        result.push_back(std::move(sp));
        index.emplace(name, result.size() - 1);
        return result.back();
    };

    if (defs.empty()) {
        // Umbriel provides one implicit scratchpad named `default`.
        Scratchpad& sp = ensure("default");
        sp.implicit = true;
    } else {
        for (const auto& def : defs) {
            if (def.name.empty()) {
                continue;
            }
            Scratchpad& sp = ensure(def.name);
            if (def.spawn_when_empty) {
                sp.spawn_when_empty = def.spawn_when_empty;
            }
        }
    }

    // Attach stored windows; create entries for names that are not configured so
    // a stale scratchpad is still visible.
    for (const auto& w : windows) {
        if (w.scratchpad.empty()) {
            continue;
        }
        Scratchpad& sp = ensure(w.scratchpad);
        sp.windows.push_back(w);
    }

    // Order windows within a scratchpad: visible first, then focused, then title.
    for (auto& sp : result) {
        std::stable_sort(sp.windows.begin(), sp.windows.end(),
                         [](const WindowInfo& a, const WindowInfo& b) {
                             if (a.visible != b.visible) {
                                 return a.visible;
                             }
                             if (a.focused != b.focused) {
                                 return a.focused;
                             }
                             return a.title < b.title;
                         });
    }

    return result;
}

}  // namespace spm
