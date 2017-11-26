/*
  DaemonKeeper.hpp

  DaemonKeeper holds global stuffs for the daemon

*/

#ifndef __CLPKM__DAEMON_KEEPER_HPP__
#define __CLPKM__DAEMON_KEEPER_HPP__



#include <cerrno>
#include <cstdio>
#include <ctime>
#include <syslog.h>
#include <unistd.h>



namespace CLPKM {

// Main class
class DaemonKeeper {
public:
	enum class loglevel : int {
		FATAL = LOG_CRIT,
		ERROR = LOG_ERR,
		INFO  = LOG_INFO,
		DEBUG = LOG_DEBUG,
		NUM_OF_LOGLEVEL
		};

	enum class run_mode {
		DAEMON,
		TERMINAL
	};

	bool shouldLog(loglevel Level) const {
		return (LogLevel >= Level);
		}

	template <class ... T>
	void Log(T&& ... FormatStr) {
		if (RunMode == run_mode::DAEMON)
			syslog(LOG_INFO, FormatStr...);
		else {
			UpdateTimeString();
			fprintf(stderr, "[%s] ", TimeStrBuffer);
			fprintf(stderr, FormatStr...);
			}
		}

	template <class ... T>
	void Log(loglevel Level, T&& ... FormatStr) {
		if (!shouldLog(Level))
			return;
		if (RunMode == run_mode::DAEMON)
			syslog(static_cast<int>(Level), FormatStr...);
		else {
			UpdateTimeString();
			fprintf(stderr, "[%s] ", TimeStrBuffer);
			fprintf(stderr, FormatStr...);
			}
		}

	int Daemonize() noexcept;

private:
	DaemonKeeper(const DaemonKeeper& ) = delete;
	DaemonKeeper& operator=(const DaemonKeeper& ) = delete;

	DaemonKeeper() : LogLevel(loglevel::DEBUG), RunMode(run_mode::TERMINAL) { }

	void UpdateTimeString() noexcept;

	//
	loglevel LogLevel;
	run_mode RunMode;

	time_t Time = 0;
	char TimeStrBuffer[128] = {};

	friend DaemonKeeper& getDaemonKeeper(void);

	};

DaemonKeeper& getDaemonKeeper(void);

}



#endif
