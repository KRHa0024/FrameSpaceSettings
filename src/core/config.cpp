#include "config.h"
#include "files.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace
{
std::string Trim( const std::string &s )
{
	const size_t a = s.find_first_not_of( " \t\r" ), b = s.find_last_not_of( " \t\r" );
	return a == std::string::npos ? "" : s.substr( a, b - a + 1 );
}

double Clamp( double v, double lo, double hi, double fallback )
{
	return v == v ? std::min( hi, std::max( lo, v ) ) : fallback;
}
} // namespace

Settings ParseSettings( const std::string &ini )
{
	Settings s;
	const Settings d;
	std::istringstream in( ini );
	std::string line;
	while ( std::getline( in, line ) )
	{
		const size_t eq = line.find( '=' );
		if ( line.empty() || line[0] == '#' || line[0] == ';' || eq == std::string::npos )
			continue;
		const std::string k = Trim( line.substr( 0, eq ) ), v = Trim( line.substr( eq + 1 ) );
		char *end = nullptr;
		double num = strtod( v.c_str(), &end );
		if ( end == v.c_str() )
			num = NAN; // not a number: Clamp falls back to the default
		const bool on = v == "1" || v == "true" || v == "on";
		if ( k == "dragGain" ) s.dragGain = Clamp( num, 0.1, 10, d.dragGain );
		else if ( k == "snapDegrees" ) s.snapDegrees = Clamp( num, 1, 180, d.snapDegrees );
		else if ( k == "dragEnabled" ) s.dragEnabled = on;
		else if ( k == "turnEnabled" ) s.turnEnabled = on;
		else if ( k == "snapTurn" ) s.snapTurn = on;
		else if ( k == "gravity" ) s.gravity = on;
		else if ( k == "gravityStrength" ) s.gravityStrength = Clamp( num, 0, kGravityMax, d.gravityStrength );
		else if ( k == "dragLeft" ) s.dragLeft = on;
		else if ( k == "dragRight" ) s.dragRight = on;
		else if ( k == "turnLeft" ) s.turnLeft = on;
		else if ( k == "turnRight" ) s.turnRight = on;
		else if ( k == "leftHand" ) s.dragLeft = s.turnLeft = on; // older files: one switch per hand for both
		else if ( k == "rightHand" ) s.dragRight = s.turnRight = on;
		else if ( k == "haptics" ) s.haptics = on;
		else if ( k == "resetEnabled" || k == "resetHold" ) s.resetEnabled = on; // resetHold: older files
		else if ( k == "checkUpdates" ) s.checkUpdates = on;
		else if ( k == "welcomeDone" ) s.welcomeDone = on;
		else if ( k == "language" ) s.language = ( v == "ja" || v == "en" ) ? v : "auto";
	}
	return s;
}

std::string FormatSettings( const Settings &s )
{
	char buf[768];
	snprintf( buf, sizeof buf,
		"# Frame Space Settings settings\n"
		"dragEnabled=%d\nturnEnabled=%d\ndragGain=%g\nsnapTurn=%d\nsnapDegrees=%g\ngravity=%d\ngravityStrength=%g\n"
		"dragLeft=%d\ndragRight=%d\nturnLeft=%d\nturnRight=%d\nhaptics=%d\nresetEnabled=%d\ncheckUpdates=%d\nwelcomeDone=%d\nlanguage=%s\n",
		s.dragEnabled, s.turnEnabled, s.dragGain, s.snapTurn, s.snapDegrees, s.gravity, s.gravityStrength, s.dragLeft, s.dragRight,
		s.turnLeft, s.turnRight, s.haptics, s.resetEnabled, s.checkUpdates, s.welcomeDone, s.language.c_str() );
	return buf;
}

Settings LoadSettings()
{
	const std::string path = files::ConfigDir() + "/config.ini";
	std::string text;
	if ( files::ReadFile( path, text ) )
		return ParseSettings( text );
	const Settings s;
	files::WriteFileAtomic( path, FormatSettings( s ) );
	return s;
}

bool SaveSettings( const Settings &s )
{
	return files::WriteFileAtomic( files::ConfigDir() + "/config.ini", FormatSettings( s ) );
}
