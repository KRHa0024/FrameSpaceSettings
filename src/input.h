#pragma once
#include "ui/panel.h"

#include <openvr.h>
#include <string>
#include <utility>
#include <vector>

struct InputFrame
{
	bool drag[2] = {}, turn[2] = {}; // space drag / space turn, [0] left hand, [1] right hand
	bool reset = false;
};

class Input
{
public:
	bool Init( const std::string &manifestPath, std::string &error );
	// Must run every tick: SteamVR only reports bindings for actions that are being polled.
	bool Poll( InputFrame &out );
	void Pulse( int hand, float seconds, float amplitude );
	bool OpenBindingEditor( const char *appKey );
	// What each of our actions is bound to right now. False when SteamVR Input is not available.
	bool Bindings( std::vector<BindingInfo> &out );

private:
	enum
	{
		Drag,
		Turn,
		Reset,
		Haptic,
		Count
	};
	vr::VRActionSetHandle_t m_set = vr::k_ulInvalidActionSetHandle;
	vr::VRActionHandle_t m_action[Count] = {};
	vr::VRInputValueHandle_t m_hand[2] = {};
	bool m_ok = false;
};
