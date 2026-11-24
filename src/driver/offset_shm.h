#pragma once
// Shared between Frame Space Settings (writer) and its SteamVR driver (reader, inside vrserver): the world move to apply to
// every tracked device pose. A seqlock: the writer bumps `seq` to odd, writes, bumps to even; the reader retries while
// `seq` is odd or changed. `heartbeat` is the writer's CLOCK_MONOTONIC time in ns; a stale one (the app is gone) means
// "no move", so the world can never stay moved without the app.

#include <atomic>
#include <cstddef>
#include <cstdint>

constexpr const char *kOffsetShmName = "/frame-space-settings-offset";
constexpr uint32_t kOffsetShmMagic = 0x46535331; // "FSS1"
constexpr uint64_t kOffsetStaleNs = 2'000'000'000ull;

struct OffsetShm
{
	uint32_t magic;
	uint32_t version;
	std::atomic<uint32_t> seq;
	uint32_t enabled;
	double yaw;          // radians about +Y
	double t[3];         // meters, applied after the rotation (world = R(yaw) * p + t)
	uint64_t heartbeat;  // CLOCK_MONOTONIC ns
	// written by the driver, for diagnostics
	std::atomic<uint64_t> posesSeen;
	std::atomic<uint64_t> posesMoved;
	std::atomic<uint32_t> devicesSeenMask; // bit per device index < 32

	// Version 2 (appended: a reader must check the mapping size and driverVersion before touching these).
	uint32_t driverVersion;
	std::atomic<uint64_t> driverBeat; // CLOCK_MONOTONIC ns, refreshed by the driver every server frame
	// Device positions in raw space *before* the move: what gestures must use, or moving the world would move the hand
	// that is grabbing it. Seqlock per device, as above.
	struct Device
	{
		std::atomic<uint32_t> seq;
		uint32_t valid;
		double pos[3];
	} devices[16];
};

constexpr size_t kOffsetShmV1Size = offsetof( OffsetShm, driverVersion );
constexpr uint32_t kDriverVersion = 2;
