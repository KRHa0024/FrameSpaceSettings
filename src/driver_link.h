#pragma once
// The app's side of the link to the Frame Space Settings SteamVR driver: hands the world move to the driver through
// shared memory, and tells whether the driver is loaded and working. SteamVR's playspace is never changed, so the Frame's
// own playspace corrections (it forces the floor height when it saves its map) cannot undo the move.

#include "core/math.h"
#include "driver/offset_shm.h"

#include <cstddef>
#include <cstdint>

class DriverLink
{
public:
	~DriverLink();
	// Call every tick: (re)attaches to the shared memory and follows the driver's signs of life.
	void Poll( double now );
	// The driver is loaded, hooked and seeing pose updates right now.
	bool Alive() const { return m_alive; }
	int Version() const;
	// The app's move W (how the playspace would move) is applied as W^-1 to device poses. Also the heartbeat: call it
	// at least every second while moved, or the driver stops applying the move.
	void Write( const Offset &w, bool enabled );
	// A device's raw position before the move, given the raw position SteamVR reports now (moved by what we last wrote).
	bool TruePosition( uint32_t device, const Vec3 &reported, Vec3 &out ) const;

private:
	void Attach();
	void Detach();
	OffsetShm *m_shm = nullptr;
	size_t m_size = 0;
	bool m_v2 = false;
	bool m_alive = false;
	uint64_t m_lastSeen = 0;
	double m_lastAdvance = -10, m_nextAttach = 0, m_attachedAt = 0, m_lastBeatSeen = 0;
	Offset m_written;
	bool m_writtenEnabled = false;
};
