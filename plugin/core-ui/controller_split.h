// The pad's Ctrl for the stack split (#1442): Controller QoL's item modifier (LB by default) held with A.
#pragma once

namespace controller_split {

// True when Controller QoL's modifier is held on XInput pad 0. Reads that plugin's config on each call (a stack has just
// left the cursor, so it is rare), so an edit applies at once; no config or key means LB.
auto ModifierHeld() noexcept -> bool;

// True when RB is down as well as the modifier (#1472's whole stack to the Cube). With modifier = "rb" it is never true.
auto ModifierAndRbHeld() noexcept -> bool;

// True when RB is down and the modifier is not (#1515's 5 from a stack tab). With modifier = "rb" it is never true.
auto RbAloneHeld() noexcept -> bool;

}  // namespace controller_split
