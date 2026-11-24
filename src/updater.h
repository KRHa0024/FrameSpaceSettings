#pragma once
// Updates from the GitHub releases of the project. The check asks GitHub for the latest release; applying downloads its
// tarball and SHA256SUMS, checks the checksum, unpacks it and runs the new program's --install. --install replaces the
// driver and data first and the program last, each file by an atomic rename, so a failure leaves the old program in
// place. The app then starts the new program in its place; a new driver takes effect after the next SteamVR restart.
//
// Quitting cancels the work: the commands run in a process group of their own that the destructor terminates, and
// --install is never started once quitting began.
//
// curl, sha256sum and tar from the system do the network and file work (all part of SteamOS), on a worker thread.

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

constexpr const char *kRepo = "KRHa0024/FrameSpaceSettings";
constexpr const char *kReleaseAsset = "frame-space-settings-linux-arm64.tar.gz";

class Updater
{
public:
	enum class State
	{
		Idle,
		Checking,
		UpToDate,
		Available, // a newer release exists: LatestTag()
		Applying,
		Installed, // the new version is installed: restart the program to use it
		Failed,      // applying failed: shown with "check again"
		CheckFailed, // no answer from GitHub (offline, no release yet): silent, tried again later
	};
	~Updater();
	void Check();  // no-op while busy
	void Apply();  // only from Available
	State GetState() const { return m_state; }
	std::string LatestTag() const;

private:
	void Join();
	void RunCheck();
	void RunApply( std::string tag );
	// Runs a shell command (cancellable); returns its standard output, `ok` = exited with 0.
	std::string Run( const std::string &cmd, bool &ok );
	std::atomic<State> m_state{ State::Idle };
	std::atomic<bool> m_cancel{ false };
	std::atomic<int> m_child{ 0 }; // process group of the running command, 0 when none
	mutable std::mutex m_mutex;
	std::string m_tag;
	std::thread m_worker;
};
