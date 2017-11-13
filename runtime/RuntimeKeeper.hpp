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
		DEBUG,
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

	bool shouldLog(loglevel Level) const {
		return (LogLevel >= Level);
		}

	template <class ... T>
	void Log(T&& ... FormatStr) {
		fprintf(stderr, FormatStr...);
		}

	template <class ... T>
	void Log(loglevel Level, T&& ... FormatStr) {
		if (shouldLog(Level))
			Log(FormatStr...);
		}

private:
	RuntimeKeeper(const RuntimeKeeper& ) = delete;
	RuntimeKeeper& operator=(const RuntimeKeeper& ) = delete;

	// Internal functions
	RuntimeKeeper();

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

	// Wa-i! Sugo-i! Tanoshi-!
	friend RuntimeKeeper& getRuntimeKeeper(void);

	};

RuntimeKeeper& getRuntimeKeeper(void);

}



#endif
