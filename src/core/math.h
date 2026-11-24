#pragma once
// Rigid transforms in OpenVR's raw tracking space (right handed, +Y up, gravity aligned). No OpenVR types here, so
// everything in core/ builds and tests without SteamVR.

#include <cmath>

struct Vec3
{
	double x = 0, y = 0, z = 0;
};

inline Vec3 operator+( Vec3 a, Vec3 b ) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline Vec3 operator-( Vec3 a, Vec3 b ) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline Vec3 operator*( Vec3 a, double s ) { return { a.x * s, a.y * s, a.z * s }; }

// Row-major 3x4, same layout as vr::HmdMatrix34_t.
struct Mat34
{
	double m[3][4] = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 } };
	Vec3 T() const { return { m[0][3], m[1][3], m[2][3] }; }
};

Mat34 Mul( const Mat34 &a, const Mat34 &b );
// Inverse of a rigid transform (rotation transposed).
Mat34 Inverse( const Mat34 &a );
// Rotation about +Y by `yaw` radians, then translation `t`. Positive yaw increases atan2(x, z).
Mat34 YawTranslate( double yaw, Vec3 t );
double YawOf( const Mat34 &a );
Vec3 RotateY( Vec3 v, double yaw );
double WrapAngle( double a );

// Rotation angle of a^-1 * b, in radians.
double RotationAngle( const Mat34 &a, const Mat34 &b );
bool NearPose( const Mat34 &a, const Mat34 &b, double posTol, double rotTol );
bool Finite( const Mat34 &a );
// Same x, z and rotation (within 5 mm / 0.2 degrees) but a different height.
bool OnlyHeightDiffers( const Mat34 &a, const Mat34 &b );

// The world move: W(p) = RotateY(p, yaw) + t, applied in raw space on the left of a zero pose.
struct Offset
{
	double yaw = 0; // radians, unbounded
	Vec3 t;

	Mat34 Matrix() const { return YawTranslate( yaw, t ); }
	void Translate( Vec3 d ) { t = t + d; }
	// Turns the world about the vertical axis through `pivot` (raw space).
	void RotateAbout( Vec3 pivot, double angle )
	{
		t = RotateY( t - pivot, angle ) + pivot;
		yaw += angle;
	}
	bool IsZero() const { return yaw == 0 && t.x == 0 && t.y == 0 && t.z == 0; }
	bool operator==( const Offset &o ) const { return yaw == o.yaw && t.x == o.t.x && t.y == o.t.y && t.z == o.t.z; }
	bool operator!=( const Offset &o ) const { return !( *this == o ); }
	// The yaw + translation part of a gravity aligned transform.
	static Offset FromMatrix( const Mat34 &w ) { return { YawOf( w ), w.T() }; }
};

// Largest translation OpenVR accepts in a zero pose (OVR Advanced Settings uses the same limit). Used as the sanity
// limit for the move.
constexpr double kMaxZeroPoseOffset = 39900.0;
bool InRange( const Mat34 &zeroPose );
