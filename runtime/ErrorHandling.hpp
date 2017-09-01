/*
  ErrorHandling.hpp

  Some helper class and macro for error handling

*/

#ifndef __CLPKM__ERROR_HANDLING_HPP__
#define __CLPKM__ERROR_HANDLING_HPP__

#include "RuntimeKeeper.hpp"
#include <cinttypes>
#include <CL/opencl.h>



namespace CLPKM {

// Helper class to for exception handling to identify OpenCL errors
class __ocl_error {
public:
	__ocl_error(cl_int R)
	: __error_code(R) { }

	operator cl_int() const { return __error_code; }

private:
	cl_int __error_code;

	};

}

#define OCL_THROW(Status) throw CLPKM::__ocl_error(Status)

#define OCL_ASSERT(Status) \
	do { \
		/* Avoid re-evaluation in case it has any side-effects */ \
		cl_int __status = (Status); \
		if (__status != CL_SUCCESS) { \
			CLPKM::getRuntimeKeeper().Log( \
					CLPKM::RuntimeKeeper::loglevel::ERROR, \
					"\n==CLPKM== %s:%d (%s) ret " \
					PRId32 "\n", __FILE__, __LINE__, __func__, __status); \
			OCL_THROW(__status); \
			} \
		} while(0)



#endif
