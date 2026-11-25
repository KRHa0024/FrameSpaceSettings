#include "app.h"

#include "core/files.h"
#include "ui/panel.h"
#include "core/log.h"
#include "core/version.h"

#include <openvr.h>

#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
void OnSignal( int )
{
	App::s_quit = true;
}

void OnShow( int )
{
	App::s_show = true;
}

std::string SelfPath()
{
	char exe[4096];
	const ssize_t n = readlink( "/proc/self/exe", exe, sizeof exe - 1 );
	return n > 0 ? std::string( exe, (size_t)n ) : std::string();
}

std::string BinPath() { return files::Home() + "/.local/bin/frame-space-settings"; }
std::string ManifestPath() { return files::DataDir() + "/frame-space-settings.vrmanifest"; }
std::string DriverDir() { return files::DataDir() + "/driver/framespacesettings"; }
// The entry SteamVR's dashboard lists under its + button, as other Frame tools do.
std::string DesktopPath() { return files::Home() + "/.local/share/applications/frame-space-settings.desktop"; }
std::string IconPath( int size )
{
	return files::Home() + "/.local/share/icons/hicolor/" + std::to_string( size ) + "x" + std::to_string( size ) + "/apps/frame-space-settings.png";
}
constexpr int kIconSizes[] = { 128, 256 };
constexpr const char *kVrPathReg = "/opt/steamvr/bin/vrpathreg.sh";

bool Copy( const std::string &from, const std::string &to, bool executable );

// The SteamVR driver: copied next to the app's data and registered as an external driver. vrserver
// loads it on its next start. Copies go through a rename, so a driver file vrserver has loaded is never overwritten.
bool InstallDriver( const std::string &buildDir )
{
	const std::string so = buildDir + "/driver_framespacesettings.so", manifest = buildDir + "/data/driver/driver.vrdrivermanifest";
	if ( !files::Exists( so ) || !files::Exists( manifest ) )
	{
		printf( "driver not built (%s): nothing can be moved without it\n", so.c_str() );
		return false;
	}
	files::MakeDirs( DriverDir() + "/bin/linuxarm64" );
	if ( !Copy( manifest, DriverDir() + "/driver.vrdrivermanifest", false ) ||
		 !Copy( so, DriverDir() + "/bin/linuxarm64/driver_framespacesettings.so", true ) )
	{
		fprintf( stderr, "could not copy the driver to %s\n", DriverDir().c_str() );
		return false;
	}
	const std::string cmd = std::string( kVrPathReg ) + " adddriver '" + DriverDir() + "' >/dev/null 2>&1";
	if ( system( cmd.c_str() ) != 0 )
	{
		fprintf( stderr, "vrpathreg could not register the driver\n" );
		return false;
	}
	printf( "driver installed and registered: SteamVR loads it on its next start\n" );
	return true;
}

bool Copy( const std::string &from, const std::string &to, bool executable )
{
	std::string data;
	if ( !files::ReadFile( from, data ) || !files::WriteFileAtomic( to, data ) )
		return false;
	return !executable || chmod( to.c_str(), 0755 ) == 0;
}

std::string LockPath() { return files::StateDir() + "/lock"; }

// One running copy only: two would fight over the driver's move. The holder's pid goes in the file, for ShowRunning.
// SIGUSR1 must be handled before the pid is there: its default action ends the process.
bool LockSingleInstance()
{
	files::MakeDirs( files::StateDir() );
	const int fd = open( LockPath().c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644 );
	if ( fd < 0 || flock( fd, LOCK_EX | LOCK_NB ) != 0 ) // fd stays open for the life of the process
		return false;
	const std::string pid = std::to_string( getpid() ) + "\n";
	if ( ftruncate( fd, 0 ) != 0 || pwrite( fd, pid.data(), pid.size(), 0 ) != (ssize_t)pid.size() )
		logging::Printf( "could not write the pid to %s", LockPath().c_str() );
	return true;
}

// Launched again (from the dashboard's + button) while running: the running copy opens its panel.
bool ShowRunning()
{
	std::string text;
	if ( !files::ReadFile( LockPath(), text ) )
		return false;
	const pid_t pid = (pid_t)atoi( text.c_str() );
	std::string comm; // only signal our own program: a stale pid may belong to anything by now
	if ( pid <= 1 || !files::ReadFile( "/proc/" + std::to_string( pid ) + "/comm", comm ) || comm.rfind( "frame-space-set", 0 ) != 0 )
		return false;
	return kill( pid, SIGUSR1 ) == 0;
}

// The dashboard's + button lists the desktop entries. Exec is absolute: Steam may start it without ~/.local/bin in PATH.
bool InstallDesktopEntry()
{
	for ( const int size : kIconSizes )
	{
		const std::string path = IconPath( size );
		files::MakeDirs( path.substr( 0, path.rfind( '/' ) ) );
		Canvas icon( size, size );
		Panel::DrawIcon( icon );
		if ( !icon.WritePng( path ) )
			return false;
	}
	files::MakeDirs( DesktopPath().substr( 0, DesktopPath().rfind( '/' ) ) );
	const std::string entry = "[Desktop Entry]\n"
							  "Type=Application\n"
							  "Name=Frame Space Settings\n"
							  "Comment=Space drag and space turn. Launch again while running to open the panel.\n"
							  "Comment[ja]=空間をドラッグして移動・回転。常駐中にもう一度起動するとパネルを開く\n"
							  "Exec=\"" + BinPath() + "\" --show\n"
							  "Icon=frame-space-settings\n"
							  "Terminal=false\n"
							  "Categories=Utility;\n";
	return files::WriteFileAtomic( DesktopPath(), entry );
}

bool InitUtility()
{
	vr::EVRInitError err = vr::VRInitError_None;
	vr::VR_Init( &err, vr::VRApplication_Background ); // never starts vrserver (see App::ConnectSafely)
	if ( err != vr::VRInitError_None )
	{
		fprintf( stderr, "SteamVR is not reachable (%s); start SteamVR and try again\n", vr::VR_GetVRInitErrorAsEnglishDescription( err ) );
		return false;
	}
	return true;
}

// The pieces go in from the least to the most visible, the program itself last: if a step fails (also during an
// in-app update) the installed program stays the old one. Each file is replaced by an atomic rename.
int Install()
{
	const std::string self = SelfPath(), dir = self.substr( 0, self.rfind( '/' ) ), src = dir + "/data";
	const std::string data = files::DataDir();
	files::MakeDirs( data + "/bindings" );
	files::MakeDirs( files::Home() + "/.local/bin" );
	if ( !InstallDriver( dir ) )
	{
		fprintf( stderr, "the driver could not be installed: nothing can be moved without it\n" );
		return 1;
	}
	if ( !Copy( src + "/action_manifest.json", data + "/action_manifest.json", false ) ||
		 !Copy( src + "/bindings/frame_controller.json", data + "/bindings/frame_controller.json", false ) )
	{
		fprintf( stderr, "could not copy the program's data (run --install from the build or release folder)\n" );
		return 1;
	}
	char manifest[2048];
	snprintf( manifest, sizeof manifest,
		"{\n"
		"  \"source\": \"user\",\n"
		"  \"applications\": [{\n"
		"    \"app_key\": \"%s\",\n"
		"    \"launch_type\": \"binary\",\n"
		"    \"binary_path_linux\": \"%s\",\n"
		"    \"binary_path_linux_arm\": \"%s\",\n"
		"    \"action_manifest_path\": \"%s/action_manifest.json\",\n"
		"    \"is_dashboard_overlay\": true,\n"
		"    \"strings\": {\n"
		"      \"en_us\": { \"name\": \"Frame Space Settings\", \"description\": \"Drag the space to move and turn\" },\n"
		"      \"ja_jp\": { \"name\": \"Frame Space Settings\", \"description\": \"空間をドラッグして移動・回転\" }\n"
		"    }\n"
		"  }]\n"
		"}\n",
		kAppKey, BinPath().c_str(), BinPath().c_str(), data.c_str() );
	if ( !files::WriteFileAtomic( ManifestPath(), manifest ) )
	{
		fprintf( stderr, "could not write %s\n", ManifestPath().c_str() );
		return 1;
	}
	// SteamVR only takes an application whose binary is there: on a first install the program goes in before the
	// registration (an update keeps replacing it last, so a failed update leaves the old one working).
	if ( !files::Exists( BinPath() ) && !Copy( self, BinPath(), true ) )
	{
		fprintf( stderr, "could not copy the program to %s\n", BinPath().c_str() );
		return 1;
	}
	if ( !InitUtility() )
		return 1;
	// Start with SteamVR on the first install only; an update (or installing again) keeps the user's choice.
	const bool firstInstall = !vr::VRApplications()->IsApplicationInstalled( kAppKey );
	const vr::EVRApplicationError ae = vr::VRApplications()->AddApplicationManifest( ManifestPath().c_str(), false );
	vr::EVRApplicationError autoErr = vr::VRApplicationError_None;
	if ( ae == vr::VRApplicationError_None && firstInstall )
		autoErr = vr::VRApplications()->SetApplicationAutoLaunch( kAppKey, true );
	const std::string aeName = vr::VRApplications()->GetApplicationsErrorNameFromEnum( ae );
	const std::string autoName = vr::VRApplications()->GetApplicationsErrorNameFromEnum( autoErr );
	vr::VR_Shutdown();
	if ( ae != vr::VRApplicationError_None )
	{
		fprintf( stderr, "SteamVR refused the registration of %s (error %d, %s)\n", ManifestPath().c_str(), (int)ae, aeName.c_str() );
		return 1;
	}
	// Not starting with SteamVR is no reason to fail the install: it can be turned on in the panel.
	if ( autoErr != vr::VRApplicationError_None )
		fprintf( stderr,
			"could not make SteamVR start it (error %d, %s); start %s once and turn on \"Start with SteamVR\" in its panel\n",
			(int)autoErr, autoName.c_str(), BinPath().c_str() );
	if ( !Copy( self, BinPath(), true ) )
	{
		fprintf( stderr, "could not copy the program to %s\n", BinPath().c_str() );
		return 1;
	}
	// Last, like the program: an entry the + button lists must start something.
	if ( !InstallDesktopEntry() )
		fprintf( stderr, "could not add %s: the dashboard's + button will not list it\n", DesktopPath().c_str() );
	printf( "installed %s%s\n", BinPath().c_str(), firstInstall && autoErr == vr::VRApplicationError_None ? "; SteamVR starts it from now on" : "" );
	return 0;
}

int Uninstall()
{
	if ( InitUtility() )
	{
		vr::VRApplications()->SetApplicationAutoLaunch( kAppKey, false );
		vr::VRApplications()->RemoveApplicationManifest( ManifestPath().c_str() );
		vr::VR_Shutdown();
	}
	if ( files::Exists( DriverDir() ) )
	{
		const std::string cmd = std::string( kVrPathReg ) + " removedriver '" + DriverDir() + "' >/dev/null 2>&1";
		if ( system( cmd.c_str() ) != 0 )
			fprintf( stderr, "vrpathreg could not unregister the driver; remove it with: %s removedriver %s\n", kVrPathReg, DriverDir().c_str() );
	}
	// A driver file vrserver has loaded stays valid after unlink until SteamVR restarts.
	for ( const int size : kIconSizes )
		unlink( IconPath( size ).c_str() );
	for ( const std::string &f : { DesktopPath(), BinPath(), ManifestPath(), files::DataDir() + "/action_manifest.json",
			  files::DataDir() + "/bindings/frame_controller.json", DriverDir() + "/bin/linuxarm64/driver_framespacesettings.so",
			  DriverDir() + "/driver.vrdrivermanifest" } )
		unlink( f.c_str() );
	printf( "uninstalled (settings are kept in %s)\n", files::ConfigDir().c_str() );
	return 0;
}

void Usage()
{
	printf( "frame-space-settings - space drag and space turn for the Steam Frame\n\n"
			"  (no arguments)   run (normally started by SteamVR)\n"
			"  --show           run and open the panel in the dashboard; if already running, open its panel\n"
			"  --screenshot FILE.png [ja|en]  render the panel with sample data (no SteamVR)\n"
			"  --install        copy to ~/.local and register with SteamVR (starts with SteamVR)\n"
			"  --version        print the version\n"
			"  --uninstall      undo --install\n" );
}
} // namespace

int main( int argc, char **argv )
{
	const std::string arg = argc > 1 ? argv[1] : "";
	if ( arg == "-h" || arg == "--help" )
	{
		Usage();
		return 0;
	}
	if ( arg == "--version" )
	{
		printf( "%s\n", FSS_VERSION );
		return 0;
	}
	if ( arg == "--install" )
		return Install();
	if ( arg == "--uninstall" )
		return Uninstall();
	if ( arg == "--screenshot" && argc > 2 )
	{
		// The panel with sample data, without SteamVR.
		PanelModel m;
		m.off.Translate( { 0.31, -0.30, 0.25 } );
		m.off.yaw = 45 * M_PI / 180;
		m.settings.snapTurn = true;
		m.settings.lockX = m.settings.lockZ = true; // up and down only
		// Arguments after FILE and the language only pick a sample state: a 3rd turns gravity on (and hovers space turn),
		// a 4th also shows the missing-driver warning strip.
		m.settings.gravity = argc > 4;
		m.driverMode = argc <= 5;
		m.bindingsKnown = true; // the default bindings
		m.bindings = { { 0, 0, "/input/dpad_right", "click" }, { 0, 1, "/input/x", "click" }, { 1, 0, "/input/dpad_down", "double" },
			{ 1, 1, "/input/a", "double" }, { 2, 0, "/input/thumbstick", "double" } };
		if ( const char *b = getenv( "FSS_BINDINGS" ) ) // other bindings to look at: "action:hand:/input/x:slot,..."
		{
			m.bindings.clear();
			std::string all = b;
			size_t i = 0;
			while ( i < all.size() )
			{
				size_t e = all.find( ',', i );
				if ( e == std::string::npos )
					e = all.size();
				const std::string one = all.substr( i, e - i );
				i = e + 1;
				const size_t a = one.find( ':' ), h = one.find( ':', a + 1 ), s = one.rfind( ':' );
				if ( a == std::string::npos || h == std::string::npos || s <= h )
					continue;
				m.bindings.push_back( { atoi( one.c_str() ), atoi( one.c_str() + a + 1 ), one.substr( h + 1, s - h - 1 ), one.substr( s + 1 ) } );
			}
		}
		Canvas c( kPanelW, kPanelH );
		Panel p;
		Act hover = argc > 4 ? Act::Turn : Act::None;
		if ( const char *h = getenv( "FSS_HOVER" ) ) // which element the laser is on, by its number in enum Act
			hover = (Act)atoi( h );
		m.version = FSS_VERSION;
		m.settings.welcomeDone = !getenv( "FSS_WELCOME" ); // other notices to look at
		if ( const char *u = getenv( "FSS_UPDATE" ) )
			m.update = atoi( u ), m.updateTag = "v0.2.0";
		m.driverRestart = getenv( "FSS_DRIVER_RESTART" ) != nullptr;
		m.driverBlocked = getenv( "FSS_BLOCKED" ) != nullptr;
		if ( getenv( "FSS_UNMOVED" ) )
			m.off = {};
		p.Draw( c, m, hover, argc > 3 ? argv[3] : "ja" );
		Canvas icon( 128, 128 );
		Panel::DrawIcon( icon );
		icon.WritePng( std::string( argv[2] ) + ".icon.png" );
		return c.WritePng( argv[2] ) ? 0 : 1;
	}
	const bool show = arg == "--show";
	if ( !arg.empty() && !show )
	{
		Usage();
		return 2;
	}

	signal( SIGINT, OnSignal );
	signal( SIGTERM, OnSignal );
	signal( SIGHUP, OnSignal );
	signal( SIGUSR1, OnShow );
	if ( !LockSingleInstance() )
	{
		if ( show && ShowRunning() )
			return 0;
		fprintf( stderr, "frame-space-settings is already running\n" );
		return 1;
	}
	App::s_show = show;
	logging::Open( files::StateDir() + "/frame-space-settings.log" );

	int rc = 0;
	{
		App app;
		std::string error;
		if ( !app.Init( error ) )
		{
			logging::Printf( "%s", error.c_str() );
			return 1;
		}
		logging::Printf( "started (version %s)", FSS_VERSION );
		rc = app.Run();
	} // the app is gone (move dropped, SteamVR connection closed) before an update restarts it
	if ( App::s_restart )
	{
		const std::string bin = BinPath();
		char *const args[] = { const_cast<char *>( bin.c_str() ), nullptr };
		signal( SIGUSR1, SIG_IGN ); // kept across exec, until the new program handles it: the lock file still has this pid
		execv( bin.c_str(), args ); // same process: SteamVR keeps it as its auto-started app; the lock fd is CLOEXEC
		logging::Printf( "could not start the updated program: %s", strerror( errno ) );
		return 1;
	}
	return rc;
}
