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

using ProgramTable = std::unordered_map<cl_program, ProgramInfo>;
using KernelTable = std::unordered_map<cl_kernel, KernelInfo>;
using tlv_t = cl_uint;



// Main class
class RuntimeKeeper {
public:
	ProgramTable& getProgramTable() { return PT; }
	KernelTable&  getKernelTable() { return KT; }
	const std::string& getCompilerPath() { return CompilerPath; }
	tlv_t getCRThreshold() { return Threshold; }

	void setCRThreshold(tlv_t T) { Threshold = T; }
	void setCompilerPath(std::string&& Path) { CompilerPath = std::move(Path); }

	enum state_t {
		SUCCEED = 0,
		INVALID_CONFIG,
		UNKNOWN_ERROR,
		NUM_OF_STATE
	};

	struct InitHelper {
		virtual state_t operator()(RuntimeKeeper& ) = 0;
		virtual ~InitHelper() { }
		};

private:
	RuntimeKeeper(const RuntimeKeeper& ) = delete;
	RuntimeKeeper& operator=(const RuntimeKeeper& ) = delete;

	// Internal functions
	// Default parameters
	RuntimeKeeper()
	: State(SUCCEED), PT(), KT(), CompilerPath("/usr/bin/clpkm.sh"),
	  Threshold(1000000) { }

	RuntimeKeeper(InitHelper& Init)
	: RuntimeKeeper() { State = Init(*this); }

	// Internal status
	state_t State;

	// Members
	// OpenCL related stuff
	ProgramTable PT;
	KernelTable  KT;

	// Config stuff
	std::string CompilerPath;
	tlv_t       Threshold;

	// Wa-i! Sugo-i! Tanoshi-!
	friend RuntimeKeeper& getRuntimeKeeper(void);
	friend class InitHelper;

	};

RuntimeKeeper& getRuntimeKeeper(void);

}



#endif
