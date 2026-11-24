#include "input.h"

using namespace vr;

namespace
{
constexpr const char *kSet = "/actions/space";
constexpr const char *kNames[] = { "/actions/space/in/drag", "/actions/space/in/turn", "/actions/space/in/reset",
	"/actions/space/out/haptic" };
} // namespace

bool Input::Init( const std::string &manifestPath, std::string &error )
{
	IVRInput *in = VRInput();
	if ( !in )
	{
		error = "VRInput unavailable";
		return false;
	}
	if ( EVRInputError e = in->SetActionManifestPath( manifestPath.c_str() ); e != VRInputError_None )
	{
		error = "SetActionManifestPath failed (" + std::to_string( (int)e ) + ")";
		return false;
	}
	if ( in->GetActionSetHandle( kSet, &m_set ) != VRInputError_None )
	{
		error = "GetActionSetHandle failed";
		return false;
	}
	for ( int i = 0; i < Count; i++ )
		if ( in->GetActionHandle( kNames[i], &m_action[i] ) != VRInputError_None )
		{
			error = std::string( "GetActionHandle failed: " ) + kNames[i];
			return false;
		}
	in->GetInputSourceHandle( "/user/hand/left", &m_hand[0] );
	in->GetInputSourceHandle( "/user/hand/right", &m_hand[1] );
	m_ok = true;
	return true;
}

bool Input::Poll( InputFrame &out )
{
	out = {};
	if ( !m_ok )
		return false;
	IVRInput *in = VRInput();
	VRActiveActionSet_t active = {};
	active.ulActionSet = m_set;
	if ( in->UpdateActionState( &active, sizeof active, 1 ) != VRInputError_None )
		return false;
	auto state = [&]( int action, VRInputValueHandle_t from ) {
		InputDigitalActionData_t d;
		return in->GetDigitalActionData( m_action[action], &d, sizeof d, from ) == VRInputError_None && d.bActive && d.bState;
	};
	for ( int h = 0; h < 2; h++ )
	{
		out.drag[h] = state( Drag, m_hand[h] );
		out.turn[h] = state( Turn, m_hand[h] );
	}
	out.reset = state( Reset, k_ulInvalidInputValueHandle );
	return true;
}

void Input::Pulse( int hand, float seconds, float amplitude )
{
	if ( m_ok )
		VRInput()->TriggerHapticVibrationAction( m_action[Haptic], 0.0f, seconds, 150.0f, amplitude, m_hand[hand] );
}

bool Input::OpenBindingEditor( const char *appKey )
{
	return m_ok && VRInput()->OpenBindingUI( appKey, m_set, k_ulInvalidInputValueHandle, false ) == VRInputError_None;
}

bool Input::Bindings( std::vector<BindingInfo> &out )
{
	out.clear();
	if ( !m_ok )
		return false;
	for ( int a = Drag; a <= Reset; a++ )
	{
		InputBindingInfo_t info[16];
		uint32_t n = 0;
		if ( VRInput()->GetActionBindingInfo( m_action[a], info, sizeof info[0], 16, &n ) != VRInputError_None )
			continue; // not bound
		for ( uint32_t i = 0; i < n && i < 16; i++ )
		{
			BindingInfo b;
			b.action = a - Drag;
			const std::string dev = info[i].rchDevicePathName;
			b.hand = dev == "/user/hand/left" ? 0 : dev == "/user/hand/right" ? 1 : -1;
			b.input = info[i].rchInputPathName;
			b.slot = info[i].rchSlotName;
			out.push_back( b );
		}
	}
	return true;
}
