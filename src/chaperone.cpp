#include "chaperone.h"

#include <openvr.h>

#include <chrono>
#include <thread>

using namespace vr;

namespace
{
Mat34 FromVr( const HmdMatrix34_t &a )
{
	Mat34 r;
	for ( int i = 0; i < 3; i++ )
		for ( int j = 0; j < 4; j++ )
			r.m[i][j] = a.m[i][j];
	return r;
}

void Sleep( int ms )
{
	std::this_thread::sleep_for( std::chrono::milliseconds( ms ) );
}
} // namespace

bool ChaperoneStore::ReadSaved( Mat34 &standing, Mat34 &seated )
{
	IVRChaperoneSetup *cs = VRChaperoneSetup();
	if ( !cs )
		return false;
	// While SteamVR reloads the saved copy the working copy cannot be read for a moment.
	for ( int i = 0; i < 40; i++ )
	{
		cs->RevertWorkingCopy();
		HmdMatrix34_t st, se;
		if ( cs->GetWorkingStandingZeroPoseToRawTrackingPose( &st ) && cs->GetWorkingSeatedZeroPoseToRawTrackingPose( &se ) )
		{
			standing = FromVr( st );
			seated = FromVr( se );
			return Finite( standing ) && Finite( seated );
		}
		Sleep( 25 );
	}
	return false;
}

uint64_t ChaperoneStore::CurrentUniverse()
{
	ETrackedPropertyError err = TrackedProp_Success;
	const uint64_t id = VRSystem()->GetUint64TrackedDeviceProperty( k_unTrackedDeviceIndex_Hmd, Prop_CurrentUniverseId_Uint64, &err );
	return err == TrackedProp_Success ? id : 0;
}
