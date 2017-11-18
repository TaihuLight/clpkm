/*
  Main.cpp

  clpkm-daemon

*/

#include "DaemonKeeper.hpp"
#include "ResourceGuard.hpp"
#include "TaskKind.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <unordered_map>

#include <signal.h>

#include <systemd/sd-bus.h>
#include <yaml-cpp/yaml.h>

using namespace CLPKM;



namespace {

// Config related stuff

// Helper struct to exchange config info because I'm lazy
struct {
	// In
	const char* ConfigPath;
	// Out
	std::string CompilerPath;
	uint64_t    Threshold;
} GblConfig;

// Note: Throw exception on error
void LoadConfig(void) {
	YAML::Node Config = YAML::LoadFile(GblConfig.ConfigPath);
	if (Config["compiler"])
		GblConfig.CompilerPath = Config["compiler"].as<std::string>();
	if (Config["threshold"])
		GblConfig.Threshold = Config["threshold"].as<uint64_t>();
	}

// Handler for SIGHUP to reload config file
void ReloadConfig(int Signal) try {
	getDaemonKeeper().Log(DaemonKeeper::loglevel::INFO,
	                      "Reloading config on signal %d\n", Signal);
	LoadConfig();
	}
catch (const std::exception& E) {
	getDaemonKeeper().Log(DaemonKeeper::loglevel::ERROR,
	                      "Failed to reload config: \"%s\"\n",
	                      E.what());
	}

// Task management related stuff

// Helper struct for task manager because I'm lazy again
struct {
	bool IsOnTerminate = false;

	// Record what kinda task the individual high priority process is running
	std::unordered_map<std::string, task_bitmap> ProcBitmap;

	// Global bitmap indicates if there is any high priority process running task
	// of corresponding kind
	task_bitmap GlobalBitmap = 0;

	// Count of each kind of task that high pirority processes are running
	// Help us incrementally update the bitmap
	unsigned CountOfEachTaskKind[
			static_cast<size_t>(task_kind::NUM_OF_TASK_KIND)] = {};

	} Task;

void OnTerminate(int Signal) {
	getDaemonKeeper().Log(DaemonKeeper::loglevel::INFO,
	                      "Shutting down on signal %d\n", Signal);
	Task.IsOnTerminate = true;
	}

// Runtime call this method on initialization
int GetConfig(sd_bus_message *Msg, void *UserData, sd_bus_error *ErrorRet) {

	(void) UserData;
	(void) ErrorRet;
	int Ret = 0;

	Ret = sd_bus_message_read(Msg, "");

	if (Ret < 0) {
		getDaemonKeeper().Log(DaemonKeeper::loglevel::ERROR,
		                      "GetConfig failed to read message: %s\n",
		                      strerror(-Ret));
		}

	return sd_bus_reply_method_return(
			Msg, "st" TASK_BITMAP_DBUS_TYPE_CODE,
			GblConfig.CompilerPath.c_str(), GblConfig.Threshold,
			Task.GlobalBitmap);

	}

// Update the bitmap of a given process, and propogate it throught global bitmap
void UpdateProcBitmap(task_bitmap& ProcMap, task_bitmap NewMap) {

	// Retrieve old bitmap so we know what has changed
	task_bitmap OldMap = ProcMap;
	ProcMap = NewMap;

	task_bitmap NewGblMap = Task.GlobalBitmap;
	size_t NumOfTaskKind = static_cast<size_t>(task_kind::NUM_OF_TASK_KIND);

	// Increamentally update the global bitmap
	for (size_t Kind = 0; Kind < NumOfTaskKind; ++Kind) {

		unsigned ThisCount = Task.CountOfEachTaskKind[Kind];

		// Update the count of process running this kind of task
		ThisCount += static_cast<unsigned>(NewMap & 1)
		             - static_cast<unsigned>(OldMap & 1);

		// Update the corresponding bit of this kind in the global bitmap
		NewGblMap ^= (-static_cast<task_bitmap>(IsNotZero(ThisCount)) ^ NewGblMap)
		             & (static_cast<task_bitmap>(1) << Kind);

		// Prep for the next kind
		OldMap >>= 1;
		NewMap >>= 1;
		Task.CountOfEachTaskKind[Kind] = ThisCount;

		}

	Task.GlobalBitmap = NewGblMap;

	}

// For high priority processes
int SetHighPrioTaskBitmap(sd_bus_message* Msg, void* UserData,
                          sd_bus_error* ErrorRet) {

	(void) UserData;
	(void) ErrorRet;
	auto& D = getDaemonKeeper();

	// The flag indicates that the process want to set or clear run level
	task_bitmap Bitmap = 0;
	int Ret = sd_bus_message_read(Msg, TASK_BITMAP_DBUS_TYPE_CODE, &Bitmap);

	if (Ret < 0) {
		D.Log(DaemonKeeper::loglevel::ERROR,
		      "SetHighPrioTaskBitmap failed to read message: %s\n", strerror(-Ret));
		return Ret;
		}

	std::string Sender = sd_bus_message_get_sender(Msg);

	// Mask of valid fields
	task_bitmap Mask = 1;
	Mask = (Mask << static_cast<size_t>(task_kind::NUM_OF_TASK_KIND)) - 1;

	bool Reply = true;

	// Check if the bitmap is valid, i.e. no 1's outside of the mask
	if (Bitmap & ~Mask)
		Reply = false;
	// Update the bitmap of the sender process
	// If the value doesn't exist, it's zero-initialized
	else
		UpdateProcBitmap(Task.ProcBitmap[Sender], Bitmap);

	// sd_bus_error_set_const
	return sd_bus_reply_method_return(Msg, "b", Reply);

	}

const sd_bus_vtable SchedSrvVTable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("GetConfig", "", "st" TASK_BITMAP_DBUS_TYPE_CODE, GetConfig,
	              SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("SetHighPrioTaskBitmap", TASK_BITMAP_DBUS_TYPE_CODE, "b",
	              // FIXME: should not be unprivileged!
	              SetHighPrioTaskBitmap, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_SIGNAL("RunLevelChanged", "b", 0),
	SD_BUS_VTABLE_END
	};

int NameOwnerChangeWatcher(sd_bus_message* Msg, void* UserData,
                           sd_bus_error* ErrorRet) {

	(void) UserData;
	(void) ErrorRet;
	auto& D = getDaemonKeeper();

	const char* Name = nullptr;
	const char* OldOwner = nullptr;
	const char* NewOwner = nullptr;

	int Ret = sd_bus_message_read(Msg, "sss", &Name, &OldOwner, &NewOwner);

	if (Ret < 0) {
		D.Log(DaemonKeeper::loglevel::ERROR,
		      "NameOwnerChangeWatcher failed to read message: %s\n",
		      strerror(-Ret));
		return Ret;
		}

	D.Log(DaemonKeeper::loglevel::DEBUG,
	      "Name \"%s\" owner changed: \"%s\" -> \"%s\"\n",
	      Name, OldOwner, NewOwner);

	// If the name has no owner now, i.e. released
	if (NewOwner[0] == '\0') {
		// If the name was owned by a high priority process
		if (auto It = Task.ProcBitmap.find(Name); It != Task.ProcBitmap.end()) {
			// Update its bitmap to all zero, i.e. no running task
			UpdateProcBitmap(It->second, 0);
			// ...and release the bitmap
			Task.ProcBitmap.erase(It);
			}
		}

	return 1;

	}

} // namespace



int main(int ArgCount, const char* ArgVar[]) {

	auto& D = getDaemonKeeper();

	// Check options
	if (ArgCount != 4) {
		D.Log("CLPKM Daemon, providing clpkm-sched-srv\n"
		      "\nUsage:\n"
		      "\t'%s' <run_mode> <bus_mode> <config>\n"
		      "\nOptions:\n"
		      "\t<run_mode> \"terminal\" or \"daemon\"\n"
		      "\t<bus_mode> \"user\" or \"system\"\n", ArgVar[0]);
		return -1;
		}

	// Load initial config
	try {
		GblConfig.ConfigPath = ArgVar[3];
		LoadConfig();
		}
	catch (const std::exception& E) {
		D.Log(DaemonKeeper::loglevel::FATAL,
		      "Failed to load config: \"%s\"",
		      E.what());
		return -1;
		}

	// Set up signal handlers
	if (signal(SIGINT, SIG_IGN) == SIG_ERR
	 || signal(SIGQUIT, SIG_IGN) == SIG_ERR
	 || signal(SIGHUP, ReloadConfig) == SIG_ERR
	 || signal(SIGTERM, OnTerminate) == SIG_ERR
	 || signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
		D.Log(DaemonKeeper::loglevel::FATAL,
		      "Failed to set signal handler: %s\n", strerror(errno));
		return -1;
		}

	int   Ret = 0;
	sdBus Bus = nullptr;

	// Set up bus
	if (!strcmp(ArgVar[2], "system"))
		Ret = sd_bus_open_system(&Bus.get());
	else if (!strcmp(ArgVar[2], "user"))
		Ret = sd_bus_open_user(&Bus.get());
	else {
		D.Log(DaemonKeeper::loglevel::FATAL,
		      "\"%s\" is not a valid bus mode!\n", ArgVar[2]);
		return -1;
		}

	// Daemonize if run on deamon mode
	if (!strcmp(ArgVar[1], "daemon")) {
		if (int ErrNo = D.Daemonize())
			return ErrNo;
		}
	else if (strcmp(ArgVar[1], "terminal")) {
		D.Log(DaemonKeeper::loglevel::FATAL,
		      "\"%s\" is not a valid run mode!\n", ArgVar[1]);
		return -1;
		}

	// Finished handling command options here!
	// Now start bus related initialization

	// Check bus status
	if (Ret < 0) {
		D.Log(DaemonKeeper::loglevel::FATAL,
		      "Failed to open bus: %s\n", strerror(-Ret));
		return -1;
		}

	sdBusSlot Slot = nullptr;

	Ret = sd_bus_add_object_vtable(
			Bus.get(), &Slot.get(),
			"/edu/nctu/sslab/CLPKMSchedSrv",
			"edu.nctu.sslab.CLPKMSchedSrv",
			SchedSrvVTable, nullptr);

	if (Ret < 0) {
		D.Log(DaemonKeeper::loglevel::FATAL,
		      "sd_bus_add_object_vtable failed: %s\n", strerror(-Ret));
		return -1;
		}

	Ret = sd_bus_add_match(
			Bus.get(), &Slot.get(),
			"type='signal',"
			"sender='org.freedesktop.DBus',"
			"interface='org.freedesktop.DBus',"
			"member='NameOwnerChanged'",
			NameOwnerChangeWatcher, nullptr);

	if (Ret < 0) {
		D.Log(DaemonKeeper::loglevel::FATAL,
		      "sd_bus_bus_add_match: %s\n", strerror(-Ret));
		return -1;
		}

	Ret = sd_bus_request_name(Bus.get(), "edu.nctu.sslab.CLPKMSchedSrv", 0);

	if (Ret < 0) {
		D.Log(DaemonKeeper::loglevel::FATAL,
		      "Failed to request name: %s\n", strerror(-Ret));
		return -1;
		}

	task_bitmap Bitmap = 0;

	// Main loop
	while (!Task.IsOnTerminate) {

		Ret = sd_bus_process(Bus.get(), nullptr);

		if (Ret < 0) {
			D.Log(DaemonKeeper::loglevel::FATAL,
			      "Failed to process bus: %s\n", strerror(-Ret));
			break;
			}

		// Check if we got more things to process
		if (Ret > 0)
			continue;

		task_bitmap NewBitmap = Task.GlobalBitmap;

		// No more request atm
		// Check if run level changed
		if (Bitmap != NewBitmap) {

			D.Log(DaemonKeeper::loglevel::INFO,
			      "run level change to %" TASK_BITMAP_PRINTF_SPECIFIER "\n",
			      Bitmap = NewBitmap);

			Ret = sd_bus_emit_signal(
					Bus.get(),
					"/edu/nctu/sslab/CLPKMSchedSrv",
					"edu.nctu.sslab.CLPKMSchedSrv",
					"RunLevelChanged", TASK_BITMAP_DBUS_TYPE_CODE,
					Bitmap);

			if (Ret < 0) {
				D.Log(DaemonKeeper::loglevel::FATAL,
				      "Failed to emit signal: %s\n", strerror(-Ret));
				break;
				}

			}

		Ret = sd_bus_flush(Bus.get());

		if (Ret < 0) {
			D.Log(DaemonKeeper::loglevel::FATAL,
			      "Failed to flush the bus: %s\n", strerror(-Ret));
			break;
			}

		Ret = sd_bus_wait(Bus.get(), static_cast<uint64_t>(-1));

		if (Ret < 0 && Ret != -EINTR) {
			D.Log(DaemonKeeper::loglevel::FATAL,
			      "Failed to wait on bus: %s\n", strerror(-Ret));
			break;
			}

		Ret = 0;

		} // Main loop

	return Ret;

	}
