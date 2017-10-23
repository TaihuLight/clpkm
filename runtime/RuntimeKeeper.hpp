/*
  RuntimeKeeper.hpp

  RuntimeKeeper holds global stuffs for the runtime

*/

#ifndef __CLPKM__RUNTIME_KEEPER_HPP__
#define __CLPKM__RUNTIME_KEEPER_HPP__



#include "KernelProfile.hpp"
#include "ResourceGuard.hpp"

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <boost/thread/shared_mutex.hpp>
#include <CL/opencl.h>



namespace CLPKM {

// Helper classes
struct QueueInfo {
	cl_context   Context;
	cl_device_id Device;
	clQueue      ShadowQueue;

	const bool ShallReorder;
	clEvent    TaskBlocker;
	std::unique_ptr<std::mutex> BlockerMutex;

	QueueInfo(cl_context C, cl_device_id D, clQueue&& Q, bool SR)
	: Context(C), Device(D), ShadowQueue(std::move(Q)), ShallReorder(SR),
	  TaskBlocker(NULL), BlockerMutex(std::make_unique<std::mutex>()) { }
	};

struct ProgramInfo {
	cl_context  Context;
	clProgram   ShadowProgram;
	std::string BuildLog;
	ProfileList KernelProfileList;

	ProgramInfo(cl_context C, clProgram&& P, std::string&& BL, ProfileList&& PL)
	: Context(C), ShadowProgram(std::move(P)), BuildLog(std::move(BL)),
	  KernelProfileList(std::move(PL)) { }
	};

struct KernelInfo {
	using karg_t = std::pair<size_t, const void*>;

	cl_context           Context;
	cl_program           Program;
	const KernelProfile* Profile;
	std::vector<karg_t>  Args;

	std::vector<clKernel>       Pool;
	size_t                      RefCount;
	std::unique_ptr<std::mutex> Mutex;

	KernelInfo(cl_context C, cl_program P, const KernelProfile* KP)
	: Context(C), Program(P), Profile(KP),
	  Args(KP->NumOfParam, karg_t(0, nullptr)), RefCount(1),
	  Mutex(std::make_unique<std::mutex>()) { }
	};

struct EventLog {
	cl_ulong Queued;
	cl_ulong Sumit;
	cl_ulong Start;
	cl_ulong End;
	};

using QueueTable = std::unordered_map<cl_command_queue, QueueInfo>;
using ProgramTable = std::unordered_map<cl_program, ProgramInfo>;
using KernelTable = std::unordered_map<cl_kernel, KernelInfo>;
using EventLogger = std::unordered_map<cl_event, EventLog>;
using tlv_t = cl_uint;



// Main class
class RuntimeKeeper {
public:
	enum priority {
		HIGH = 0,
		LOW,
		NUM_OF_PRIORITY
		};

	enum loglevel {
		FATAL = 0,
		ERROR,
		INFO,
		NUM_OF_LOGLEVEL
		};

	priority getPriority() const { return Priority; }

	QueueTable&   getQueueTable() { return QT; }
	ProgramTable& getProgramTable() { return PT; }
	KernelTable&  getKernelTable() { return KT; }
	EventLogger&  getEventLogger() { return EL; }

	auto& getQTLock() { return QTLock; }
	auto& getPTLock() { return PTLock; }
	auto& getKTLock() { return KTLock; }

	const std::string& getCompilerPath() const { return CompilerPath; }
	tlv_t getCRThreshold() const { return Threshold; }

	template <class ... T>
	void Log(loglevel Level, T&& ... FormatStr) {
		if (LogLevel >= Level)
			fprintf(stderr, FormatStr...);
		}

	class ConfigLoader {
	protected:
		static void setPriority(CLPKM::RuntimeKeeper& RT, priority P) {
			RT.setPriority(P);
			}
		static void setLogLevel(CLPKM::RuntimeKeeper& RT, loglevel L) {
			RT.setLogLevel(L);
			}
		static void setCompilerPath(CLPKM::RuntimeKeeper& RT, std::string&& Path) {
			RT.setCompilerPath(std::move(Path));
			}
		static void setCRThreshold(CLPKM::RuntimeKeeper& RT, tlv_t T) {
			RT.setCRThreshold(T);
			}

	public:
		virtual bool operator()(RuntimeKeeper& ) const = 0;
		virtual ~ConfigLoader() { }

		};

private:
	RuntimeKeeper(const RuntimeKeeper& ) = delete;
	RuntimeKeeper& operator=(const RuntimeKeeper& ) = delete;

	// Internal functions
	// Default parameters
	RuntimeKeeper()
	: Priority(LOW), LogLevel(FATAL), PT(), KT(), EL(),
	  CompilerPath("/usr/bin/clpkm.sh"), Threshold(1000000) { }

	RuntimeKeeper(const ConfigLoader& CL)
	: RuntimeKeeper() {
		if(!CL(*this)) {
			this->Log(loglevel::FATAL, "\n==CLPKM== Failed to load config\n");
			std::terminate();
			}
		}

	void setPriority(priority P) { Priority = P; }
	void setLogLevel(loglevel L) { LogLevel = L; }
	void setCompilerPath(std::string&& Path) { CompilerPath = std::move(Path); }
	void setCRThreshold(tlv_t T) { Threshold = T; }

	// Internal status
	priority Priority;
	loglevel LogLevel;

	// Members
	// OpenCL related stuff
	QueueTable   QT;
	ProgramTable PT;
	KernelTable  KT;
	EventLogger  EL;

	// Locks
	boost::upgrade_mutex QTLock;
	boost::upgrade_mutex PTLock;
	boost::upgrade_mutex KTLock;

	// Config stuff
	std::string CompilerPath;
	tlv_t       Threshold;

	// Wa-i! Sugo-i! Tanoshi-!
	friend RuntimeKeeper& getRuntimeKeeper(void);
	friend class ConfigLoader;

	};

RuntimeKeeper& getRuntimeKeeper(void);

}



#endif
