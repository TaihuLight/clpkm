/*
  Main.cpp

  clpkm-daemon

*/

#include "DaemonKeeper.hpp"
#include "ResourceGuard.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <unordered_set>

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
void ReloadConfig(int ) try {
	LoadConfig();
	}
catch (const std::exception& E) {
	getDaemonKeeper().Log(
			DaemonKeeper::loglevel::ERROR,
			"Failed to reload config: \"%s\"",
			E.what());
	}

// Task management related stuff

// Helper struct for task manager because I'm lazy again
struct {
	std::unordered_set<std::string> HighPrioSet;
	} Task;

// Runtime call this method on initialization
int GetConfig(sd_bus_message *Msg, void *UserData, sd_bus_error *ErrorRet) {

	int Ret = 0;

	Ret = sd_bus_message_read(Msg, "");

	if (Ret < 0) {
		getDaemonKeeper().Log(
				DaemonKeeper::loglevel::ERROR,
				"GetConfig failed to read message: %s\n",
				strerror(-Ret));
		}

	return sd_bus_reply_method_return(
			Msg, "st", GblConfig.CompilerPath.c_str(), GblConfig.Threshold);

	}

// For high priority processes
int SetHighPrioProc(sd_bus_message* Msg, void* UserData,
                    sd_bus_error* ErrorRet) {

	auto& D = getDaemonKeeper();

	// The flag indicates that the process want to set or clear run level
	bool Flag = false;
	int  Ret = sd_bus_message_read(Msg, "b", &Flag);

	if (Ret < 0) {
		D.Log(DaemonKeeper::loglevel::ERROR,
		      "SetHighPrioProc failed to read message: %s\n", strerror(-Ret));
		return Ret;
		}

	std::string Sender = sd_bus_message_get_sender(Msg);

	// Check if the process is in the high priority process set
	auto It = Task.HighPrioSet.find(Sender);
	bool Reply = true;

	// If the process want to register, and it is not already in the set
	if (Flag && It == Task.HighPrioSet.end())
		Task.HighPrioSet.emplace(std::move(Sender));
	// If the process want to unregister
	else if (!Flag && It != Task.HighPrioSet.end())
		Task.HighPrioSet.erase(It);
	// If the request doesn't make sense, reply false
	else
		Reply = false;

	// sd_bus_error_set_const
	return sd_bus_reply_method_return(Msg, "b", Reply);

	}

/* The vtable of our little object, implements the net.poettering.Calculator
 * interface */
const sd_bus_vtable SchedSrvVTable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("GetConfig", "", "su", GetConfig,
	              SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("SetHighPrioProc", "b", "b", SetHighPrioProc,
	              // FIXME: should not be unprivileged!
	              SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_SIGNAL("RunLevelChanged", "b", 0),
	SD_BUS_VTABLE_END
	};

int NameOwnerChangeWatcher(sd_bus_message* Msg, void* UserData,
                           sd_bus_error* ErrorRet) {

	auto& D = getDaemonKeeper();

	const char* Name = nullptr;
	const char* OldOwner = nullptr;
	const char* NewOwner = nullptr;

	int Ret = sd_bus_message_read(Msg, "sss", &Name, &OldOwner, &NewOwner);

	if (Ret < 0) {
		D.Log(
				DaemonKeeper::loglevel::ERROR,
				"NameOwnerWatcher failed to read message: %s\n",
				strerror(-Ret));
		return Ret;
		}

	D.Log(DaemonKeeper::loglevel::DEBUG,
	      "NameOwnerChanged\n  "
	      "name: \"%s\"\n  old_owner: \"%s\"\n  new_owner: \"%s\"\n",
	      Name, OldOwner, NewOwner);

	// If the released name was owned by a high priority process, remove it from
	// the set
	if (NewOwner[0] == '\0')
		Task.HighPrioSet.erase(Name);

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
	// Don't handle SIGTERM
	if (signal(SIGINT, SIG_IGN) == SIG_ERR
	 || signal(SIGQUIT, SIG_IGN) == SIG_ERR
	 || signal(SIGHUP, ReloadConfig) == SIG_ERR
	 || signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
		D.Log(DaemonKeeper::loglevel::FATAL,
		      "Failed to set signal handler: %s\n", strerror(errno));
		return -1;
		}

	int Ret = 0;

	sdBus        Bus = nullptr;
	sdBusMessage Msg = nullptr;

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
	if (!strcmp(ArgVar[1], "daemon") && D.Daemonize())
		return -1;
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

	bool HighPrioRunning = false;

	// Main loop
	while (true) {

		sdBusMessage Msg = nullptr;
		Ret = sd_bus_process(Bus.get(), &Msg.get());

		if (Ret < 0) {
			D.Log(DaemonKeeper::loglevel::FATAL,
			      "Failed to process bus: %s\n", strerror(-Ret));
			break;
			}

		// Check if we got more things to process
		if (Ret > 0)
			continue;

		bool HighPrioStillRunning = Task.HighPrioSet.size();

		// No more request atm
		// Check if run level changed
		if (HighPrioRunning != HighPrioStillRunning) {

			D.Log(DaemonKeeper::loglevel::INFO,
			      "run level change to %d\n",
			      HighPrioRunning = HighPrioStillRunning);

			Ret = sd_bus_emit_signal(
					Bus.get(),
					"/edu/nctu/sslab/CLPKMSchedSrv",
					"edu.nctu.sslab.CLPKMSchedSrv",
					"RunLevelChanged", "b",
					HighPrioRunning);

			if (Ret < 0) {
				D.Log(DaemonKeeper::loglevel::FATAL,
				      "Failed to emit signal: %s\n", strerror(-Ret));
				break;
				}

			}

		Ret = sd_bus_wait(Bus.get(), static_cast<uint64_t>(-1));

		if (Ret < 0) {
			D.Log(DaemonKeeper::loglevel::FATAL,
			      "Failed to wait on bus: %s\n",
			      strerror(-Ret));
			break;
			}

		} // Main loop

	return Ret;

	}
