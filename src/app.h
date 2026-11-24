#pragma once
#include "chaperone.h"
#include "driver_link.h"
#include "input.h"
#include "overlay.h"
#include "updater.h"
#include "ui/panel.h"

#include "core/config.h"
#include "core/gestures.h"
#include "core/math.h"

#include <atomic>
#include <string>

constexpr const char *kAppKey = "dev.krha.framespacesettings";
// User values of the SteamVR keyboard: which number is being typed.
constexpr uint64_t kKeyboardGravity = 1;

class App
{
public:
	static std::atomic<bool> s_quit;
	static std::atomic<bool> s_restart; // after Run returns: start the (updated) installed program in this process

	~App();
	bool Init( std::string &error );
	static bool ConnectSafely( std::string &error );
	int Run();

private:
	enum class State
	{
		Waiting, // no tracking or no saved playspace yet
		Ready,
		Paused, // SteamVR's room setup is running
	};
	enum class Resync
	{
		Keep,        // SteamVR reloaded its playspace: read it again, the move stays
		NewOriginal, // the user may have recentered: if the saved playspace changed, the move goes
	};

	void Tick( double now, double dt );
	void PollEvents( double now );
	void OnEvent( const vr::VREvent_t &ev, double now );
	void CheckUniverse( double now );
	void ScheduleResync( Resync mode, double at, const char *why );
	void DoResync( double now );
	void HandleInput( const InputFrame &in, double now, double dt );
	void ShowMove( double now );
	void Reset( const char *why );
	void ResetGestures();
	bool GestureActive() const;
	bool Real( uint64_t u ) const { return u != 0 && u != kDummyUniverse; }
	void Pulse( bool both );
	void HandlePanelEvent( const vr::VREvent_t &ev );
	void HandlePanelAct( Act a );
	void RedrawPanel( double now );
	void FollowDriver( double now );

	Settings m_settings;
	Input m_input;
	bool m_inputOk = false;

	State m_state = State::Waiting;
	uint64_t m_universe = 0;
	bool m_worldChanged = false; // tracking was lost since the last resync: raw space may be a new one

	Mat34 m_baseStanding; // the user's own (saved) standing playspace, raw space
	bool m_haveBase = false;
	Offset m_off; // the move
	bool m_dirty = false;
	double m_lastWrite = -10;

	double m_nextUniverseCheck = 0, m_nextRetry = 0;
	double m_resyncAt = -1;
	Resync m_resyncMode = Resync::Keep;
	std::string m_resyncWhy;

	bool m_panelDirty = true;
	PanelOverlay m_overlay;
	bool m_overlayOk = false;
	Panel m_panel;
	Act m_hover = Act::None;
	std::string m_lang = "ja";
	uint64_t m_lastPanelHash = 0;
	std::string m_lastBindings = "?";
	double m_nextPanelDraw = 0;
	// The Frame Space Settings SteamVR driver moves the devices; without it nothing is moved.
	DriverLink m_driver;
	bool m_driverMode = false; // the driver is loaded and working
	double m_nextDriverCheck = 0;
	bool m_driverRestart = false; // the installed driver is newer than the running SteamVR
	Updater m_updater;
	double m_nextUpdateCheck = 0, m_restartAt = -1;
	int m_drawnUpdate = -1;
	State m_drawnState = State::Waiting;
	double m_displayHz = 90;

	DragGesture m_drag[2];
	TurnGesture m_turn[2];
	bool m_wasGrabbing[2] = {};
	bool m_resetWasDown = false;
	GravityMotion m_gravity;
};
