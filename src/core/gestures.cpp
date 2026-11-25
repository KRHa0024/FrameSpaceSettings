#include "gestures.h"

bool DragGesture::Update( bool held, bool valid, Vec3 hand, double gain, Offset &off, double now, AxisLock lock, double heading )
{
	if ( !held )
		m_hasHeading = false; // the next press takes the head's heading again
	else if ( !m_hasHeading && std::isfinite( heading ) )
	{
		m_heading = heading;
		m_hasHeading = true;
	}
	if ( !held || !valid )
	{
		if ( m_active && !held )
			m_released = true; // let go on purpose: the momentum counts
		else if ( m_active )
			m_vel = {};
		m_active = false;
		return false;
	}
	bool changed = false;
	if ( m_active )
	{
		Vec3 d = ( hand - m_last ) * gain;
		if ( lock.Any() )
		{
			// In the frame the head faced: x across, z forward. A locked axis takes none of the hand's movement, and so none
			// of the momentum either.
			Vec3 l = RotateY( d, -m_heading );
			if ( lock.x )
				l.x = 0;
			if ( lock.y )
				l.y = 0;
			if ( lock.z )
				l.z = 0;
			d = RotateY( l, m_heading );
		}
		const double dt = now - m_lastTime;
		if ( dt > 1e-4 && dt < 0.2 )
			m_vel = m_vel * 0.6 + d * ( 0.4 / dt ); // smoothed over the last few frames
		if ( d.x != 0 || d.y != 0 || d.z != 0 )
		{
			off.Translate( d );
			changed = true;
		}
	}
	else
		m_vel = {};
	m_active = true;
	m_released = false;
	m_last = hand;
	m_lastTime = now;
	return changed;
}

bool DragGesture::TakeRelease( Vec3 &worldVelocity )
{
	if ( !m_released )
		return false;
	m_released = false;
	worldVelocity = m_vel;
	return true;
}

bool GravityMotion::Airborne( const Offset &off, double g ) const
{
	// The user's height above their own floor is -t.y (moving the world down raises the user).
	return m_vel.x != 0 || m_vel.y != 0 || m_vel.z != 0 || ( g > 0 && -off.t.y > 1e-4 );
}

bool GravityMotion::Step( Offset &off, double dt, double g, bool holdHeight )
{
	if ( holdHeight )
	{
		m_vel.y = 0;
		g = 0;
	}
	const double height = -off.t.y;
	if ( height <= 1e-4 && m_vel.y >= 0 )
	{
		m_vel = {}; // standing on (or below) the floor and not thrown upwards
		return false;
	}
	m_vel.y += g * dt; // the user falls = the world rises
	m_vel = m_vel * std::exp( -kFriction * dt );
	if ( g <= 0 && std::sqrt( m_vel.x * m_vel.x + m_vel.y * m_vel.y + m_vel.z * m_vel.z ) < 0.005 )
	{
		m_vel = {}; // weightless and nearly still: stop where it is
		return false;
	}
	off.Translate( m_vel * dt );
	if ( height > 0 && -off.t.y <= 0 )
	{
		off.t.y = 0; // landed on the floor
		m_vel = {};
	}
	return true;
}

bool TurnGesture::Update( bool held, bool valid, Vec3 hand, Vec3 head, Offset &off, double snapRad )
{
	m_snapped = false;
	const Vec3 rel = hand - head;
	if ( !held || !valid || std::hypot( rel.x, rel.z ) < kMinRadius )
	{
		if ( !held )
			m_accum = 0; // a new grab starts a new swing
		m_active = false;
		return false;
	}
	const double heading = std::atan2( rel.x, rel.z );
	bool changed = false;
	if ( m_active )
	{
		const double step = WrapAngle( heading - m_last );
		if ( step != 0 && std::fabs( step ) <= kMaxStep )
		{
			if ( snapRad <= 0 )
			{
				off.RotateAbout( head, step );
				changed = true;
			}
			else
			{
				m_accum += step;
				while ( std::fabs( m_accum ) >= snapRad )
				{
					const double s = m_accum > 0 ? snapRad : -snapRad;
					off.RotateAbout( head, s );
					m_accum -= s;
					changed = m_snapped = true;
				}
			}
		}
	}
	m_active = true;
	m_last = heading;
	return changed;
}
