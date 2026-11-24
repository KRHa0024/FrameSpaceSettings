#pragma once
// SteamVR's saved playspace, read to notice a recenter or a room setup (the move is then dropped). Nothing is written:
// the world is moved by the Frame Space Settings SteamVR driver, on the device poses.

#include "core/math.h"

#include <cstdint>

constexpr uint64_t kDummyUniverse = 424242; // what SteamVR runs while the headset is not tracking

class ChaperoneStore
{
public:
	// The saved standing / seated zero poses (raw space).
	static bool ReadSaved( Mat34 &standing, Mat34 &seated );
	static uint64_t CurrentUniverse(); // 0 when unknown
};
