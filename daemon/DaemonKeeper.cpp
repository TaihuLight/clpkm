/*
  DaemonKeeper.cpp

  Impl functionality of DaemonKeeper

*/

#include "DaemonKeeper.hpp"

using namespace CLPKM;



int DaemonKeeper::Daemonize() noexcept {
	if (daemon(0, 0) != 0)
		return errno;
	openlog("clpkm-sched-srv", LOG_PID, LOG_DAEMON);
	RunMode = run_mode::DAEMON;
	return 0;
	}

DaemonKeeper& CLPKM::getDaemonKeeper(void) {
	static DaemonKeeper D;
	return D;
	}
