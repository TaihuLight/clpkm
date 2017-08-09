/*
  LookupVendorImpl.hpp

  Impl of OpenCL API lookup and caching

*/

#include "LookupVendorImpl.hpp"
#include <cassert>
#include <dlfcn.h>



namespace CLPKM {
#define CLPKM_LOOKUP(__api_name) \
template <> typename __ret_type<::CLPKM::OclAPI::__api_name>::value \
Lookup<::CLPKM::OclAPI::__api_name>(void) { \
static auto __api_ptr = \
		reinterpret_cast<decltype(&__api_name)>(dlsym(RTLD_NEXT, #__api_name)); \
	assert(__api_ptr != nullptr && "dlsym returned a null pointer!"); \
	assert(dlerror() == nullptr && "dlerror returned non null"); \
	return __api_ptr; \
	}
#include "LookupList.inc"
#undef CLPKM_LOOKUP
}
