#include "files.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

namespace files
{
std::string Home()
{
	if ( const char *h = getenv( "HOME" ); h && *h )
		return h;
	if ( const passwd *pw = getpwuid( getuid() ) )
		return pw->pw_dir;
	return "/tmp";
}

// Deliberately not XDG_*_HOME: SteamVR, a desktop terminal and Flatpak launchers on the Frame see different values, and
// settings must be found by every copy of the program.
std::string ConfigDir() { return Home() + "/.config/frame-space-settings"; }
std::string StateDir() { return Home() + "/.local/state/frame-space-settings"; }
std::string DataDir() { return Home() + "/.local/share/frame-space-settings"; }

bool MakeDirs( const std::string &dir )
{
	for ( size_t i = 1; i <= dir.size(); i++ )
	{
		if ( i == dir.size() || dir[i] == '/' )
		{
			const std::string part = dir.substr( 0, i );
			if ( mkdir( part.c_str(), 0755 ) != 0 && errno != EEXIST )
				return false;
		}
	}
	return true;
}

bool ReadFile( const std::string &path, std::string &out )
{
	FILE *f = std::fopen( path.c_str(), "rb" );
	if ( !f )
		return false;
	out.clear();
	char buf[8192];
	size_t n;
	while ( ( n = std::fread( buf, 1, sizeof buf, f ) ) > 0 )
		out.append( buf, n );
	const bool ok = !std::ferror( f );
	std::fclose( f );
	return ok;
}

bool WriteFileAtomic( const std::string &path, const std::string &data )
{
	const size_t slash = path.rfind( '/' );
	const std::string dir = slash == std::string::npos ? "." : path.substr( 0, slash );
	if ( !MakeDirs( dir ) )
		return false;
	const std::string tmp = path + ".tmp";
	const int fd = open( tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644 );
	if ( fd < 0 )
		return false;
	size_t done = 0;
	while ( done < data.size() )
	{
		const ssize_t w = write( fd, data.data() + done, data.size() - done );
		if ( w < 0 && errno == EINTR )
			continue;
		if ( w <= 0 )
		{
			close( fd );
			unlink( tmp.c_str() );
			return false;
		}
		done += (size_t)w;
	}
	if ( fsync( fd ) != 0 || close( fd ) != 0 || rename( tmp.c_str(), path.c_str() ) != 0 )
	{
		unlink( tmp.c_str() );
		return false;
	}
	if ( const int dfd = open( dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC ); dfd >= 0 )
	{
		fsync( dfd );
		close( dfd );
	}
	return true;
}

bool Exists( const std::string &path )
{
	return access( path.c_str(), F_OK ) == 0;
}
} // namespace files
