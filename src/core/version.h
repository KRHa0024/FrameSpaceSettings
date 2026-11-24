#pragma once
// Release versions ("v1.2.3" tags) and their order. No OpenVR, tested without SteamVR.
#include <string>

#ifndef FSS_VERSION
#define FSS_VERSION "0.0.0" // set by CMake from project(VERSION)
#endif

// "v1.2.3" or "1.2.3" -> true and the three numbers; anything after them ("-beta") is ignored.
bool ParseVersion( const std::string &text, int out[3] );
// True when `candidate` is a later version than `current`; false when either cannot be read.
bool IsNewerVersion( const std::string &candidate, const std::string &current );
// The value of "tag_name" in a GitHub release JSON, or "".
std::string ReleaseTag( const std::string &json );
