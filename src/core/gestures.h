#pragma once
#include "math.h"

// Axes space drag does not move along. Y is always up; X (left / right) and Z (forward / back) follow the way the head faced
// when the button was pressed, so they mean the same thing whichever way the user has turned.
struct AxisLock
{
	bool x = false, y = false, z = false;
	bool Any() const { return x || y || z; }
};

// Space drag: while held, the world follows the hand. Hand positions are raw space before the move, so moving the world
// never feeds back into the measurement.
class DragGesture
{
public:
	// Returns true when it changed `off`. An invalid pose pauses and re-anchors on the next valid one (no jump).
	// `heading`: the head's HeadingOf in the same space as `hand`, NAN when unknown. It is taken once per press, the first
	// time it is known; until then X and Z are the raw space's own.
	bool Update( bool held, bool valid, Vec3 hand, double gain, Offset &off, double now, AxisLock lock = {}, double heading = NAN );
	bool Active() const { return m_active; }
	// After the button is let go (not after losing tracking): the world's velocity at that moment, once.
	bool TakeRelease( Vec3 &worldVelocity );

private:
	bool m_active = false, m_released = false;
	bool m_hasHeading = false; // the heading of this press is taken
	double m_heading = 0;
	Vec3 m_last, m_vel;
	double m_lastTime = 0;
};

// Space turn: the world turns about the head by the change of the hand's heading around the head. With `snapRad` > 0 the
// same gesture turns in steps of that angle, once the hand has swung that far.
class TurnGesture
{
public:
	bool Update( bool held, bool valid, Vec3 hand, Vec3 head, Offset &off, double snapRad = 0 );
	bool Active() const { return m_active; }
	bool Snapped() const { return m_snapped; } // the last Update turned one snap step

	static constexpr double kMinRadius = 0.08;   // m: closer to the head than this the heading is noise
	static constexpr double kMaxStep = M_PI / 2; // a larger jump between two samples is a tracking glitch

private:
	bool m_active = false, m_snapped = false;
	double m_last = 0, m_accum = 0;
};

// Gravity (as in OpenVR Advanced Settings): after a space drag is let go the move keeps the hand's momentum, falls with
// gravity and lands on the user's own floor (no height offset). Below that floor nothing falls. With g = 0 (weightless)
// the throw drifts, slows down and stays where it stopped.
class GravityMotion
{
public:
	static constexpr double kFriction = 1.0; // 1/s: momentum fades while in the air
	void Launch( Vec3 worldVelocity ) { m_vel = worldVelocity; }
	void Stop() { m_vel = {}; }
	// Advances by dt; true when it changed `off`. g in m/s^2. With `holdHeight` (Y locked) it does not fall or rise: the
	// throw only drifts sideways and slows down, as when weightless.
	bool Step( Offset &off, double dt, double g, bool holdHeight = false );
	// Still moving, or above the floor with gravity to pull it down.
	bool Airborne( const Offset &off, double g ) const;

private:
	Vec3 m_vel; // of the world; the user moves the opposite way
};
