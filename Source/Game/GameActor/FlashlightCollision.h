#pragma once

#include "Collision/CollisionShape.h"

// Reuse existing slot 5 for this game's melee query. Editor labels are display-only.
inline constexpr ECollisionChannel FlashlightAttackChannel = ECollisionChannel::Camera;
