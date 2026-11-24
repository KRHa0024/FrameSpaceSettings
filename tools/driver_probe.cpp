// Diagnostic for the SteamVR driver: feeds a test move to the driver through shared memory and measures what
// SteamVR reports for the headset. Phases: none -> world 10 cm down -> 15 degrees turn -> none.
// Usage: fss-driver-probe [seconds per phase] [seconds to wait after tracking starts]
#include "driver/offset_shm.h"

#include <openvr.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <fcntl.h>
#include <string>
#include <sys/file.h>
#include <sys/mman.h>
#include <thread>
#include <time.h>
#include <unistd.h>

static uint64_t NowNs()
{
	timespec ts;
	clock_gettime( CLOCK_MONOTONIC, &ts );
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

int main( int argc, char **argv )
{
	const double hold = argc > 1 ? atof( argv[1] ) : 4.0;
	// One writer only: the app holds this lock while it runs.
	const std::string lock = std::string( getenv( "HOME" ) ? getenv( "HOME" ) : "." ) + "/.local/state/frame-space-settings/lock";
	const int lfd = open( lock.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644 );
	if ( lfd < 0 || flock( lfd, LOCK_EX | LOCK_NB ) != 0 )
	{
		printf( "Frame Space Settings is running: stop it first (two writers would fight over the driver's move)\n" );
		return 1;
	}
	const int fd = shm_open( kOffsetShmName, O_RDWR | O_CREAT | O_CLOEXEC, 0600 );
	if ( fd < 0 || ftruncate( fd, sizeof( OffsetShm ) ) != 0 )
		return 1;
	auto *s = (OffsetShm *)mmap( nullptr, sizeof( OffsetShm ), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0 );
	s->magic = kOffsetShmMagic;
	s->version = 1;
	auto set = [&]( bool on, double yaw, double ty ) {
		const uint32_t q = s->seq.load( std::memory_order_relaxed ) | 1;
		s->seq.store( q, std::memory_order_relaxed );
		std::atomic_thread_fence( std::memory_order_release );
		s->enabled = on;
		s->yaw = yaw;
		s->t[0] = 0, s->t[1] = ty, s->t[2] = 0;
		s->heartbeat = NowNs();
		s->seq.store( q + 1, std::memory_order_release );
	};
	vr::EVRInitError e;
	vr::VR_Init( &e, vr::VRApplication_Background );
	if ( e )
	{
		printf( "SteamVR not reachable (%d)\n", e );
		return 1;
	}
	// Wait for the headset to be tracked (after a SteamVR restart the headset may still be waking up).
	for ( int i = 0; i < 180; i++ )
	{
		vr::TrackedDevicePose_t p;
		vr::VRSystem()->GetDeviceToAbsoluteTrackingPose( vr::TrackingUniverseStanding, 0, &p, 1 );
		if ( p.bPoseIsValid && p.eTrackingResult == vr::TrackingResult_Running_OK )
			break;
		std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
	}
	if ( argc > 2 )
		std::this_thread::sleep_for( std::chrono::duration<double>( atof( argv[2] ) ) ); // time to look around first
	auto head = [&]( const char *label ) {
		vr::TrackedDevicePose_t p;
		vr::VRSystem()->GetDeviceToAbsoluteTrackingPose( vr::TrackingUniverseStanding, 0, &p, 1 );
		const auto &m = p.mDeviceToAbsoluteTracking.m;
		printf( "%-24s head (%.3f, %.3f, %.3f) heading %+.1f deg; driver saw %llu poses, moved %llu, devices 0x%x\n", label, m[0][3], m[1][3],
			m[2][3], std::atan2( -m[0][2], m[2][2] ) * 57.29578, (unsigned long long)s->posesSeen.load(), (unsigned long long)s->posesMoved.load(),
			s->devicesSeenMask.load() );
	};
	auto run = [&]( bool on, double yaw, double ty, const char *label ) {
		const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>( hold );
		set( on, yaw, ty );
		std::this_thread::sleep_for( std::chrono::milliseconds( 300 ) );
		head( label );
		while ( std::chrono::steady_clock::now() < end )
		{
			set( on, yaw, ty ); // heartbeat
			std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
		}
	};
	run( false, 0, 0, "no move" );
	run( true, 0, -0.10, "world 10 cm down" );
	run( true, 15 * M_PI / 180, 0, "world turned 15 deg" );
	run( false, 0, 0, "no move again" );
	set( false, 0, 0 );
	vr::VR_Shutdown();
	return 0;
}
