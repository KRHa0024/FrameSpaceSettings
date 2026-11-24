// Core logic tests: no SteamVR needed.
#include "core/config.h"
#include "core/version.h"
#include "core/gestures.h"
#include "core/math.h"

#include <cstdio>
#include <string>

static int g_failures = 0;
#define CHECK( cond )                                                                                                  \
	do                                                                                                                 \
	{                                                                                                                  \
		if ( !( cond ) )                                                                                               \
		{                                                                                                              \
			printf( "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond );                                                   \
			g_failures++;                                                                                              \
		}                                                                                                              \
	} while ( 0 )

static Mat34 SomePose( double yaw, Vec3 t )
{
	return YawTranslate( yaw, t );
}

static void TestMath()
{
	const Mat34 a = SomePose( 0.7, { 1, 2, 3 } ), b = SomePose( -1.9, { -4, 0.5, 2 } );
	CHECK( NearPose( Mul( a, Inverse( a ) ), Mat34(), 1e-12, 1e-9 ) );
	CHECK( NearPose( Mul( Mul( a, b ), Inverse( b ) ), a, 1e-12, 1e-9 ) );
	CHECK( std::fabs( YawOf( a ) - 0.7 ) < 1e-12 );
	CHECK( std::fabs( WrapAngle( 3 * M_PI ) - M_PI ) < 1e-12 || std::fabs( WrapAngle( 3 * M_PI ) + M_PI ) < 1e-12 );
	CHECK( std::fabs( WrapAngle( 0.25 ) - 0.25 ) < 1e-15 );
	// RotateY and YawTranslate agree.
	const Vec3 p{ 0.3, 1, -2 }, r = RotateY( p, 0.4 );
	Mat34 pm;
	pm.m[0][3] = p.x, pm.m[1][3] = p.y, pm.m[2][3] = p.z;
	const Vec3 r2 = Mul( YawTranslate( 0.4, {} ), pm ).T();
	CHECK( std::fabs( r.x - r2.x ) < 1e-12 && std::fabs( r.z - r2.z ) < 1e-12 );
	// The move recovered from its matrix.
	Offset off;
	off.Translate( { 1, -0.3, 2 } );
	off.RotateAbout( { 0.5, 1.7, -1 }, 0.9 );
	const Offset back = Offset::FromMatrix( off.Matrix() );
	CHECK( std::fabs( WrapAngle( back.yaw - off.yaw ) ) < 1e-12 && std::fabs( back.t.x - off.t.x ) < 1e-12 &&
		   std::fabs( back.t.z - off.t.z ) < 1e-12 );
	// Turning about a point keeps that point where it is.
	Offset turn;
	const Vec3 pivot{ 2, 1, -3 };
	turn.RotateAbout( pivot, 1.1 );
	Mat34 pv;
	pv.m[0][3] = pivot.x, pv.m[1][3] = pivot.y, pv.m[2][3] = pivot.z;
	const Vec3 moved = Mul( turn.Matrix(), pv ).T();
	CHECK( std::fabs( moved.x - pivot.x ) < 1e-12 && std::fabs( moved.z - pivot.z ) < 1e-12 );
	CHECK( std::fabs( RotationAngle( SomePose( 0, {} ), SomePose( 1e-6, {} ) ) - 1e-6 ) < 1e-12 );
	CHECK( !InRange( SomePose( 0, { 0, 0, 5e4 } ) ) && InRange( SomePose( 0, { 0, 0, 3e4 } ) ) );
	CHECK( OnlyHeightDiffers( SomePose( 0.3, { 1, 0.066, 2 } ), SomePose( 0.3, { 1, 0, 2 } ) ) );
	CHECK( !OnlyHeightDiffers( SomePose( 0.3, { 1.1, 0.066, 2 } ), SomePose( 0.3, { 1, 0, 2 } ) ) ); // a recenter moves x/z too
	CHECK( !OnlyHeightDiffers( SomePose( 0.3, { 1, 0, 2 } ), SomePose( 0.3, { 1, 0, 2 } ) ) );
}

static void TestGestures()
{
	Offset off;
	DragGesture drag;
	CHECK( !drag.Update( true, true, { 0, 1, 0 }, 1, off, 0.00 ) ); // first sample anchors
	CHECK( drag.Update( true, true, { 0.1, 1.2, 0 }, 1, off, 0.01 ) );
	CHECK( std::fabs( off.t.x - 0.1 ) < 1e-12 && std::fabs( off.t.y - 0.2 ) < 1e-12 );
	drag.Update( true, true, { 0.2, 1.5, 0 }, 1, off, 0.02 );
	CHECK( std::fabs( off.t.y - 0.5 ) < 1e-12 );
	drag.Update( true, false, { 9, 9, 9 }, 1, off, 0.03 ); // tracking lost: pause
	drag.Update( true, true, { 5, 5, 5 }, 1, off, 0.04 );  // re-anchor, no jump
	CHECK( std::fabs( off.t.x - 0.2 ) < 1e-12 );

	{
		// Letting go hands over the momentum; losing tracking does not.
		Offset o;
		DragGesture d;
		for ( int i = 0; i <= 10; i++ )
			d.Update( true, true, { 0.02 * i, 1, 0 }, 1, o, 0.01 * i ); // 2 m/s along +x
		d.Update( false, true, { 0.2, 1, 0 }, 1, o, 0.11 );
		Vec3 v;
		CHECK( d.TakeRelease( v ) && std::fabs( v.x - 2.0 ) < 0.1 && !d.TakeRelease( v ) );
		d.Update( true, true, { 0, 1, 0 }, 1, o, 0.2 );
		d.Update( true, false, { 0, 1, 0 }, 1, o, 0.21 );
		CHECK( !d.TakeRelease( v ) );
	}
	{
		// Snap turn: nothing until the hand has swung a full step, then exactly one step.
		Offset o;
		TurnGesture tg;
		const double snap = 30 * M_PI / 180;
		tg.Update( true, true, { 0, 1, 0.5 }, { 0, 1, 0 }, o, snap );
		tg.Update( true, true, { 0.5 * std::sin( 0.4 ), 1, 0.5 * std::cos( 0.4 ) }, { 0, 1, 0 }, o, snap ); // 23 deg
		CHECK( o.yaw == 0 && !tg.Snapped() );
		tg.Update( true, true, { 0.5 * std::sin( 0.6 ), 1, 0.5 * std::cos( 0.6 ) }, { 0, 1, 0 }, o, snap ); // 34 deg
		CHECK( std::fabs( o.yaw - snap ) < 1e-12 && tg.Snapped() );
	}
	{
		// Gravity: a raised user falls and lands on their floor; on the floor nothing happens.
		Offset o;
		o.Translate( { 0, -1.0, 0 } ); // user 1 m up
		GravityMotion g;
		int steps = 0;
		while ( g.Step( o, 0.01, 9.8 ) && steps < 1000 )
			steps++;
		CHECK( o.t.y == 0 && steps > 40 && steps < 50 ); // ~0.45 s for 1 m
		CHECK( !g.Step( o, 0.01, 9.8 ) );
		g.Launch( { 0, -2, 0 } ); // thrown up from the floor
		CHECK( g.Step( o, 0.01, 9.8 ) && o.t.y < 0 );

		// Weightless: the throw drifts, slows down, stops in the air and is then left alone.
		Offset w;
		GravityMotion z;
		z.Launch( { 1, -1, 0 } );
		steps = 0;
		while ( z.Step( w, 0.01, 0 ) && steps < 2000 )
			steps++;
		CHECK( steps < 2000 && w.t.y < -0.5 && !z.Airborne( w, 0 ) && z.Airborne( w, 9.8 ) );
	}

	Offset t;
	TurnGesture turn;
	turn.Update( true, true, { 0, 1, 0.5 }, { 0, 1, 0 }, t );
	turn.Update( true, true, { 0.5, 1, 0 }, { 0, 1, 0 }, t ); // a quarter turn around the head
	CHECK( std::fabs( t.yaw - M_PI / 2 ) < 1e-12 );
	turn.Update( true, true, { 0.01, 1, 0 }, { 0, 1, 0 }, t ); // too close to the head: ignored
	CHECK( !turn.Active() );
}

static void TestConfig()
{
	const Settings s = ParseSettings( "snapDegrees=30\nleftHand=0\ndragGain=999\n# x=1\ngravityStrength=abc\ngravity=1\nturnEnabled=0\nturnRight=0\n" );
	CHECK( s.snapDegrees == 30 && s.dragGain == 10 && s.gravity && !s.turnEnabled && s.dragEnabled );
	CHECK( !s.dragLeft && !s.turnLeft && s.dragRight && !s.turnRight ); // the old leftHand covers both
	CHECK( s.gravityStrength == 9.8 ); // "abc" is not a number: the default
	CHECK( ParseSettings( "gravityStrength=0" ).gravityStrength == 0 && ParseSettings( "gravityStrength=99" ).gravityStrength == kGravityMax );
	const Settings r = ParseSettings( FormatSettings( s ) );
	CHECK( r.snapDegrees == s.snapDegrees && r.dragGain == s.dragGain && r.dragLeft == s.dragLeft && r.turnRight == s.turnRight &&
		   r.gravity == s.gravity && r.gravityStrength == s.gravityStrength && r.turnEnabled == s.turnEnabled );
}

static void TestVersion()
{
	int v[3];
	CHECK( ParseVersion( "v1.20.3", v ) && v[0] == 1 && v[1] == 20 && v[2] == 3 );
	CHECK( ParseVersion( "0.1.0-beta", v ) && v[2] == 0 );
	CHECK( !ParseVersion( "v1.2", v ) && !ParseVersion( "latest", v ) );
	CHECK( IsNewerVersion( "v0.2.0", "0.1.9" ) && IsNewerVersion( "v0.1.10", "0.1.9" ) && IsNewerVersion( "v1.0.0", "0.9.9" ) );
	CHECK( !IsNewerVersion( "v0.1.0", "0.1.0" ) && !IsNewerVersion( "v0.0.9", "0.1.0" ) && !IsNewerVersion( "garbage", "0.1.0" ) );
	CHECK( ReleaseTag( "{\"url\": \"x\", \"tag_name\": \"v0.3.1\", \"name\": \"y\"}" ) == "v0.3.1" );
	CHECK( ReleaseTag( "{\"message\": \"Not Found\"}" ).empty() );
	const Settings s = ParseSettings( "checkUpdates=0\nwelcomeDone=1\n" );
	CHECK( !s.checkUpdates && s.welcomeDone );
	const Settings r = ParseSettings( FormatSettings( s ) );
	CHECK( r.checkUpdates == s.checkUpdates && r.welcomeDone == s.welcomeDone );
}

int main()
{
	TestMath();
	TestGestures();
	TestConfig();
	TestVersion();
	printf( g_failures ? "%d FAILED\n" : "all tests passed\n", g_failures );
	return g_failures ? 1 : 0;
}
