// The Frame Space Settings SteamVR driver. It adds no devices: it hooks IVRServerDriverHost::TrackedDevicePoseUpdated,
// which every driver (the Frame's "cv" included) calls with each new device pose, and puts the world move from
// Frame Space Settings into the pose's world-from-driver transform. All devices move together, so to games it is the same as
// moving the playspace, and the Frame's own playspace corrections (which act on the zero pose) do not touch it.
//
// Safety: does nothing when ~/.config/frame-space-settings/driver-disabled exists; does not hook again if the previous start
// died while hooking (marker file); applies nothing when the app's heartbeat is older than 2 s.

#include "offset_shm.h"

#include <openvr_driver.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dlfcn.h>
#include <fcntl.h>
#include <pwd.h>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace vr;

namespace
{
using PoseFn = void ( * )( IVRServerDriverHost *, uint32_t, const DriverPose_t &, uint32_t );
std::atomic<PoseFn> g_original{ nullptr }; // written before the slot is patched, read by every hooked call
void **g_slot = nullptr;
OffsetShm *g_shm = nullptr;
std::atomic<uint64_t> g_calls{ 0 };

std::string Home()
{
	if ( const char *h = getenv( "HOME" ); h && *h )
		return h;
	const passwd *pw = getpwuid( getuid() ); // vrserver normally has HOME; this is only a fallback
	return pw && pw->pw_dir ? pw->pw_dir : "/tmp";
}
std::string Marker() { return Home() + "/.local/state/frame-space-settings/driver-hooking"; }
bool Exists( const std::string &p ) { return access( p.c_str(), F_OK ) == 0; }

void Log( const char *fmt, ... ) __attribute__( ( format( printf, 1, 2 ) ) );
void Log( const char *fmt, ... )
{
	char buf[512] = "Frame Space Settings driver: ";
	const size_t n = strlen( buf );
	va_list ap;
	va_start( ap, fmt );
	vsnprintf( buf + n, sizeof buf - n - 2, fmt, ap );
	va_end( ap );
	strcat( buf, "\n" );
	if ( VRDriverLog() )
		VRDriverLog()->Log( buf );
}

uint64_t NowNs()
{
	timespec ts;
	clock_gettime( CLOCK_MONOTONIC, &ts );
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

// Reads the move; false when there is none to apply.
bool ReadOffset( double &yaw, double t[3] )
{
	if ( !g_shm || g_shm->magic != kOffsetShmMagic )
		return false;
	for ( int tries = 0; tries < 4; tries++ )
	{
		const uint32_t a = g_shm->seq.load( std::memory_order_acquire );
		if ( a & 1 )
			continue;
		const uint32_t enabled = g_shm->enabled;
		yaw = g_shm->yaw;
		t[0] = g_shm->t[0], t[1] = g_shm->t[1], t[2] = g_shm->t[2];
		const uint64_t hb = g_shm->heartbeat;
		std::atomic_thread_fence( std::memory_order_acquire );
		if ( g_shm->seq.load( std::memory_order_relaxed ) != a )
			continue;
		if ( !enabled || NowNs() - hb > kOffsetStaleNs || !std::isfinite( yaw ) || !std::isfinite( t[0] ) || !std::isfinite( t[1] ) ||
			 !std::isfinite( t[2] ) )
			return false;
		return yaw != 0 || t[0] != 0 || t[1] != 0 || t[2] != 0;
	}
	return false;
}

HmdQuaternion_t Mul( const HmdQuaternion_t &a, const HmdQuaternion_t &b )
{
	return { a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z, a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
		a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w };
}

void Hooked( IVRServerDriverHost *self, uint32_t device, const DriverPose_t &pose, uint32_t size )
{
	g_calls.fetch_add( 1, std::memory_order_relaxed );
	if ( g_shm )
	{
		g_shm->posesSeen.fetch_add( 1, std::memory_order_relaxed );
		if ( device < 32 )
			g_shm->devicesSeenMask.fetch_or( 1u << device, std::memory_order_relaxed );
	}
	if ( g_shm && device < 16 && size >= sizeof( DriverPose_t ) )
	{
		// The device's raw position as its driver reported it, before any move.
		const HmdQuaternion_t &qp = pose.qRotation, &qw0 = pose.qWorldFromDriverRotation;
		auto rotate = []( const HmdQuaternion_t &q, const double v[3], double out[3] ) {
			// v' = v + 2w(u x v) + 2 u x (u x v), u = (x, y, z)
			const double ux = q.x, uy = q.y, uz = q.z;
			const double cx = uy * v[2] - uz * v[1], cy = uz * v[0] - ux * v[2], cz = ux * v[1] - uy * v[0];
			out[0] = v[0] + 2 * ( q.w * cx + uy * cz - uz * cy );
			out[1] = v[1] + 2 * ( q.w * cy + uz * cx - ux * cz );
			out[2] = v[2] + 2 * ( q.w * cz + ux * cy - uy * cx );
		};
		double head[3], inDriver[3], raw[3];
		rotate( qp, pose.vecDriverFromHeadTranslation, head );
		for ( int k = 0; k < 3; k++ )
			inDriver[k] = pose.vecPosition[k] + head[k];
		rotate( qw0, inDriver, raw );
		OffsetShm::Device &d = g_shm->devices[device];
		const uint32_t s = d.seq.load( std::memory_order_relaxed ) | 1;
		d.seq.store( s, std::memory_order_relaxed ); // odd: writing
		std::atomic_thread_fence( std::memory_order_release );
		d.valid = pose.poseIsValid && pose.deviceIsConnected && pose.result == TrackingResult_Running_OK;
		for ( int k = 0; k < 3; k++ )
			d.pos[k] = raw[k] + pose.vecWorldFromDriverTranslation[k];
		d.seq.store( s + 1, std::memory_order_release ); // even: done
	}
	double yaw, t[3];
	if ( size < sizeof( DriverPose_t ) || !ReadOffset( yaw, t ) )
	{
		g_original.load( std::memory_order_acquire )( self, device, pose, size );
		return;
	}
	DriverPose_t p = pose;
	// world' = W * world, with W = rotation about +Y by `yaw`, then translation t.
	const HmdQuaternion_t qw = { std::cos( yaw / 2 ), 0, std::sin( yaw / 2 ), 0 };
	const double c = std::cos( yaw ), s = std::sin( yaw );
	const double *v = p.vecWorldFromDriverTranslation;
	const double x = c * v[0] + s * v[2], z = -s * v[0] + c * v[2], y = v[1];
	p.vecWorldFromDriverTranslation[0] = x + t[0];
	p.vecWorldFromDriverTranslation[1] = y + t[1];
	p.vecWorldFromDriverTranslation[2] = z + t[2];
	p.qWorldFromDriverRotation = Mul( qw, p.qWorldFromDriverRotation );
	if ( g_shm )
		g_shm->posesMoved.fetch_add( 1, std::memory_order_relaxed );
	g_original.load( std::memory_order_acquire )( self, device, p, size );
}

// The protection of the mapping that holds `addr`, from /proc/self/maps (PROT_* bits); -1 if not found.
int ProtectionOf( const void *addr )
{
	FILE *f = fopen( "/proc/self/maps", "r" );
	if ( !f )
		return -1;
	char line[512];
	int prot = -1;
	while ( fgets( line, sizeof line, f ) )
	{
		unsigned long lo, hi;
		char perms[5] = {};
		if ( sscanf( line, "%lx-%lx %4s", &lo, &hi, perms ) == 3 && (uintptr_t)addr >= lo && (uintptr_t)addr < hi )
		{
			prot = ( perms[0] == 'r' ? PROT_READ : 0 ) | ( perms[1] == 'w' ? PROT_WRITE : 0 ) | ( perms[2] == 'x' ? PROT_EXEC : 0 );
			break;
		}
	}
	fclose( f );
	return prot;
}

// Swaps one vtable entry. Only the page holding it is touched (an aligned pointer never spans two pages), and that page
// gets back exactly the protection it had: forcing a neighbour read-only would crash vrserver later on any write there.
bool Patch( void **slot, void *value )
{
	const long page = sysconf( _SC_PAGESIZE );
	void *start = (void *)( (uintptr_t)slot & ~(uintptr_t)( page - 1 ) );
	const int prot = ProtectionOf( slot );
	if ( prot < 0 )
		return false;
	if ( !( prot & PROT_WRITE ) && mprotect( start, (size_t)page, prot | PROT_WRITE ) != 0 )
		return false;
	__atomic_store_n( slot, value, __ATOMIC_SEQ_CST );
	if ( !( prot & PROT_WRITE ) )
		mprotect( start, (size_t)page, prot );
	return true;
}

// Keeps this .so mapped until vrserver exits: a pose call may be inside Hooked while the driver is cleaned up, and
// another program's hook may have chained to us.
void PinSelf()
{
	Dl_info info;
	if ( dladdr( (void *)&PinSelf, &info ) && info.dli_fname )
		dlopen( info.dli_fname, RTLD_NOW | RTLD_NODELETE | RTLD_NOLOAD );
}

void OpenShm()
{
	const int fd = shm_open( kOffsetShmName, O_RDWR | O_CREAT | O_CLOEXEC, 0600 );
	if ( fd < 0 )
		return;
	if ( ftruncate( fd, sizeof( OffsetShm ) ) == 0 )
	{
		void *p = mmap( nullptr, sizeof( OffsetShm ), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0 );
		if ( p != MAP_FAILED )
			g_shm = (OffsetShm *)p;
	}
	close( fd );
}

class Provider : public IServerTrackedDeviceProvider
{
public:
	EVRInitError Init( IVRDriverContext *ctx ) override
	{
		VR_INIT_SERVER_DRIVER_CONTEXT( ctx );
		if ( Exists( Home() + "/.config/frame-space-settings/driver-disabled" ) )
		{
			Log( "disabled by ~/.config/frame-space-settings/driver-disabled" );
			return VRInitError_None;
		}
		if ( Exists( Marker() ) )
		{
			Log( "the previous start did not finish hooking (crash?); not hooking. Delete %s to try again.", Marker().c_str() );
			return VRInitError_None;
		}
		mkdir( ( Home() + "/.local/state" ).c_str(), 0755 );
		mkdir( ( Home() + "/.local/state/frame-space-settings" ).c_str(), 0755 );
		if ( FILE *f = fopen( Marker().c_str(), "w" ) )
			fclose( f );
		OpenShm();
		if ( g_shm )
		{
			// A writer killed between its two sequence bumps would leave a counter odd for good: start clean.
			g_shm->seq.store( 0, std::memory_order_relaxed );
			for ( OffsetShm::Device &d : g_shm->devices )
				d.seq.store( 0, std::memory_order_relaxed );
			g_shm->enabled = 0;
			g_shm->magic = kOffsetShmMagic;
			g_shm->driverVersion = kDriverVersion;
			g_shm->driverBeat.store( NowNs(), std::memory_order_release );
		}
		IVRServerDriverHost *host = VRServerDriverHost();
		void **vtable = host ? *(void ***)host : nullptr;
		if ( !vtable || !g_shm )
		{
			Log( "no driver host or shared memory; not hooking" );
			unlink( Marker().c_str() );
			return VRInitError_None;
		}
		g_slot = &vtable[1]; // IVRServerDriverHost_006: [0] TrackedDeviceAdded, [1] TrackedDevicePoseUpdated
		g_original.store( (PoseFn)*g_slot, std::memory_order_release );
		PinSelf();
		if ( !Patch( g_slot, (void *)&Hooked ) )
		{
			Log( "cannot patch the driver host; not hooking" );
			g_slot = nullptr;
			unlink( Marker().c_str() );
			return VRInitError_None;
		}
		Log( "hooked TrackedDevicePoseUpdated (host %p, vtable %p)", (void *)host, (void *)vtable );
		return VRInitError_None;
	}
	void Cleanup() override
	{
		// Put the original back only if the slot is still ours: if someone hooked it after us, restoring would cut them
		// out, and their own restore would later point at us. Either way this .so stays loaded (PinSelf).
		if ( g_slot && g_original.load() && *g_slot == (void *)&Hooked )
			Patch( g_slot, (void *)g_original.load() );
		g_slot = nullptr;
		Log( "unhooked after %llu pose updates", (unsigned long long)g_calls.load() );
		unlink( Marker().c_str() );
		VR_CLEANUP_SERVER_DRIVER_CONTEXT();
	}
	const char *const *GetInterfaceVersions() override { return k_InterfaceVersions; }
	void RunFrame() override
	{
		if ( g_shm && g_slot )
			g_shm->driverBeat.store( NowNs(), std::memory_order_release ); // "the hook is in and vrserver is running"
		// The hook has been working for a while: the start went fine, drop the crash marker.
		if ( g_slot && !m_markerCleared && g_calls.load( std::memory_order_relaxed ) > 500 )
		{
			unlink( Marker().c_str() );
			m_markerCleared = true;
			Log( "pose updates are flowing through the hook (%llu so far)", (unsigned long long)g_calls.load() );
		}
	}
	bool ShouldBlockStandbyMode() override { return false; }
	void EnterStandby() override {}
	void LeaveStandby() override {}

private:
	bool m_markerCleared = false;
};

Provider g_provider;
} // namespace

extern "C" __attribute__( ( visibility( "default" ) ) ) void *HmdDriverFactory( const char *name, int *ret )
{
	if ( !strcmp( name, IServerTrackedDeviceProvider_Version ) )
		return &g_provider;
	if ( ret )
		*ret = VRInitError_Init_InterfaceNotFound;
	return nullptr;
}
