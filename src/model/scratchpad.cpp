// SPDX-License-Identifier: MIT
#include "model/scratchpad.hpp"

#include <algorithm>
#include <iterator>
#include <unordered_map>

namespace paddeck {

int Scratchpad::visible_count() const {
    return static_cast<int>(std::count_if(
        windows.begin(), windows.end(), [](const WindowInfo& w) { return w.visible; }));
}

Scratchpad::Presentation Scratchpad::presentation() const {
    const int visible = visible_count();
    if (visible == 0) {
        return Presentation::hidden;
    }
    // Umbriel shows a pad either as a whole or one member at a time. A single
    // visible member of a larger pad therefore means it is stepping.
    if (visible == 1 && windows.size() > 1) {
        return Presentation::single;
    }
    return Presentation::all;
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
    //
    // No ordering happens here: the incoming order is only a starting point.
    // Umbriel lists windows most-recently-focused first, so the display order is
    // kept by MemberOrder across refreshes instead.
    for (const auto& w : windows) {
        if (w.scratchpad.empty()) {
            continue;
        }
        Scratchpad& sp = ensure(w.scratchpad);
        sp.windows.push_back(w);
    }

    return result;
}

void MemberOrder::apply(std::vector<Scratchpad>& scratchpads) {
    for (auto& sp : scratchpads) {
        std::vector<std::string>& remembered = m_order[sp.name];
        const auto is_member = [&sp](const std::string& id) {
            return std::any_of(sp.windows.begin(), sp.windows.end(),
                               [&id](const WindowInfo& w) { return w.id == id; });
        };

        // Keep the remembered slots, dropping members that have left the pad.
        std::vector<std::string> order;
        order.reserve(sp.windows.size());
        for (const std::string& id : remembered) {
            if (is_member(id)) {
                order.push_back(id);
            }
        }
        // Append members this pad has not held before, in the order they arrive.
        for (const auto& w : sp.windows) {
            if (std::find(order.begin(), order.end(), w.id) == order.end()) {
                order.push_back(w.id);
            }
        }

        std::map<std::string, std::size_t> rank;
        for (std::size_t i = 0; i < order.size(); ++i) {
            rank.emplace(order[i], i);
        }
        std::stable_sort(sp.windows.begin(), sp.windows.end(),
                         [&rank](const WindowInfo& a, const WindowInfo& b) {
                             return rank.at(a.id) < rank.at(b.id);
                         });
        remembered = std::move(order);
    }

    // Forget scratchpads that no longer exist, so the map cannot grow forever.
    for (auto it = m_order.begin(); it != m_order.end();) {
        const bool known = std::any_of(scratchpads.begin(), scratchpads.end(),
                                       [&name = it->first](const Scratchpad& sp) {
                                           return sp.name == name;
                                       });
        it = known ? std::next(it) : m_order.erase(it);
    }
}

}  // namespace paddeck
