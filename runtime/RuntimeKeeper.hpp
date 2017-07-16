/*
  RuntimeKeeper.hpp

  Globals or so

*/

#ifndef __CLPKM__RUNTIME_KEEPER_HPP__
#define __CLPKM__RUNTIME_KEEPER_HPP__



#include "KernelProfile.hpp"
#include <string>
#include <unordered_map>
#include <CL/opencl.h>



namespace CLPKM {

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

	std::unordered_map<cl_program, ProgramInfo> ProgramTable;
	std::unordered_map<cl_kernel, KernelInfo> KernelTable;
}



#endif
