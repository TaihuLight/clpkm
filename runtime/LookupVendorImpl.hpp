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

// Helper class to get the return function pointer type from the OclAPI classes
template <class __api_type>
struct __ret_type {
	// This base class should only be instantiated when the requested function
	// is not listed in LookupList.inc
	// The compiler may decide to instantiate this function anyway and therefore
	// trigger unwanted assertion failure
	// The std::is_same here is to avoid such case
	static_assert(!std::is_same<__api_type, __api_type>::value,
	              "lookup an function that is absent from LookupList.inc");
	};

// Base
template <class __api_type>
typename __ret_type<__api_type>::value Lookup(void) { }

#define CLPKM_LOOKUP(__api_name) \
template <> struct __ret_type<::CLPKM::OclAPI::__api_name> { \
	using value = decltype(&::__api_name); \
	}; \
template <> typename __ret_type<::CLPKM::OclAPI::__api_name>::value \
Lookup<::CLPKM::OclAPI::__api_name>(void);
#include "LookupList.inc"
#undef CLPKM_LOOKUP

}



#endif
