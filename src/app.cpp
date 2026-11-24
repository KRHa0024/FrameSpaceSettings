#include "app.h"

#include "core/files.h"
#include "core/log.h"
#include "core/version.h"

#include <openvr.h>

#include <dirent.h>
#include <malloc.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <thread>

using namespace vr;

std::atomic<bool> App::s_quit{ false };
std::atomic<bool> App::s_restart{ false };

namespace
{
constexpr double kDegToRad = M_PI / 180.0;
constexpr double kResyncDebounce = 0.3;
constexpr double kRoomSetupTimeout = 90.0;

double Now()
{
	return std::chrono::duration<double>( std::chrono::steady_clock::now().time_since_epoch() ).count();
}

std::string FindData()
{
	char exe[4096];
	const ssize_t n = readlink( "/proc/self/exe", exe, sizeof exe - 1 );
	if ( n > 0 )
	{
		std::string dir( exe, (size_t)n );
		dir = dir.substr( 0, dir.rfind( '/' ) );
		for ( const std::string &c : { dir + "/data", dir + "/../data" } )
			if ( files::Exists( c + "/action_manifest.json" ) )
				return c;
	}
	return files::DataDir();
}

const char *EventName( uint32_t type )
{
	return VRSystem()->GetEventTypeNameFromEnum( (EVREventType)type );
}

struct Tracked
{
	bool valid = false;
	Vec3 pos;
};

Tracked ReadPose( TrackedDeviceIndex_t idx, const TrackedDevicePose_t *poses )
{
	Tracked t;
	if ( idx >= k_unMaxTrackedDeviceCount )
		return t;
	const TrackedDevicePose_t &p = poses[idx];
	t.valid = p.bPoseIsValid && p.bDeviceIsConnected && p.eTrackingResult == TrackingResult_Running_OK;
	t.pos = { p.mDeviceToAbsoluteTracking.m[0][3], p.mDeviceToAbsoluteTracking.m[1][3], p.mDeviceToAbsoluteTracking.m[2][3] };
	return t;
}
// Start time of the running vrserver (seconds since the epoch), or -1. The process is looked up again only when the
// remembered one is gone.
double VrserverStart()
{
	static int pid = 0;
	static double started = -1;
	auto read = []( int p, double &out ) {
		std::string comm, stat;
		const std::string dir = "/proc/" + std::to_string( p );
		if ( !files::ReadFile( dir + "/comm", comm ) || comm.rfind( "vrserver", 0 ) != 0 || !files::ReadFile( dir + "/stat", stat ) )
			return false;
		std::string up;
		files::ReadFile( "/proc/stat", up );
		const size_t k = up.find( "btime " );
		const size_t close = stat.rfind( ')' ); // field 2 (comm) may hold spaces; count the fields after it
		if ( k == std::string::npos || close == std::string::npos )
			return false;
		const char *f = stat.c_str() + close + 2; // field 3
		for ( int field = 3; field < 22 && *f; field++ )
			f = strchr( f, ' ' ) ? strchr( f, ' ' ) + 1 : "";
		out = atol( up.c_str() + k + 6 ) + atof( f ) / sysconf( _SC_CLK_TCK ); // field 22: start, in ticks since boot
		return true;
	};
	if ( pid > 0 && read( pid, started ) )
		return started;
	pid = 0;
	started = -1;
	if ( DIR *d = opendir( "/proc" ) )
	{
		while ( dirent *e = readdir( d ) )
			if ( e->d_name[0] >= '1' && e->d_name[0] <= '9' && read( atoi( e->d_name ), started ) )
			{
				pid = atoi( e->d_name );
				break;
			}
		closedir( d );
	}
	return started;
}

// True when the installed driver file was written after the running vrserver started: SteamVR still has the old one
// (or none) loaded and loads the new one on its next start.
bool DriverNewerThanSteamVR()
{
	struct stat st;
	if ( stat( ( files::DataDir() + "/driver/framespacesettings/bin/linuxarm64/driver_framespacesettings.so" ).c_str(), &st ) != 0 )
		return false;
	const double started = VrserverStart();
	return started > 0 && st.st_mtime > started + 1;
}
} // namespace

App::~App()
{
	if ( !VRSystem() )
		return;
	m_driver.Write( {}, false ); // the world goes back at once (the driver would also drop it 2 s after our last heartbeat)
	if ( m_overlayOk )
	{
		// Take the panel down while still connected: vanishing with an upload in flight can crash the compositor.
		m_overlay.Shutdown();
		std::this_thread::sleep_for( std::chrono::milliseconds( 300 ) );
	}
	VR_Shutdown();
}

// Connect in two steps: an Overlay-type VR_Init starts vrserver itself when SteamVR is not up yet, and
// on the Frame a vrserver started early hands gamescope an empty dmabuf modifier list -> gamescope crash loop (black
// screen after the boot logo). So connect as Background first (it only waits for SteamVR), and switch to Overlay (needed
// for input and the panel's dmabufs) once steamvr.service is active and the headset is connected.
bool App::ConnectSafely( std::string &error )
{
	EVRInitError err = VRInitError_None;
	for ( ;; )
	{
		VR_Init( &err, VRApplication_Background );
		if ( err == VRInitError_None )
			break;
		if ( s_quit )
		{
			error = std::string( "VR_Init failed: " ) + VR_GetVRInitErrorAsEnglishDescription( err );
			return false;
		}
		std::this_thread::sleep_for( std::chrono::seconds( 5 ) );
	}
	for ( int waited = 0; !s_quit; waited += 2 )
	{
		VREvent_t ev;
		while ( VRSystem()->PollNextEvent( &ev, sizeof ev ) )
			if ( ev.eventType == VREvent_Quit )
			{
				VRSystem()->AcknowledgeQuit_Exiting();
				s_quit = true;
			}
		const bool hmd = VRSystem()->IsTrackedDeviceConnected( k_unTrackedDeviceIndex_Hmd );
		// The real runtime dir: SteamOS Desktop Mode's nested session points XDG_RUNTIME_DIR elsewhere, where the user bus is not.
		const bool service =
			system( "XDG_RUNTIME_DIR=/run/user/$(id -u) systemctl --user is-active --quiet steamvr.service 2>/dev/null" ) == 0;
		// Without the systemd unit (SteamVR started some other way) a connected headset for a while is enough: we are
		// attached to a running vrserver either way.
		if ( hmd && ( service || waited >= 20 ) )
			break;
		std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
	}
	std::this_thread::sleep_for( std::chrono::seconds( 3 ) ); // let the session settle
	// SteamVR may have gone away during the wait; an Overlay init would then start vrserver itself. Wait while still
	// connected as Background until it is back.
	while ( !s_quit && system( "XDG_RUNTIME_DIR=/run/user/$(id -u) systemctl --user is-active --quiet steamvr.service 2>/dev/null" ) != 0 &&
			system( "pgrep -x vrserver >/dev/null" ) != 0 )
		std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
	VR_Shutdown();
	if ( s_quit )
	{
		error = "quit while waiting for SteamVR";
		return false;
	}
	VR_Init( &err, VRApplication_Overlay );
	if ( err != VRInitError_None )
	{
		error = std::string( "VR_Init(Overlay) failed: " ) + VR_GetVRInitErrorAsEnglishDescription( err );
		return false;
	}
	return true;
}

bool App::Init( std::string &error )
{
	if ( !ConnectSafely( error ) )
		return false;
	if ( !VRSystem() || !VRChaperoneSetup() )
	{
		error = "SteamVR chaperone interfaces unavailable";
		return false;
	}
	m_settings = LoadSettings();

	std::string inputError;
	m_inputOk = m_input.Init( FindData() + "/action_manifest.json", inputError );
	if ( !m_inputOk )
		logging::Printf( "SteamVR Input unavailable: %s", inputError.c_str() );

	ETrackedPropertyError pe;
	const float hz = VRSystem()->GetFloatTrackedDeviceProperty( k_unTrackedDeviceIndex_Hmd, Prop_DisplayFrequency_Float, &pe );
	if ( pe == TrackedProp_Success && hz > 30 && hz < 500 )
		m_displayHz = hz;

	m_lang = ResolveLanguage( m_settings.language );
	{
		char root[1024];
		uint32_t need = 0;
		if ( VR_GetRuntimePath( root, sizeof root, &need ) )
			m_panel.SetSteamVRRoot( root );
	}
	{
		Canvas icon( 128, 128 );
		Panel::DrawIcon( icon );
		m_overlayOk = m_overlay.Init( "dev.krha.framespacesettings.panel", "Frame Space Settings", kPanelW, kPanelH, icon.Pixels(), 128 );
		Fonts::Get().Release(); // loaded again when the panel is opened
		malloc_trim( 0 );
		if ( !m_overlayOk )
			logging::Printf( "could not create the dashboard panel; the controller buttons still work" );
	}

	m_universe = ChaperoneStore::CurrentUniverse();
	m_worldChanged = true;
	m_nextUpdateCheck = Now() + 20; // the first update check, once SteamVR has settled
	ScheduleResync( Resync::Keep, 0, "start" );
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Dashboard panel

void App::HandlePanelAct( Act a )
{
	switch ( a )
	{
	case Act::None:
		return;
	case Act::Reset:
		Reset( "panel" );
		return;
	case Act::GravityValue:
	{
		char text[16];
		snprintf( text, sizeof text, "%g", m_settings.gravityStrength );
		if ( !m_overlay.ShowKeyboard( m_lang == "ja" ? "重力の強さ（m/s²、0〜50）" : "Gravity strength (m/s², 0-50)", text, kKeyboardGravity ) )
			logging::Printf( "could not open the SteamVR keyboard" );
		return;
	}
	case Act::EditBindings:
		if ( !m_input.OpenBindingEditor( kAppKey ) )
			logging::Printf( "could not open the SteamVR binding editor" );
		return;
	case Act::Quit:
		logging::Printf( "quit from the panel" );
		s_quit = true;
		return;
	case Act::UpdateNow:
		m_updater.Apply();
		return;
	case Act::UpdateRetry:
		m_updater.Check();
		return;
	case Act::AutoStart:
	{
		const bool on = !VRApplications()->GetApplicationAutoLaunch( kAppKey );
		const EVRApplicationError e = VRApplications()->SetApplicationAutoLaunch( kAppKey, on );
		logging::Printf( "start with SteamVR: %s%s", on ? "on" : "off", e == VRApplicationError_None ? "" : " (SteamVR refused)" );
		return;
	}
	case Act::Language:
		m_lang = m_lang == "ja" ? "en" : "ja";
		m_settings.language = m_lang;
		SaveSettings( m_settings );
		return;
	default:
		if ( ApplySettingsAct( a, m_settings ) )
			SaveSettings( m_settings );
		if ( a == Act::CheckUpdates && m_settings.checkUpdates )
			m_nextUpdateCheck = 0; // turned on: check now
		return;
	}
}

void App::HandlePanelEvent( const VREvent_t &ev )
{
	switch ( ev.eventType )
	{
	case VREvent_MouseMove:
	{
		const Act hover = m_panel.HitTest( (int)ev.data.mouse.x, kPanelH - (int)ev.data.mouse.y ); // mouse origin: bottom-left
		if ( hover != m_hover )
		{
			m_hover = hover;
			m_panelDirty = true;
		}
		break;
	}
	case VREvent_MouseButtonDown:
		if ( ev.data.mouse.button & VRMouseButton_Left )
		{
			HandlePanelAct( m_panel.HitTest( (int)ev.data.mouse.x, kPanelH - (int)ev.data.mouse.y ) );
			m_panelDirty = true;
		}
		break;
	case VREvent_KeyboardDone:
	{
		if ( ev.data.keyboard.uUserValue != kKeyboardGravity )
			break;
		std::string text = m_overlay.KeyboardText();
		for ( char &ch : text )
			if ( ch == ',' )
				ch = '.'; // "1,6" too
		char *end = nullptr;
		const double v = strtod( text.c_str(), &end );
		if ( end != text.c_str() && v == v )
		{
			m_settings.gravityStrength = std::clamp( std::round( v * 100 ) / 100, 0.0, kGravityMax );
			SaveSettings( m_settings );
		}
		m_panelDirty = true;
		break;
	}
	case VREvent_OverlayClosed: // the dashboard's close button on our tab
		logging::Printf( "quit from the dashboard" );
		s_quit = true;
		break;
	case VREvent_OverlayShown:
		m_panelDirty = true;
		break;
	case VREvent_OverlayHidden:
		m_hover = Act::None;
		m_lastPanelHash = 0; // the picture went with the buffers: send it again when the tab opens
		Fonts::Get().Release();
		malloc_trim( 0 );
		break;
	default:
		break;
	}
}

void App::RedrawPanel( double now )
{
	m_panelDirty = false;
	m_nextPanelDraw = now + 0.1; // at most 10 times a second (values change continuously while grabbing)
	PanelModel model;
	model.off = m_off;
	model.settings = m_settings;
	model.inputOk = m_inputOk;
	model.waiting = m_state == State::Waiting;
	model.driverMode = m_driverMode;
	model.driverBlocked = !m_driver.Alive() && files::Exists( files::StateDir() + "/driver-hooking" );
	model.bindingsKnown = m_input.Bindings( model.bindings );
	model.autoStart = VRApplications()->GetApplicationAutoLaunch( kAppKey );
	model.version = FSS_VERSION;
	model.driverRestart = m_driverRestart;
	model.update = (int)m_updater.GetState();
	model.updateTag = m_updater.LatestTag();
	{
		std::string summary;
		for ( const BindingInfo &b : model.bindings )
			summary += " " + std::to_string( b.action ) + ":" + std::to_string( b.hand ) + b.input + "/" + b.slot;
		if ( summary != m_lastBindings )
		{
			logging::Printf( "bindings (action:hand input/slot):%s", summary.empty() ? " none" : summary.c_str() );
			m_lastBindings = summary;
		}
	}
	Canvas canvas( kPanelW, kPanelH ); // only while drawing: ~7 MB is not kept around
	m_panel.Draw( canvas, model, m_hover, m_lang );
	uint64_t h = 1469598103934665603ull; // FNV-1a: sending an identical picture would still make SteamVR reload it
	for ( uint8_t b : canvas.Pixels() )
		h = ( h ^ b ) * 1099511628211ull;
	if ( h == m_lastPanelHash )
		return;
	m_lastPanelHash = h;
	m_overlay.Show( canvas.Pixels() );
}

// ---------------------------------------------------------------------------------------------------------------------
// Playspace

void App::ScheduleResync( Resync mode, double at, const char *why )
{
	// Bursts of events push a near resync back (debounce); a far one (room setup timeout) is pulled in by a nearer one.
	if ( m_resyncAt < 0 || at < m_resyncAt || m_resyncAt - Now() < 1.0 )
		m_resyncAt = at;
	if ( m_resyncWhy.empty() || ( mode == Resync::NewOriginal && m_resyncMode != Resync::NewOriginal ) )
		m_resyncWhy = why;
	if ( mode == Resync::NewOriginal )
		m_resyncMode = Resync::NewOriginal;
}

void App::DoResync( double now )
{
	const Resync mode = m_resyncMode;
	const std::string why = m_resyncWhy;
	m_resyncAt = -1;
	m_resyncMode = Resync::Keep;
	m_resyncWhy.clear();

	if ( const uint64_t u = ChaperoneStore::CurrentUniverse(); u != 0 )
		m_universe = u;
	if ( !Real( m_universe ) )
	{
		m_state = State::Waiting;
		m_resyncMode = mode; // a recenter seen during a tracking dropout still counts once tracking is back
		m_resyncWhy = why;
		return;
	}
	Mat34 st, se;
	if ( !ChaperoneStore::ReadSaved( st, se ) )
	{
		m_state = State::Waiting;
		m_nextRetry = now + 2;
		return;
	}
	const bool moved = !m_off.IsZero();
	if ( moved && !m_worldChanged && m_haveBase && OnlyHeightDiffers( st, m_baseStanding ) )
	{
		// The Frame's tracking forcing its own floor height onto the saved playspace (why the height snaps back in movers
		// that edit the zero pose). Not a recenter: keep the playspace the move was made on, so the user's height does not jump.
		logging::Printf( "%s: SteamVR moved the saved floor by %+.3f m; keeping the move and the floor it was made on", why.c_str(),
			st.m[1][3] - m_baseStanding.m[1][3] );
		m_state = State::Ready;
		m_dirty = true;
		return;
	}
	if ( m_worldChanged && moved )
	{
		// After losing tracking, raw space is a new one: a move made in the old one means nothing in the new one.
		logging::Printf( "%s: new tracking world, move dropped", why.c_str() );
		m_off = {};
		ResetGestures();
	}
	else if ( mode == Resync::NewOriginal && moved && m_haveBase && !NearPose( st, m_baseStanding, 0.005, 0.2 * kDegToRad ) )
	{
		const Vec3 d = st.T() - m_baseStanding.T();
		logging::Printf( "%s: the saved playspace changed by %+.3f, %+.3f, %+.3f m, yaw %+.2f deg (recenter or room setup), move dropped",
			why.c_str(), d.x, d.y, d.z, WrapAngle( YawOf( st ) - YawOf( m_baseStanding ) ) / kDegToRad );
		m_off = {};
		ResetGestures();
	}
	else if ( moved )
		logging::Printf( "%s: move kept on the saved playspace", why.c_str() );
	m_worldChanged = false;
	m_baseStanding = st;
	m_haveBase = true;
	m_state = State::Ready;
	// A dropped move must reach the driver now (otherwise it would keep it until our heartbeat runs out).
	m_dirty = true;
}

// Follows whether the Frame Space Settings SteamVR driver is loaded and working; without it nothing can be moved.
void App::FollowDriver( double now )
{
	if ( now < m_nextDriverCheck )
		return;
	m_nextDriverCheck = now + 0.5;
	static double nextFileCheck = 0;
	if ( now >= nextFileCheck )
	{
		nextFileCheck = now + 5;
		if ( const bool r = DriverNewerThanSteamVR(); r != m_driverRestart )
		{
			m_driverRestart = r;
			m_panelDirty = true;
		}
	}
	const bool alive = m_driver.Alive();
	if ( alive == m_driverMode )
		return;
	m_driverMode = alive;
	if ( alive )
		logging::Printf( "the Frame Space Settings driver (version %d) is working", m_driver.Version() );
	else
	{
		logging::Printf( "the Frame Space Settings driver is not working: the world cannot be moved" );
		Reset( "driver gone" ); // the driver drops the move on its own; start from the original when it is back
	}
	m_panelDirty = true;
}

void App::ShowMove( double now )
{
	m_dirty = false;
	if ( !m_driverMode )
		return;
	if ( !InRange( m_off.Matrix() ) )
	{
		logging::Printf( "move too large; back to the original" );
		Reset( "out of range" );
		return;
	}
	m_driver.Write( m_off, !m_off.IsZero() );
	m_lastWrite = now;
}

void App::Reset( const char *why )
{
	if ( !m_off.IsZero() )
		logging::Printf( "%s: back to the original playspace", why );
	m_off = {};
	m_gravity.Stop();
	m_dirty = false;
	ResetGestures();
	m_driver.Write( {}, false );
	m_panelDirty = true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Input

bool App::GestureActive() const
{
	return m_drag[0].Active() || m_drag[1].Active() || m_turn[0].Active() || m_turn[1].Active();
}

void App::ResetGestures()
{
	for ( int h = 0; h < 2; h++ )
	{
		m_drag[h] = DragGesture();
		m_turn[h] = TurnGesture();
	}
}

void App::Pulse( bool both )
{
	if ( !m_settings.haptics || !m_inputOk )
		return;
	m_input.Pulse( 1, 0.05f, 0.6f );
	if ( both )
		m_input.Pulse( 0, 0.05f, 0.6f );
}

void App::HandleInput( const InputFrame &in, double now, double dt )
{
	const bool resetPressed = in.reset && !m_resetWasDown; // once per press; long press etc. is up to the binding
	m_resetWasDown = in.reset;
	if ( resetPressed && m_settings.resetEnabled )
	{
		Reset( "reset button" );
		Pulse( true );
		return;
	}

	Tracked hmd, hands[2];
	if ( in.drag[0] || in.drag[1] || in.turn[0] || in.turn[1] )
	{
		TrackedDevicePose_t poses[k_unMaxTrackedDeviceCount];
		VRSystem()->GetDeviceToAbsoluteTrackingPose( TrackingUniverseRawAndUncalibrated, 0.0f, poses, k_unMaxTrackedDeviceCount );
		hmd = ReadPose( k_unTrackedDeviceIndex_Hmd, poses );
		hands[0] = ReadPose( VRSystem()->GetTrackedDeviceIndexForControllerRole( TrackedControllerRole_LeftHand ), poses );
		hands[1] = ReadPose( VRSystem()->GetTrackedDeviceIndexForControllerRole( TrackedControllerRole_RightHand ), poses );
		{
			// SteamVR's raw poses include our move; grabbing must use where the hands really are, or the world would
			// chase the hand that drags it.
			const TrackedDeviceIndex_t idx[3] = { k_unTrackedDeviceIndex_Hmd,
				VRSystem()->GetTrackedDeviceIndexForControllerRole( TrackedControllerRole_LeftHand ),
				VRSystem()->GetTrackedDeviceIndexForControllerRole( TrackedControllerRole_RightHand ) };
			Tracked *t[3] = { &hmd, &hands[0], &hands[1] };
			for ( int i = 0; i < 3; i++ )
				if ( t[i]->valid && !m_driver.TruePosition( idx[i], t[i]->pos, t[i]->pos ) )
					t[i]->valid = false;
		}
	}

	// How a button has to be pressed (double press, long press, ...) is up to the SteamVR binding.
	const bool dragHand[2] = { m_settings.dragLeft, m_settings.dragRight }, turnHand[2] = { m_settings.turnLeft, m_settings.turnRight };
	bool dragHeld[2], turnHeld[2];
	for ( int h = 0; h < 2; h++ )
	{
		dragHeld[h] = dragHand[h] && m_settings.dragEnabled && in.drag[h];
		turnHeld[h] = turnHand[h] && m_settings.turnEnabled && in.turn[h];
		const bool grabbing = dragHeld[h] || turnHeld[h];
		if ( grabbing && !m_wasGrabbing[h] && m_settings.haptics && m_inputOk )
			m_input.Pulse( h, 0.04f, 0.5f );
		m_wasGrabbing[h] = grabbing;
	}
	// One hand per gesture; the hand already holding keeps it.
	auto pick = []( const bool held[2], bool active0, bool active1 ) {
		if ( active0 && held[0] )
			return 0;
		if ( active1 && held[1] )
			return 1;
		return held[0] ? 0 : held[1] ? 1 : -1;
	};
	const int dragBy = pick( dragHeld, m_drag[0].Active(), m_drag[1].Active() );
	const int turnBy = pick( turnHeld, m_turn[0].Active(), m_turn[1].Active() );

	bool changed = false;
	const double snap = m_settings.snapTurn ? m_settings.snapDegrees * kDegToRad : 0;
	for ( int h = 0; h < 2; h++ )
	{
		changed |= m_drag[h].Update( h == dragBy, hands[h].valid, hands[h].pos, m_settings.dragGain, m_off, now );
		changed |= m_turn[h].Update( h == turnBy && hmd.valid, hands[h].valid, hands[h].pos, hmd.pos, m_off, snap );
		if ( m_turn[h].Snapped() && m_settings.haptics && m_inputOk )
			m_input.Pulse( h, 0.03f, 0.6f ); // one click per snap step
		Vec3 v;
		if ( m_drag[h].TakeRelease( v ) && m_settings.gravity )
			m_gravity.Launch( v ); // let go: the momentum carries on
	}
	if ( GestureActive() )
		m_gravity.Stop(); // holding the world: no falling
	if ( changed )
		m_dirty = true;
	(void)dt;
}

// ---------------------------------------------------------------------------------------------------------------------
// Events and main loop

void App::CheckUniverse( double now )
{
	const uint64_t u = ChaperoneStore::CurrentUniverse();
	if ( u == 0 || u == m_universe )
		return;
	logging::Printf( "tracking universe %llu -> %llu", (unsigned long long)m_universe, (unsigned long long)u );
	const bool viaDummy = !Real( u ) || !Real( m_universe );
	m_universe = u;
	m_worldChanged = m_worldChanged || viaDummy; // between real universes (relocalizing) raw space carries on
	if ( !Real( u ) )
	{
		if ( m_state != State::Paused )
			m_state = State::Waiting;
		m_driver.Write( {}, false ); // tracking lost: raw space is about to be a new one
		ResetGestures();
		return;
	}
	if ( m_state != State::Paused )
		ScheduleResync( Resync::Keep, now + kResyncDebounce, "tracking universe changed" );
}

void App::OnEvent( const VREvent_t &ev, double now )
{
	switch ( ev.eventType )
	{
	case VREvent_Quit:
		m_driver.Write( {}, false );
		VRSystem()->AcknowledgeQuit_Exiting();
		s_quit = true;
		break;
	case VREvent_ChaperoneRoomSetupStarting:
		// What the room setup ends with is the new original.
		Reset( "room setup started" );
		m_state = State::Paused;
		ScheduleResync( Resync::NewOriginal, now + kRoomSetupTimeout, "room setup" );
		break;
	case VREvent_StandingZeroPoseReset:
	case VREvent_SeatedZeroPoseReset:
		// While SteamVR's room setup runs, only its commit (807) or the timeout ends the pause: reading the saved copy
		// now would revert the working copy the setup is editing.
		if ( m_state == State::Paused )
			break;
		[[fallthrough]];
	case 807: // RoomSetupCommitted (RoomSetupFinished in older headers)
		ScheduleResync( Resync::NewOriginal, now + kResyncDebounce, EventName( ev.eventType ) );
		break;
	case VREvent_ChaperoneUniverseHasChanged:
	case VREvent_ChaperoneDataHasChanged:
		if ( m_state == State::Paused )
			break;
		ScheduleResync( Resync::Keep, now + kResyncDebounce, EventName( ev.eventType ) );
		break;
	case VREvent_ChaperoneFlushCache:
		m_nextUniverseCheck = 0; // how the Frame announces a new tracking universe
		break;
	default:
		break;
	}
}

void App::PollEvents( double now )
{
	VREvent_t ev;
	while ( VRSystem()->PollNextEvent( &ev, sizeof ev ) )
		OnEvent( ev, now );
}

void App::Tick( double now, double dt )
{
	PollEvents( now );
	if ( s_quit )
		return;
	m_driver.Poll( now );
	FollowDriver( now );
	if ( m_settings.checkUpdates && now >= m_nextUpdateCheck )
	{
		m_nextUpdateCheck = now + 12 * 3600.0; // at start (see Init) and twice a day
		m_updater.Check();
	}
	if ( const int u = (int)m_updater.GetState(); u != m_drawnUpdate )
	{
		m_drawnUpdate = u;
		m_panelDirty = true;
		if ( m_updater.GetState() == Updater::State::Installed )
		{
			logging::Printf( "updated to %s; restarting", m_updater.LatestTag().c_str() );
			m_restartAt = now + 2.0; // long enough to show "restarting" on the panel
		}
	}
	if ( m_restartAt >= 0 && now >= m_restartAt && !GestureActive() ) // not while the world is held
	{
		s_restart = true;
		s_quit = true;
		return;
	}
	if ( m_overlayOk )
	{
		VREvent_t ev;
		while ( m_overlay.PollEvent( ev ) )
			HandlePanelEvent( ev );
	}
	if ( now >= m_nextUniverseCheck )
	{
		m_nextUniverseCheck = now + 0.25;
		CheckUniverse( now );
	}
	// Not in the middle of a grab, unless it is long overdue.
	if ( m_resyncAt >= 0 && now >= m_resyncAt && ( !GestureActive() || now - m_resyncAt > 5.0 ) )
		DoResync( now );
	if ( m_state == State::Waiting && m_resyncAt < 0 && now >= m_nextRetry )
	{
		m_nextRetry = now + 2.0;
		ScheduleResync( Resync::Keep, now, "waiting for a playspace" );
	}

	InputFrame in;
	const bool haveInput = m_inputOk && m_input.Poll( in ); // every tick: SteamVR reports bindings only for polled actions
	if ( m_state != m_drawnState )
	{
		m_drawnState = m_state;
		m_panelDirty = true;
	}
	if ( m_driverMode && !m_off.IsZero() && !m_dirty && now - m_lastWrite > 0.25 )
	{
		m_driver.Write( m_off, true ); // heartbeat (also while waiting for SteamVR): without it the driver drops the move after 2 s
		m_lastWrite = now;
	}
	if ( m_state != State::Ready || !m_driverMode )
		goto PanelTick;
	if ( haveInput )
		HandleInput( in, now, dt );
	if ( m_settings.gravity && !GestureActive() && m_gravity.Step( m_off, dt, m_settings.gravityStrength ) )
		m_dirty = true; // falling (or flying after a throw)
	if ( m_dirty )
	{
		ShowMove( now );
		m_panelDirty = true;
	}
PanelTick:
	if ( m_overlayOk && m_panelDirty && m_overlay.Visible() && now >= m_nextPanelDraw )
		RedrawPanel( now );
}

int App::Run()
{
	double last = Now();
	while ( !s_quit )
	{
		const double now = Now();
		Tick( now, std::min( now - last, 0.1 ) );
		last = now;
		// Every frame while the world is being moved, otherwise a relaxed poll for the buttons.
		double wait = 0.02;
		if ( GestureActive() || m_dirty || ( m_settings.gravity && m_gravity.Airborne( m_off, m_settings.gravityStrength ) ) )
		{
			const double period = 1.0 / m_displayHz;
			float since = 0;
			uint64_t frame = 0;
			wait = period;
			if ( VRSystem()->GetTimeSinceLastVsync( &since, &frame ) )
				wait = period - since + 0.002; // just after the next vsync, when fresh poses are in
			while ( wait < 0.002 )
				wait += period;
		}
		std::this_thread::sleep_for( std::chrono::duration<double>( wait ) );
	}
	return 0;
}
