/*
  CompilerDriver.hpp

  Driver to drive CLPKMCC

*/

#ifndef __CLPKM__COMPILER_DRIVER_HPP__
#define __CLPKM__COMPILER_DRIVER_HPP__



#include "KernelProfile.hpp"
#include <string>



namespace CLPKM {
	bool Compile(std::string& Source, const char* Options, ProfileList& PL);
}



#endif
