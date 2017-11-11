/*
  DaemonKeeper.hpp

  DaemonKeeper holds global stuffs for the daemon

*/

#ifndef __CLPKM__DAEMON_KEEPER_HPP__
#define __CLPKM__DAEMON_KEEPER_HPP__



#include <cerrno>
#include <cstdio>
#include <syslog.h>
#include <unistd.h>



namespace CLPKM {

// Main class
class DaemonKeeper {
public:
	enum loglevel : int {
		FATAL = LOG_CRIT,
		ERROR = LOG_ERR,
		INFO  = LOG_INFO,
		DEBUG = LOG_DEBUG,
		NUM_OF_LOGLEVEL
		};

	enum run_mode {
		DAEMON,
		TERMINAL
	};

	bool shouldLog(loglevel Level) const {
		return (LogLevel >= Level);
		}

	template <class ... T>
	void Log(T&& ... FormatStr) {
		if (RunMode == DAEMON)
			syslog(LOG_INFO, FormatStr...);
		else
			fprintf(stderr, FormatStr...);
		}

	template <class ... T>
	void Log(loglevel Level, T&& ... FormatStr) {
		if (!shouldLog(Level))
			return;
		if (RunMode == DAEMON)
			syslog(Level, FormatStr...);
		else
			fprintf(stderr, FormatStr...);
		}

	int Daemonize() noexcept;

private:
	DaemonKeeper(const DaemonKeeper& ) = delete;
	DaemonKeeper& operator=(const DaemonKeeper& ) = delete;

	DaemonKeeper() : LogLevel(DEBUG), RunMode(TERMINAL) { }

	//
	loglevel LogLevel;
	run_mode RunMode;

	friend DaemonKeeper& getDaemonKeeper(void);

	};

DaemonKeeper& getDaemonKeeper(void);

}



#endif
