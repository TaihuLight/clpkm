/*
  RuntimeKeeper.hpp

  Globals or so

*/

#ifndef __CLPKM__RUNTIME_KEEPER_HPP__
#define __CLPKM__RUNTIME_KEEPER_HPP__



#include "KernelProfile.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <CL/opencl.h>



namespace CLPKM {

// Helper classes
struct ProgramInfo {
	cl_program  ShadowProgram;
	std::string BuildLog;
	ProfileList KernelProfileList;
	ProgramInfo() : ShadowProgram(NULL), BuildLog(), KernelProfileList() { }
	};

struct KernelInfo {
	KernelProfile* Profile;
	KernelInfo() : Profile(nullptr) { }
	};

struct EventLog {
	cl_ulong Queued;
	cl_ulong Sumit;
	cl_ulong Start;
	cl_ulong End;
	};

using ProgramTable = std::unordered_map<cl_program, ProgramInfo>;
using KernelTable = std::unordered_map<cl_kernel, KernelInfo>;
using EventLogger = std::unordered_map<cl_event, EventLog>;
using tlv_t = cl_uint;



// Main class
class RuntimeKeeper {
public:

	enum state_t {
		SUCCEED = 0,
		INVALID_CONFIG,
		UNKNOWN_ERROR,
		NUM_OF_STATE
	};

	enum priority {
		HIGH = 0,
		LOW,
		NUM_OF_PRIORITY
		};

	state_t getState() { return State; }
	priority getPriority() { return Priority; }

	ProgramTable& getProgramTable() { return PT; }
	KernelTable&  getKernelTable() { return KT; }
	EventLogger&  getEventLogger() { return EL; }
	const std::string& getCompilerPath() { return CompilerPath; }
	tlv_t getCRThreshold() { return Threshold; }

	void setPriority(priority P) { Priority = P; }
	void setCompilerPath(std::string&& Path) { CompilerPath = std::move(Path); }
	void setCRThreshold(tlv_t T) { Threshold = T; }

	struct ConfigLoader {
		virtual state_t operator()(RuntimeKeeper& ) = 0;
		virtual ~ConfigLoader() { }
		};

private:
	RuntimeKeeper(const RuntimeKeeper& ) = delete;
	RuntimeKeeper& operator=(const RuntimeKeeper& ) = delete;

	// Internal functions
	// Default parameters
	RuntimeKeeper()
	: State(SUCCEED), PT(), KT(), EL(), CompilerPath("/usr/bin/clpkm.sh"),
	  Threshold(1000000) { }

	RuntimeKeeper(ConfigLoader& CL)
	: RuntimeKeeper() { State = CL(*this); }

	// Internal status
	state_t  State;
	priority Priority;

	// Members
	// OpenCL related stuff
	ProgramTable PT;
	KernelTable  KT;
	EventLogger  EL;

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
