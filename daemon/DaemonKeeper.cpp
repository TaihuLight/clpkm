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

void DaemonKeeper::UpdateTimeString() noexcept {

	time_t Now = time(nullptr);

	if (Time == Now)
		return;

	tm LocalTime;

	localtime_r(&Now, &LocalTime);
	strftime(TimeStrBuffer, sizeof(TimeStrBuffer), "%F %r", &LocalTime);

	Time = Now;

	}



DaemonKeeper& CLPKM::getDaemonKeeper(void) {
	static DaemonKeeper D;
	return D;
	}
