// SPDX-License-Identifier: GPL-3.0-only
// At the pinned revision, the production context uses this provider even when
// devtools are disabled, but its header is guarded by the devtools switch.
// Keep this compatibility fix outside the unmodified upstream checkout.
// clang-format off
#include "engraving/devtools/engravingelementsprovider.h"
#include "engraving/engravingmodule.cpp"
// clang-format on
