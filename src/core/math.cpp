#include "math.h"

Mat34 Mul( const Mat34 &a, const Mat34 &b )
{
	Mat34 r;
	for ( int i = 0; i < 3; i++ )
	{
		for ( int j = 0; j < 4; j++ )
		{
			double v = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
			if ( j == 3 )
				v += a.m[i][3];
			r.m[i][j] = v;
		}
	}
	return r;
}

Mat34 Inverse( const Mat34 &a )
{
	Mat34 r;
	for ( int i = 0; i < 3; i++ )
		for ( int j = 0; j < 3; j++ )
			r.m[i][j] = a.m[j][i];
	for ( int i = 0; i < 3; i++ )
		r.m[i][3] = -( r.m[i][0] * a.m[0][3] + r.m[i][1] * a.m[1][3] + r.m[i][2] * a.m[2][3] );
	return r;
}

Mat34 YawTranslate( double yaw, Vec3 t )
{
	const double c = std::cos( yaw ), s = std::sin( yaw );
	Mat34 r;
	r.m[0][0] = c;
	r.m[0][2] = s;
	r.m[2][0] = -s;
	r.m[2][2] = c;
	r.m[0][3] = t.x;
	r.m[1][3] = t.y;
	r.m[2][3] = t.z;
	return r;
}

double YawOf( const Mat34 &a )
{
	return std::atan2( a.m[0][2], a.m[0][0] );
}

double HeadingOf( const Mat34 &a )
{
	// forward f = -Z column, up u = Y column. Looking down, the top of the head tips forward (and back when looking up),
	// so f - u * f.y stays horizontal-forward at every pitch.
	const double fx = -a.m[0][2], fy = -a.m[1][2], fz = -a.m[2][2];
	const double ux = a.m[0][1], uz = a.m[2][1];
	return std::atan2( fx - ux * fy, fz - uz * fy );
}

Vec3 RotateY( Vec3 v, double yaw )
{
	const double c = std::cos( yaw ), s = std::sin( yaw );
	return { c * v.x + s * v.z, v.y, -s * v.x + c * v.z };
}

double WrapAngle( double a )
{
	a = std::fmod( a + M_PI, 2 * M_PI );
	if ( a < 0 )
		a += 2 * M_PI;
	return a - M_PI;
}

double RotationAngle( const Mat34 &a, const Mat34 &b )
{
	// ||Ra - Rb||_F^2 = 8 sin^2(angle / 2): well conditioned for tiny angles, unlike acos of the trace.
	double e = 0;
	for ( int i = 0; i < 3; i++ )
		for ( int j = 0; j < 3; j++ )
			e += ( a.m[i][j] - b.m[i][j] ) * ( a.m[i][j] - b.m[i][j] );
	const double s = std::sqrt( e / 8 );
	return 2 * std::asin( s > 1 ? 1 : s );
}

bool NearPose( const Mat34 &a, const Mat34 &b, double posTol, double rotTol )
{
	if ( !Finite( a ) || !Finite( b ) )
		return false;
	const Vec3 d = a.T() - b.T();
	return d.x * d.x + d.y * d.y + d.z * d.z <= posTol * posTol && RotationAngle( a, b ) <= rotTol;
}

bool Finite( const Mat34 &a )
{
	for ( const auto &row : a.m )
		for ( double v : row )
			if ( !std::isfinite( v ) )
				return false;
	return true;
}

bool InRange( const Mat34 &z )
{
	if ( !Finite( z ) )
		return false;
	for ( int i = 0; i < 3; i++ )
		if ( std::fabs( z.m[i][3] ) > kMaxZeroPoseOffset )
			return false;
	return true;
}

bool OnlyHeightDiffers( const Mat34 &a, const Mat34 &b )
{
	Mat34 level = b;
	level.m[1][3] = a.m[1][3];
	return NearPose( a, level, 0.005, 0.2 * M_PI / 180 ) && std::fabs( a.m[1][3] - b.m[1][3] ) > 0.005;
}
