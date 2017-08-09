/*
  LookupVendorImpl.hpp

  Call the runtime loader to lookup the OpenCL API implementation from the
  underlying runtime

*/

#ifndef __CLPKM__LOOKUP_VENDOR_IMPL_HPP__
#define __CLPKM__LOOKUP_VENDOR_IMPL_HPP__

#include <type_traits>
#include <CL/opencl.h>



namespace CLPKM {

// Helper classes
namespace OclAPI {
	#define CLPKM_LOOKUP(__api_name) struct __api_name { };
	#include "LookupList.inc"
	#undef CLPKM_LOOKUP
	}

template <class __api_type>
void* Lookup(void) {
	// This function should only be called and instantiated when the requested
	// function is not listed in LookupList.inc
	// The compiler may decide to instantiate this function anyway and therefore
	// trigger unwanted assertion failure
	// The std::is_same here is to avoid such case
	static_assert(std::is_same<__api_type, __api_type>::value,
	              "lookup an function that is absent from LookupList.inc");
	return nullptr;
	}

#define CLPKM_LOOKUP(__api_name) \
template <> decltype(&__api_name) Lookup<OclAPI::__api_name>(void);
#undef CLPKM_LOOKUP

}



#endif
