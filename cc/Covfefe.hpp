/*
  Covfefe.hpp

  Generate checkpoint/resume code sequence and update kernel profile

*/

#ifndef __CLPKM__COVFEFE_HPP__
#define __CLPKM__COVFEFE_HPP__

#include "KernelProfile.hpp"
#include "LiveVarTracker.hpp"

#include "clang/AST/Stmt.h"

#include <string>
#include <utility>



// Covfefe = checkpoint/resume code for private memory
using Covfefe = std::pair<std::string, std::string>;
Covfefe GenerateCovfefe(LiveVarTracker::liveness&& , KernelProfile& );

// Locfefe = the counterpart of local memory
using Locfefe = std::pair<std::string, std::string>;
Locfefe GenerateLocfefe(std::vector<clang::VarDecl*>& , clang::FunctionDecl* ,
                        KernelProfile& );



#endif
