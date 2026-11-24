#include "updater.h"

#include "core/files.h"
#include "core/log.h"
#include "core/version.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace
{
// Single quotes for the shell; paths with a single quote are refused before this is used.
std::string Q( const std::string &s )
{
	return "'" + s + "'";
}
} // namespace

Updater::~Updater()
{
	m_cancel = true;
	if ( const int pg = m_child; pg > 0 )
		kill( -pg, SIGTERM ); // curl, tar, ... of the running step
	Join();
}

std::string Updater::Run( const std::string &cmd, bool &ok )
{
	ok = false;
	std::string out;
	if ( m_cancel )
		return out;
	int fd[2];
	if ( pipe2( fd, O_CLOEXEC ) != 0 )
		return out;
	const pid_t pid = fork();
	if ( pid < 0 )
	{
		close( fd[0] );
		close( fd[1] );
		return out;
	}
	if ( pid == 0 )
	{
		setpgid( 0, 0 ); // its own group, so the whole pipeline can be stopped at once
		dup2( fd[1], STDOUT_FILENO );
		const int devnull = open( "/dev/null", O_RDONLY );
		if ( devnull >= 0 )
			dup2( devnull, STDIN_FILENO );
		execl( "/bin/sh", "sh", "-c", cmd.c_str(), (char *)nullptr );
		_exit( 127 );
	}
	setpgid( pid, pid ); // also from here: no race with a kill before the child ran setpgid
	m_child = pid;
	close( fd[1] );
	char buf[4096];
	ssize_t n;
	while ( ( n = read( fd[0], buf, sizeof buf ) ) != 0 )
	{
		if ( n < 0 )
		{
			if ( errno == EINTR )
				continue;
			break;
		}
		out.append( buf, (size_t)n );
	}
	close( fd[0] );
	int status = 0;
	while ( waitpid( pid, &status, 0 ) < 0 && errno == EINTR )
		;
	m_child = 0;
	ok = !m_cancel && WIFEXITED( status ) && WEXITSTATUS( status ) == 0;
	return out;
}

void Updater::Join()
{
	if ( m_worker.joinable() )
		m_worker.join();
}

std::string Updater::LatestTag() const
{
	std::lock_guard<std::mutex> lock( m_mutex );
	return m_tag;
}

void Updater::Check()
{
	const State s = m_state;
	if ( s == State::Checking || s == State::Applying || s == State::Installed )
		return;
	Join();
	m_state = State::Checking;
	m_worker = std::thread( &Updater::RunCheck, this );
}

void Updater::Apply()
{
	if ( m_state != State::Available )
		return;
	Join();
	m_state = State::Applying;
	m_worker = std::thread( &Updater::RunApply, this, LatestTag() );
}

void Updater::RunCheck()
{
	bool ok = false;
	// FSS_RELEASE_API / FSS_RELEASE_BASE point the updater elsewhere, for testing without publishing a release.
	const char *api = getenv( "FSS_RELEASE_API" );
	const std::string url = api && *api ? api : std::string( "https://api.github.com/repos/" ) + kRepo + "/releases/latest";
	if ( url.find( '\'' ) != std::string::npos )
	{
		m_state = State::CheckFailed;
		return;
	}
	const std::string json = Run( "curl -fsSL --max-time 20 -H 'Accept: application/vnd.github+json' " + Q( url ) + " 2>/dev/null", ok );
	const std::string tag = ok ? ReleaseTag( json ) : "";
	if ( tag.empty() )
	{
		logging::Printf( "update check: no answer from GitHub" );
		m_state = State::CheckFailed;
		return;
	}
	{
		std::lock_guard<std::mutex> lock( m_mutex );
		m_tag = tag;
	}
	const bool newer = IsNewerVersion( tag, FSS_VERSION );
	logging::Printf( "update check: latest release %s, this is %s%s", tag.c_str(), FSS_VERSION, newer ? " -> update available" : "" );
	m_state = newer ? State::Available : State::UpToDate;
}

void Updater::RunApply( std::string tag )
{
	auto fail = [&]( const char *why ) {
		logging::Printf( "update to %s failed: %s", tag.c_str(), why );
		m_state = State::Failed;
	};
	const std::string dir = files::StateDir() + "/update";
	if ( dir.find( '\'' ) != std::string::npos || tag.find_first_not_of( "vV0123456789.-abcdefghijklmnopqrstuvwxyz" ) != std::string::npos )
		return fail( "unexpected characters in a path or the tag" );
	const char *testBase = getenv( "FSS_RELEASE_BASE" );
	const std::string base = testBase && *testBase ? std::string( testBase ) + "/"
												   : std::string( "https://github.com/" ) + kRepo + "/releases/download/" + tag + "/";
	if ( base.find( '\'' ) != std::string::npos )
		return fail( "unexpected characters in the download address" );
	bool ok = false;
	Run( "rm -rf " + Q( dir ) + " && mkdir -p " + Q( dir ), ok );
	if ( !ok )
		return fail( "could not prepare the download folder" );
	Run( "curl -fsSL --max-time 300 -o " + Q( dir + "/" + kReleaseAsset ) + " " + Q( base + kReleaseAsset ) + " && curl -fsSL --max-time 60 -o " +
			 Q( dir + "/SHA256SUMS" ) + " " + Q( base + "SHA256SUMS" ),
		ok );
	if ( !ok )
		return fail( "download failed" );
	// The tarball must be listed in SHA256SUMS and match it.
	Run( "cd " + Q( dir ) + " && grep -q ' " + std::string( kReleaseAsset ) + "$' SHA256SUMS && sha256sum -c --ignore-missing --quiet SHA256SUMS",
		ok );
	if ( !ok )
		return fail( "checksum mismatch" );
	Run( "tar -xzf " + Q( dir + "/" + kReleaseAsset ) + " -C " + Q( dir ), ok );
	if ( !ok )
		return fail( "could not unpack the release" );
	if ( m_cancel )
		return fail( "quitting" ); // never start --install while the app goes away
	const std::string out = Run( Q( dir + "/frame-space-settings/frame-space-settings" ) + " --install 2>&1", ok );
	std::string said = out;
	for ( char &ch : said )
		if ( ch == '\n' )
			ch = ' ';
	logging::Printf( "update to %s: --install said: %s", tag.c_str(), said.c_str() );
	if ( !ok )
		return fail( "the new version's --install failed" );
	Run( "rm -rf " + Q( dir ), ok );
	m_state = State::Installed;
}
