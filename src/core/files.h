#pragma once
#include <string>

namespace files
{
std::string Home();
std::string ConfigDir(); // ~/.config/frame-space-settings
std::string StateDir();  // ~/.local/state/frame-space-settings (log, lock, the driver's crash marker)
std::string DataDir();   // ~/.local/share/frame-space-settings (installed manifests)
bool MakeDirs( const std::string &dir );
bool ReadFile( const std::string &path, std::string &out );
// Writes to a temporary file, fsyncs it, renames it over `path` and fsyncs the directory: after a crash or power loss
// the file is either the old or the new content, never half of each.
bool WriteFileAtomic( const std::string &path, const std::string &data );
bool Exists( const std::string &path );
} // namespace files
