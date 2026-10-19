// SPDX-License-Identifier: MIT
// Turns an Umbriel palette into the stylesheet used by the overlay.
#pragma once

#include <string>

#include "model/types.hpp"

namespace paddeck {

std::string build_css(const Theme& theme);

}  // namespace paddeck
