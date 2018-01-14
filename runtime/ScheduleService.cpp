/*
  ScheduleService.cpp

  Impl schedule service

*/

#include "ErrorHandling.hpp"
#include "ScheduleService.hpp"
#include "LookupVendorImpl.hpp"

#include <cstdlib>
#include <cstring>

#include <sys/eventfd.h>
#include <sys/poll.h>
#include <sys/timerfd.h>

using namespace CLPKM;



namespace {

itimerspec GenOneTimeTimerSpec(time_t Sec, long NanoSec) {
	itimerspec Spec;
	Spec.it_interval.tv_sec = 0;
	Spec.it_interval.tv_nsec = 0;
	Spec.it_value.tv_sec = Sec;
	Spec.it_value.tv_nsec = NanoSec;
	return Spec;
	}

// Watcher for low priority tasks
int RunLevelChangeWatcher(sd_bus_message* Msg, void* UserData,
                          sd_bus_error* ErrorRet) {

	(void) ErrorRet;

	task_bitmap& Bitmap = *reinterpret_cast<task_bitmap*>(UserData);

	int Ret = sd_bus_message_read(Msg, TASK_BITMAP_DBUS_TYPE_CODE, &Bitmap);
	INTER_ASSERT(Ret >= 0,
	             "failed to read message from bus: %s", StrError(-Ret).c_str());

	return 1;

	}

int DaemonNameOwnerChangeWatcher(sd_bus_message* Msg, void* UserData,
                                 sd_bus_error* ErrorRet) {

	(void) UserData;
	(void) ErrorRet;

	const char* Name = nullptr;
	const char* OldOwner = nullptr;
	const char* NewOwner = nullptr;

	int Ret = sd_bus_message_read(Msg, "sss", &Name, &OldOwner, &NewOwner);

	INTER_ASSERT(Ret >= 0, "failed to read message: %s", StrError(-Ret).c_str());
	INTER_ASSERT(NewOwner[0] != '\0', "the daemon has disconnected!");

	return 1;

	}

} // namespace



void ScheduleService::SchedGuard::BindToEvent(cl_event E,
                                              bool GottaReleaseEvent) {

	if (E == NULL)
		return;

	intptr_t Data = static_cast<intptr_t>(Kind);
	Kind = task_kind::NUM_OF_TASK_KIND;

	if (GottaReleaseEvent)
		Data = -Data;

	cl_int Ret = Lookup<OclAPI::clSetEventCallback>()(
			E, CL_COMPLETE, SchedEndOnEventCallback, reinterpret_cast<void*>(Data));
	INTER_ASSERT(Ret == CL_SUCCESS, "failed to set event callback!");

	}

void CL_CALLBACK ScheduleService::SchedGuard::SchedEndOnEventCallback(
		cl_event Event, cl_int Status, void* UserData) {
	(void) Status;
	intptr_t Kind = reinterpret_cast<intptr_t>(UserData);
	if (Kind < 0) {
		Kind = -Kind;
		Lookup<OclAPI::clReleaseEvent>()(Event);
		}
	getScheduleService().SchedEnd(static_cast<task_kind>(Kind));
	}



// FIXME: change defaults to system bus
ScheduleService::ScheduleService()
: TermEventFd(-1), IsOnSystemBus(false), Priority(priority::LOW),
  Bus(nullptr), Threshold(0), Bitmap(0) {

	if (const char* Fine = getenv("CLPKM_PRIORITY")) {
		if (!strcmp(Fine, "high"))
			Priority = priority::HIGH;
		else if (strcmp(Fine, "low")) {
			getRuntimeKeeper().Log("==CLPKM== Unrecognised priority: \"%s\"\n",
			                       Fine);
			}
		}

	TermEventFd = eventfd(0, EFD_CLOEXEC);
	INTER_ASSERT(TermEventFd >= 0, "eventfd failed: %s", StrError(errno).c_str());

	StartBus();

	}

ScheduleService::~ScheduleService() {
	this->Terminate();
	}



void ScheduleService::Terminate() {

	// Send termination
	uint64_t Termination = 1;
	int Ret = write(TermEventFd, &Termination, sizeof(Termination));
	INTER_ASSERT(Ret > 0, "write to eventfd failed: %s", StrError(errno).c_str());

	if (IPCWorker.joinable())
		IPCWorker.join();

	// Clean up bus and reset to NULL
	Bus = sd_bus_flush_close_unref(Bus);

	// Release timers and and set to -1
	if (auto* HighPrioTask = std::get_if<high_prio_task>(&Task)) {
		int* TimerFd = HighPrioTask->TimerFd;
		for (size_t Kind = 0; Kind < NumOfTaskKind; ++Kind) {
			close(TimerFd[Kind]);
			TimerFd[Kind] = -1;
			}
		}

	// Release termination event
	close(TermEventFd);
	TermEventFd = -1;

	}



void ScheduleService::StartBus() {

	int Ret = 0;

	if (IsOnSystemBus)
		Ret = sd_bus_open_system(&Bus);
	else
		Ret = sd_bus_open_user(&Bus);
	INTER_ASSERT(Ret >= 0, "failed to open bus: %s", StrError(-Ret).c_str());

	// Only low priority tasks need to get config
	if (Priority != priority::LOW) {

		auto& TaskData = Task.emplace<high_prio_task>();

		// Create timers for each task kind to notify the worker
		for (size_t Kind = 0; Kind < NumOfTaskKind; ++Kind) {
			int TimerFd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
			INTER_ASSERT(TimerFd >= 0, "timerfd_create failed: %s",
			             StrError(errno).c_str());
			TaskData.TimerFd[Kind] = TimerFd;
			}

		IPCWorker = std::thread(&ScheduleService::HighPrioProcWorker, this);
		return;

		}

	Ret = sd_bus_add_match(
			Bus, nullptr,
			"type='signal',"
			"sender='edu.nctu.sslab.CLPKMSchedSrv',"
			"interface='edu.nctu.sslab.CLPKMSchedSrv',"
			"member='RunLevelChanged'",
			RunLevelChangeWatcher, &Bitmap);
	INTER_ASSERT(Ret >= 0, "failed to add match: %s", StrError(-Ret).c_str());

	Ret = sd_bus_add_match(
			Bus, nullptr,
			"type='signal',"
			"sender='org.freedesktop.DBus',"
			"interface='org.freedesktop.DBus',"
			"member='NameOwnerChanged',"
			"arg0='edu.nctu.sslab.CLPKMSchedSrv'",
			DaemonNameOwnerChangeWatcher, nullptr);
	INTER_ASSERT(Ret >= 0, "failed to add match: %s", StrError(-Ret).c_str());

	sd_bus_error    BusError = SD_BUS_ERROR_NULL;
	sd_bus_message* Msg = nullptr;

	Ret = sd_bus_call_method(
			Bus,
			"edu.nctu.sslab.CLPKMSchedSrv",  // service
			"/edu/nctu/sslab/CLPKMSchedSrv", // object path
			"edu.nctu.sslab.CLPKMSchedSrv",  // interface
			"GetConfig",                     // method name
			&BusError, &Msg, "");
	INTER_ASSERT(Ret >= 0, "call method failed: %s", StrError(-Ret).c_str());

	const char* Path = nullptr;

	Ret = sd_bus_message_read(Msg, "st" TASK_BITMAP_DBUS_TYPE_CODE,
	                          &Path, &Threshold, &Bitmap);
	INTER_ASSERT(Ret >= 0, "failed to read message: %s", StrError(-Ret).c_str());

	getRuntimeKeeper().Log(
		RuntimeKeeper::loglevel::INFO,
		"==CLPKM== Got config from the service:\n"
		"==CLPKM==   cc: \"%s\"\n"
		"==CLPKM==   threshold: %" PRIu64 "\n"
		"==CLPKM==   level: %" TASK_BITMAP_PRINTF_SPECIFIER "\n",
		Path, Threshold, Bitmap);

	CompilerPath = Path;

	sd_bus_error_free(&BusError);
	sd_bus_message_unref(Msg);

	IPCWorker = std::thread(&ScheduleService::LowPrioProcWorker, this);

	}



void ScheduleService::SchedStart(task_kind K) {

	size_t Kind = static_cast<size_t>(K);
	task_bitmap Mask = static_cast<task_bitmap>(1) << Kind;

	std::unique_lock<std::mutex> Lock(Mutex);

	// Low priority processes wait until corresponding bit becomes 0
	if (auto* LowPrioTask = std::get_if<low_prio_task>(&Task)) {
		LowPrioTask->CV[Kind].wait(Lock, [&]() -> bool {
			return Mask & ~Bitmap;
			});
		return;
		}

	// High priority task starts here
	auto* HighPrioTask = std::get_if<high_prio_task>(&Task);

	// Update task count and global bitmap
	unsigned OldCount = HighPrioTask->Count[Kind]++;
	AssignBits(Bitmap, 1, Mask);

	// Notify the worker that the bit is no longer 0
	if (!OldCount) {
		itimerspec OneNanoSec = GenOneTimeTimerSpec(0, 1);
		int Ret = timerfd_settime(HighPrioTask->TimerFd[Kind], 0, &OneNanoSec,
		                          nullptr);
		INTER_ASSERT(Ret == 0, "timerfd_settime failed: %s",
		             StrError(errno).c_str());
		}

	}

void ScheduleService::SchedEnd(task_kind K) {

	auto* HighPrioTask = std::get_if<high_prio_task>(&Task);

	if (!HighPrioTask)
		return;

	size_t Kind = static_cast<size_t>(K);
	task_bitmap Mask = static_cast<task_bitmap>(1) << Kind;

	std::unique_lock<std::mutex> Lock(Mutex);

	// Update task count and global bitmap
	unsigned NewCount = --HighPrioTask->Count[Kind];
	AssignBits(Bitmap, NewCount, Mask);

	// Reserve the resource for a while
	// Notify the worker if nobody reset the timer in time
	if (!NewCount) {
		itimerspec OneSec = GenOneTimeTimerSpec(1, 0);
		int Ret = timerfd_settime(HighPrioTask->TimerFd[Kind], 0, &OneSec, nullptr);
		INTER_ASSERT(Ret == 0, "timerfd_settime failed: %s",
		             StrError(errno).c_str());
		}

	}



// Workers
void ScheduleService::HighPrioProcWorker() {

	auto* HighPrioTask = std::get_if<high_prio_task>(&Task);
	INTER_ASSERT(HighPrioTask, "Task is not a high_prio_task!");

	// The task bitmap bitmap of every high priority process is initially 0 from
	// the perspective of the schedule service
	task_bitmap OldBitmap = 0;

	// The last one is for termination event
	pollfd PollFd[NumOfTaskKind + 1] = {};

	for (size_t Kind = 0; Kind < NumOfTaskKind; ++Kind) {
		PollFd[Kind].fd = HighPrioTask->TimerFd[Kind];
		PollFd[Kind].events = POLLIN;
		}

	PollFd[NumOfTaskKind].fd = TermEventFd;
	PollFd[NumOfTaskKind].events = POLLIN;

	while (true) {

		while (ppoll(PollFd, NumOfTaskKind + 1, nullptr, nullptr) < 0)
			INTER_ASSERT(errno == EINTR, "ppoll failed: %s", StrError(errno).c_str());

		// If the ppoll is triggered by termination event or something go wrong
		constexpr auto TermCondMask = POLLIN | POLLERR | POLLHUP | POLLNVAL;
		if (PollFd[NumOfTaskKind].revents & TermCondMask)
			break;

		std::unique_lock<std::mutex> Lock(Mutex);

		// 1's bits in the map are those changed from 1 to 0
		task_bitmap ClearedBitmap =
				OldBitmap & (Bitmap ^ static_cast<task_bitmap>(-1));
		task_bitmap Mask = 1;

		// Not every bit needs to be updated immediately
		// This record what needs to be updated
		task_bitmap MapToSet = Bitmap;

		for (size_t Kind = 0; Kind < NumOfTaskKind; ++Kind, Mask <<= 1) {
			// Make sure nobody is fucking around
			auto RetEvent = PollFd[Kind].revents;
			INTER_ASSERT(!(RetEvent & (POLLERR | POLLHUP | POLLNVAL)),
			             "timerfd revents: %d", RetEvent);
			// Stage the change of those timer has gone off
			if (RetEvent & POLLIN) {
				uint64_t Temp;
				// Consume the data so that it won't wake up ppoll again
				if (read(HighPrioTask->TimerFd[Kind], &Temp, sizeof(Temp)) > 0)
					continue;
				// The timer may be reset during the interval between ppoll and
				// acquiring the mutex
				INTER_ASSERT(errno == EAGAIN, "failed to read timerfd: %s",
				             StrError(errno).c_str());
				}
			// If it's cleared in this iteration, and the timer has yet gone off,
			// revert the change
			if (ClearedBitmap & Mask)
				MapToSet ^= Mask;
			}

		if (MapToSet == OldBitmap)
			continue;

		OldBitmap = MapToSet;
		Lock.unlock();

		// Now tell the daemon to update the change
		sd_bus_error    BusError = SD_BUS_ERROR_NULL;
		sd_bus_message* Msg = nullptr;

		int Ret = sd_bus_call_method(
				Bus,
				"edu.nctu.sslab.CLPKMSchedSrv",  // service
				"/edu/nctu/sslab/CLPKMSchedSrv", // object path
				"edu.nctu.sslab.CLPKMSchedSrv",  // interface
				"SetHighPrioTaskBitmap",         // method name
				&BusError, &Msg, TASK_BITMAP_DBUS_TYPE_CODE, MapToSet);
		INTER_ASSERT(Ret >= 0, "call method failed: %s", StrError(-Ret).c_str());

		unsigned IsGranted = 0;

		// Note: BOOLEAN uses one int to store its value
		//       Pass a C++ bool variable here can result in wild memory access
		Ret = sd_bus_message_read(Msg, "b", &IsGranted);
		INTER_ASSERT(Ret >= 0,
		             "failed to read message: %s",
		             StrError(-Ret).c_str());

		INTER_ASSERT(IsGranted, "the schedule service denied priority change!");

		sd_bus_error_free(&BusError);
		sd_bus_message_unref(Msg);

		}

	INTER_ASSERT(
			!(PollFd[NumOfTaskKind].revents & (POLLERR | POLLHUP | POLLNVAL)),
			"eventfd revents: %d", PollFd[NumOfTaskKind].revents);

	}

void ScheduleService::LowPrioProcWorker() {

	auto* LowPrioTask = std::get_if<low_prio_task>(&Task);
	INTER_ASSERT(LowPrioTask, "Task is not a low_prio_task!");

	// Start from the RunLevel set by the initial call to GetConfig
	task_bitmap OldBitmap = Bitmap;

	// The first is for sd-bus, and the last is for termination event
	pollfd PollFd[2] = {};

	constexpr auto TermCondMask = POLLIN | POLLERR | POLLHUP | POLLNVAL;
	PollFd[1].fd = TermEventFd;
	PollFd[1].events = POLLIN;

	std::unique_lock<std::mutex> Lock(Mutex);

	while (!(PollFd[1].revents & TermCondMask)) {

		// This may change RunLevel
		int Ret = sd_bus_process(Bus, nullptr);
		INTER_ASSERT(Ret >= 0, "failed to process bus: %s", StrError(-Ret).c_str());

		if (Ret > 0)
			continue;

		// If we reach here, Ret is 0
		// No more stuff to process at the moment
		getRuntimeKeeper().Log(
				RuntimeKeeper::loglevel::INFO,
				"==CLPKM== Run level changed to %" TASK_BITMAP_PRINTF_SPECIFIER "\n",
				Bitmap);

		// 1's bits in the map are those changed from 1 to 0
		task_bitmap ClearedBitmap =
				OldBitmap & (Bitmap ^ static_cast<task_bitmap>(-1));
		task_bitmap Mask = 1;

		OldBitmap = Bitmap;
		Lock.unlock();

		// Notify those changed from 1 to 0
		for (size_t Kind = 0; Kind < NumOfTaskKind; ++Kind) {
			if (ClearedBitmap & Mask)
				LowPrioTask->CV[Kind].notify_all();
			Mask <<= 1;
			}

		// These values may change, fetch fresh values right before ppoll
		Ret = PollFd[0].fd = sd_bus_get_fd(Bus);
		INTER_ASSERT(Ret >= 0, "sd_bus_get_fd failed: %s", StrError(-Ret).c_str());
		Ret = PollFd[0].events = sd_bus_get_events(Bus);
		INTER_ASSERT(Ret >= 0, "sd_bus_get_events failed: %s",
		             StrError(-Ret).c_str());

		while (ppoll(PollFd, 2, nullptr, nullptr) < 0)
			INTER_ASSERT(errno == EINTR, "ppoll failed: %s", StrError(errno).c_str());

		Lock.lock();

		}

	INTER_ASSERT(!(PollFd[1].revents & (POLLERR | POLLHUP | POLLNVAL)),
	             "eventfd revents: %d", PollFd[1].revents);

	}



auto CLPKM::getScheduleService(void) -> ScheduleService& {
	static ScheduleService S;
	return S;
	}
