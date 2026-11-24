#include "driver_link.h"

#include <algorithm>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace
{
uint64_t MonoNs()
{
	timespec ts;
	clock_gettime( CLOCK_MONOTONIC, &ts );
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
} // namespace

DriverLink::~DriverLink()
{
	if ( m_shm )
		Write( {}, false );
	Detach();
}

void DriverLink::Attach()
{
	// Never created here: the shared memory belongs to the driver, so its absence means "no driver".
	const int fd = shm_open( kOffsetShmName, O_RDWR | O_CLOEXEC, 0 );
	if ( fd < 0 )
		return;
	struct stat st;
	if ( fstat( fd, &st ) == 0 && (size_t)st.st_size >= kOffsetShmV1Size )
	{
		const size_t size = std::min( (size_t)st.st_size, sizeof( OffsetShm ) );
		void *p = mmap( nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0 );
		if ( p != MAP_FAILED )
		{
			m_shm = (OffsetShm *)p;
			m_size = size;
			m_v2 = size >= sizeof( OffsetShm ) && m_shm->driverVersion >= 2;
			m_lastSeen = m_shm->posesSeen.load( std::memory_order_relaxed );
		}
	}
	close( fd );
}

void DriverLink::Detach()
{
	if ( m_shm )
		munmap( m_shm, m_size );
	m_shm = nullptr;
	m_size = 0;
	m_v2 = false;
	m_alive = false;
}

int DriverLink::Version() const
{
	return !m_shm ? 0 : m_v2 ? (int)m_shm->driverVersion : 1;
}

void DriverLink::Poll( double now )
{
	if ( !m_shm && now >= m_nextAttach )
	{
		m_nextAttach = now + 2.0;
		Attach();
		m_attachedAt = m_lastBeatSeen = now;
	}
	if ( !m_shm )
		return;
	if ( m_v2 )
	{
		// Refreshed every server frame by a driver whose hook is in.
		const uint64_t beat = m_shm->driverBeat.load( std::memory_order_acquire );
		m_alive = beat && MonoNs() - beat < 1'000'000'000ull;
		if ( m_alive )
			m_lastBeatSeen = now;
		else if ( now - m_lastBeatSeen > 10.0 )
		{
			Detach(); // SteamVR restarted, maybe with another driver version: attach afresh
			return;
		}
	}
	else
	{
		// A version 1 driver only counts pose updates: alive while the count moves.
		const uint64_t seen = m_shm->posesSeen.load( std::memory_order_relaxed );
		if ( seen != m_lastSeen )
		{
			m_lastSeen = seen;
			m_lastAdvance = now;
		}
		m_alive = now - m_lastAdvance < 1.0;
	}
	// A version 1 mapping is re-made every 10 s: SteamVR may have restarted with a newer driver (larger layout).
	if ( !m_v2 && now - m_attachedAt > 10.0 )
	{
		const bool alive = m_alive;
		Detach();
		Attach();
		m_attachedAt = now;
		m_alive = alive && m_shm; // keep the verdict across the re-attach
		if ( m_v2 )
			m_lastBeatSeen = now;
	}
}

void DriverLink::Write( const Offset &w, bool enabled )
{
	if ( !m_shm )
		return;
	// W(p) = R(yaw) p + t  =>  W^-1(p) = R(-yaw) p - R(-yaw) t
	const Vec3 t = RotateY( w.t, -w.yaw ) * -1.0;
	// Sequence: odd while writing, even when done. Set from the current value rather than incremented twice, so a
	// counter left odd by a killed writer cannot keep every later write looking unfinished.
	const uint32_t s = m_shm->seq.load( std::memory_order_relaxed ) | 1;
	m_shm->seq.store( s, std::memory_order_relaxed );
	std::atomic_thread_fence( std::memory_order_release );
	m_shm->magic = kOffsetShmMagic;
	m_shm->version = kDriverVersion;
	m_shm->enabled = enabled ? 1 : 0;
	m_shm->yaw = -w.yaw;
	m_shm->t[0] = t.x, m_shm->t[1] = t.y, m_shm->t[2] = t.z;
	m_shm->heartbeat = MonoNs();
	m_shm->seq.store( s + 1, std::memory_order_release );
	m_written = w;
	m_writtenEnabled = enabled;
}

bool DriverLink::TruePosition( uint32_t device, const Vec3 &reported, Vec3 &out ) const
{
	if ( m_v2 && device < 16 )
	{
		const OffsetShm::Device &d = m_shm->devices[device];
		for ( int tries = 0; tries < 4; tries++ )
		{
			const uint32_t a = d.seq.load( std::memory_order_acquire );
			if ( a & 1 )
				continue;
			const Vec3 p{ d.pos[0], d.pos[1], d.pos[2] };
			const bool valid = d.valid;
			std::atomic_thread_fence( std::memory_order_acquire );
			if ( d.seq.load( std::memory_order_relaxed ) != a )
				continue;
			if ( !valid )
				return false;
			out = p;
			return true;
		}
	}
	// Version 1: undo what we wrote (reported = W^-1(true)  =>  true = W(reported)).
	out = m_writtenEnabled ? RotateY( reported, m_written.yaw ) + m_written.t : reported;
	return true;
}
