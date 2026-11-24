#include "log.h"

#include <cstdarg>
#include <mutex>
#include <cstdio>
#include <ctime>
#include <sys/time.h>

namespace logging
{
namespace
{
FILE *g_file = nullptr;
std::string g_path;
std::mutex g_mutex; // the updater logs from its worker thread
} // namespace

void Open( const std::string &path )
{
	g_path = path;
	g_file = std::fopen( path.c_str(), "abe" ); // e: not inherited by children or across an update restart (execv)
}

void Printf( const char *fmt, ... )
{
	std::lock_guard<std::mutex> lock( g_mutex );
	if ( g_file && std::ftell( g_file ) > kMaxBytes )
	{
		std::fclose( g_file );
		std::rename( g_path.c_str(), ( g_path + ".1" ).c_str() );
		g_file = std::fopen( g_path.c_str(), "abe" );
	}
	FILE *out = g_file ? g_file : stderr;
	timeval tv;
	gettimeofday( &tv, nullptr );
	tm t;
	localtime_r( &tv.tv_sec, &t );
	char stamp[40];
	strftime( stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &t );
	std::fprintf( out, "%s.%03d ", stamp, (int)( tv.tv_usec / 1000 ) );
	va_list ap;
	va_start( ap, fmt );
	std::vfprintf( out, fmt, ap );
	va_end( ap );
	std::fputc( '\n', out );
	std::fflush( out );
}
} // namespace logging
