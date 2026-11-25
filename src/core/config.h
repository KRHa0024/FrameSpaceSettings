#pragma once
#include <string>

constexpr double kGravityMax = 50; // m/s^2

struct Settings
{
	bool dragEnabled = true;   // space drag
	bool turnEnabled = true;   // space turn
	double dragGain = 1.0;     // world movement per meter of hand movement
	bool snapTurn = false;     // space turn turns in steps of snapDegrees instead of smoothly
	double snapDegrees = 45.0;
	bool gravity = false;      // momentum after a drag, falling back to the floor
	double gravityStrength = 9.8; // m/s^2, 0 = weightless (the throw drifts and slows down)
	bool dragLeft = true, dragRight = true; // which hands space drag listens to
	bool lockX = false, lockY = false, lockZ = false; // space drag does not move along these (X/Z: as the head faced)
	bool turnLeft = true, turnRight = true; // which hands space turn listens to
	bool haptics = true;
	bool checkUpdates = true;  // ask GitHub for a newer release now and then
	bool welcomeDone = false;  // the "ready" notice after the first start with the driver was dismissed
	bool resetEnabled = true;  // the reset action (how it is pressed is up to the binding) goes back to the original
	std::string language = "auto"; // auto, ja, en
};

Settings ParseSettings( const std::string &ini );
std::string FormatSettings( const Settings &s );
Settings LoadSettings(); // ~/.config/frame-space-settings/config.ini, written with defaults when missing
bool SaveSettings( const Settings &s );
