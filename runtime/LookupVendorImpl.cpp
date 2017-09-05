/*
  LookupVendorImpl.hpp

  Impl of OpenCL API lookup and caching

*/

#include "ErrorHandling.hpp"
#include "LookupVendorImpl.hpp"
#include <dlfcn.h>



namespace CLPKM {
#define CLPKM_LOOKUP(__api_name) \
template <> typename __ret_type<::CLPKM::OclAPI::__api_name>::value \
Lookup<::CLPKM::OclAPI::__api_name>(void) { \
	static const auto __api_ptr = \
		reinterpret_cast<decltype(&__api_name)>(dlsym(RTLD_NEXT, #__api_name)); \
	INTER_ASSERT(__api_ptr != nullptr, "dlsym returned a null pointer!"); \
	INTER_ASSERT(dlerror() == nullptr, "dlerror returned non null"); \
	return __api_ptr; \
	}
#include "LookupList.inc"
#undef CLPKM_LOOKUP
}
