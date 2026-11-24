#pragma once
// One-line event log at ~/.local/state/frame-space-settings/frame-space-settings.log (SteamVR starts us with stderr going
// nowhere). Capped: above kMaxBytes the file moves to .1, so the two never exceed twice that.

#include <string>

namespace logging
{
constexpr long kMaxBytes = 256 * 1024;
void Open( const std::string &path ); // without Open, lines go to stderr
#if defined( __GNUC__ )
__attribute__( ( format( printf, 1, 2 ) ) )
#endif
void Printf( const char *fmt, ... );
} // namespace logging
