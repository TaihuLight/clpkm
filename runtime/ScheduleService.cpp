/*
  ScheduleService.cpp

  Impl schedule service

*/

#include "ErrorHandling.hpp"
#include "ScheduleService.hpp"
#include "LookupVendorImpl.hpp"
#include <cstdlib>
#include <cstring>

using namespace CLPKM;



namespace {

// Watcher for low priority tasks
int RunLevelChangeWatcher(sd_bus_message* Msg, void* UserData,
                          sd_bus_error* ErrorRet) {

	(void) ErrorRet;

	task_bitmap& Bitmap = *reinterpret_cast<task_bitmap*>(UserData);

	int Ret = sd_bus_message_read(Msg, TASK_BITMAP_DBUS_TYPE_CODE, &Bitmap);
	INTER_ASSERT(Ret >= 0,
	             "failed to read message from bus: %s", StrError(-Ret).c_str());

	getRuntimeKeeper().Log(
			RuntimeKeeper::loglevel::INFO,
			"==CLPKM== Run level changed to %" TASK_BITMAP_PRINTF_SPECIFIER "\n",
			Bitmap);

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
: IsOnTerminate(false), IsOnSystemBus(false), Priority(priority::LOW),
  Bus(nullptr), Threshold(0), Bitmap(0) {

	if (const char* Fine = getenv("CLPKM_PRIORITY")) {
		if (!strcmp(Fine, "high"))
			Priority = priority::HIGH;
		else if (strcmp(Fine, "low")) {
			getRuntimeKeeper().Log("==CLPKM== Unrecognised priority: \"%s\"\n",
			                       Fine);
			}
		}

	StartBus();

	}

ScheduleService::~ScheduleService() {
	this->Terminate();
	}



void ScheduleService::Terminate() {

	IsOnTerminate = true;

	// Notify the IPC worker
	if (auto* HighPrioTask = std::get_if<high_prio_task>(&Task))
		HighPrioTask->CV.notify_all();

	if (IPCWorker.joinable())
		IPCWorker.join();

	// Clean up and reset to NULL
	Bus = sd_bus_flush_close_unref(Bus);

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
		Task.emplace<high_prio_task>();
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

	if (Priority == priority::HIGH) {

		auto* HighPrioTask = std::get_if<high_prio_task>(&Task);
		std::unique_lock<std::mutex> Lock(Mutex);

		unsigned OldCount = HighPrioTask->Count[Kind]++;
		AssignBits(Bitmap, 1, Mask);

		Lock.unlock();

		// Notify the worker that the bit is no longer 0
		if (!OldCount)
			HighPrioTask->CV.notify_one();

		return;

		}

	// Low priority task starts here
	auto* LowPrioTask = std::get_if<low_prio_task>(&Task);
	std::unique_lock<std::mutex> Lock(Mutex);

	// Wait until corresponding bit becomes 0
	LowPrioTask->CV[Kind].wait(Lock, [&]() -> bool {
		return Mask & ~Bitmap;
		});

	}

void ScheduleService::SchedEnd(task_kind K) {

	if (Priority == priority::LOW)
		return;

	size_t Kind = static_cast<size_t>(K);
	task_bitmap Mask = static_cast<task_bitmap>(1) << Kind;

	auto* HighPrioTask = std::get_if<high_prio_task>(&Task);
	std::unique_lock<std::mutex> Lock(Mutex);

	// Update task count and global bitmap
	unsigned NewCount = --HighPrioTask->Count[Kind];
	AssignBits(Bitmap, NewCount, Mask);

	Lock.unlock();

	// Notify the worker if it became 0 again
	if (!NewCount)
		HighPrioTask->CV.notify_one();

	}



// Workers
void ScheduleService::HighPrioProcWorker() {

	auto* HighPrioTask = std::get_if<high_prio_task>(&Task);
	INTER_ASSERT(HighPrioTask != nullptr, "Task is not a high_prio_task!");

	// The task bitmap bitmap of every high priority process is initially 0 from
	// the perspective of the schedule service
	task_bitmap OldBitmap = 0;

	while (!IsOnTerminate) {

		std::unique_lock<std::mutex> Lock(Mutex);

		// Wait until the termination of this process, or the run level had
		// changed
		HighPrioTask->CV.wait(Lock, [&]() -> bool {
			return (OldBitmap != Bitmap) || IsOnTerminate;
			});

		// If we got here due to IsOnTerminate
		if (IsOnTerminate)
			return;

		sd_bus_error    BusError = SD_BUS_ERROR_NULL;
		sd_bus_message* Msg = nullptr;

		// Note: bool is promoted to int here, no need to worry about the width
		int Ret = sd_bus_call_method(
				Bus,
				"edu.nctu.sslab.CLPKMSchedSrv",  // service
				"/edu/nctu/sslab/CLPKMSchedSrv", // object path
				"edu.nctu.sslab.CLPKMSchedSrv",  // interface
				"SetHighPrioTaskBitmap",         // method name
				&BusError, &Msg, TASK_BITMAP_DBUS_TYPE_CODE, OldBitmap = Bitmap);
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

	}

void ScheduleService::LowPrioProcWorker() {

	auto* LowPrioTask = std::get_if<low_prio_task>(&Task);
	INTER_ASSERT(LowPrioTask != nullptr, "Task is not a low_prio_task!");

	// Start from the RunLevel set by the initial call to GetConfig
	task_bitmap OldBitmap = Bitmap;
	uint64_t Timeout = 0;

	{
		int Ret = sd_bus_get_timeout(Bus, &Timeout);
		INTER_ASSERT(Ret >= 0, "failed to get bus timeout", StrError(-Ret).c_str());

		// 0.1s shall be enough
		if (!Timeout)
			Timeout = 100000;

		getRuntimeKeeper().Log(
				RuntimeKeeper::loglevel::INFO,
				"==CLPKM== Timeout of sd_bus_wait is set to %" PRIu64 " us\n",
				Timeout);

		}

	std::unique_lock<std::mutex> Lock(Mutex);

	while (!IsOnTerminate) {

		// This may change RunLevel
		int Ret = sd_bus_process(Bus, nullptr);
		INTER_ASSERT(Ret >= 0, "failed to process bus: %s", StrError(-Ret).c_str());

		if (Ret > 0)
			continue;

		// If we reach here, Ret is 0
		// No more stuff to process at the moment

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

		// sd_bus_wait can only return on signal or timeout
		// We are running as a library, messing around signal seems not a good
		// idea
		Ret = sd_bus_wait(Bus, Timeout);
		INTER_ASSERT(Ret >= 0 || Ret == -EINTR,
		             "failed to wait on bus: %s", StrError(-Ret).c_str());

		Lock.lock();

		}

	}



auto CLPKM::getScheduleService(void) -> ScheduleService& {
	static ScheduleService S;
	return S;
	}
