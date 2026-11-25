#pragma once
// The dashboard panel: what it shows and where its buttons are. No OpenVR here, so it renders for --screenshot too.
// Laid out for people who know OpenVR Advanced Settings: one card each for space drag and space turn (switch on top, its
// settings below), then gravity, then the options that are rarely touched.

#include "canvas.h"
#include "svg.h"

#include "core/config.h"
#include "core/math.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

constexpr int kPanelW = 1840, kPanelH = 944;

// One binding of one of our actions, as SteamVR reports it (GetActionBindingInfo).
struct BindingInfo
{
	int action = 0;     // 0 space drag, 1 space turn, 2 position reset
	int hand = -1;      // 0 left, 1 right, -1 neither
	std::string input;  // "/input/bumper"
	std::string slot;   // "click", "double", "long", "held", "touch", ...
};

struct PanelModel
{
	Offset off;
	Settings settings;
	bool inputOk = true;
	bool waiting = false;       // no tracking / no playspace yet
	bool driverMode = true;     // false: the driver is not loaded, nothing can be moved
	bool driverBlocked = false; // the driver stopped itself after a start that did not finish (crash marker)
	std::vector<BindingInfo> bindings;
	bool bindingsKnown = false; // false: SteamVR Input is not available, nothing to show
	bool autoStart = true;      // SteamVR starts the app with itself
	std::string version;        // this program's version, "0.1.0"
	bool driverRestart = false; // the installed driver file is newer than the running SteamVR: restart SteamVR to load it
	int update = 0;             // Updater::State as a number (3 available, 4 applying, 5 installed, 6 failed)
	std::string updateTag;      // the latest release's tag when known
};

enum class Act
{
	None,
	Reset,
	Drag,
	Turn,
	GainMinus,
	GainPlus,
	DragLeft,
	DragRight,
	LockX, // space drag axis locks
	LockY,
	LockZ,
	SnapTurn,
	SnapMinus,
	SnapPlus,
	TurnLeft,
	TurnRight,
	Gravity,
	GravityEarth,
	GravityMoon,
	GravityZero,
	GravityMinus,
	GravityPlus,
	GravityValue, // type a value on SteamVR's keyboard
	Haptics,
	ResetEnabled,
	EditBindings,
	Language,
	AutoStart, // SteamVR starts the app with itself
	CheckUpdates,
	UpdateNow,
	UpdateRetry,
	WelcomeOk,
	Quit,
};

struct Button
{
	int x, y, w, h;
	Act act;
};

// "ja" or "en" for a settings value of auto / ja / en (auto: the Steam client's language, then $LANG).
std::string ResolveLanguage( const std::string &setting );

class Panel
{
public:
	void Draw( Canvas &c, const PanelModel &m, Act hover, const std::string &lang );
	Act HitTest( int x, int y ) const; // panel pixels, origin top-left
	// The app icon (same design as data/icon.svg): a turn arrow around four move arrows.
	static void DrawIcon( Canvas &c );
	// Where SteamVR is installed: the controller art and its button points are read from there.
	void SetSteamVRRoot( const std::string &root ) { m_root = root; }

private:
	void LoadArt();
	void DrawBindings( Canvas &c, const PanelModel &m, const std::string &lang, int x, int y, int w, int h );

	std::vector<Button> m_buttons;
	std::string m_root = "/opt/steamvr";
	bool m_artTried = false;
	std::unique_ptr<Canvas> m_art[2]; // left, right controller, drawn once at the panel's scale
	double m_artScale = 0, m_artW = 0;
	std::map<std::string, Canvas::Pt> m_points; // button points of the right controller, in art units
};

// Applies a settings button to `s`; false when `a` is not a settings button.
bool ApplySettingsAct( Act a, Settings &s );
