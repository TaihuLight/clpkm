/*
  ResourceGuard.hpp

  Leveraging RAII to release OpenCL resources

*/

#ifndef __CLPKM__RESOURCE_GUARD_HPP__
#define __CLPKM__RESOURCE_GUARD_HPP__

#include "LookupVendorImpl.hpp"
#include <CL/opencl.h>



namespace CLPKM {

// A special RAII guard tailored for OpenCL stuff
template <class ResType, class FinType>
class ResGuard {
public:
	ResGuard(ResType R, FinType F)
	: Resource(R), Finalize(F) { }

	~ResGuard() { Release(); }

	cl_int Release(void) {
		cl_int Ret = CL_SUCCESS;
		if (Resource != NULL) {
			Ret = Finalize(Resource);
			Resource = NULL;
			}
		return Ret;
		}

	ResGuard() = delete;
	ResGuard(const ResGuard& ) = delete;
	ResGuard& operator=(const ResGuard& ) = delete;

	ResGuard(ResGuard&& RHS) {
		Resource = RHS.Resource;
		RHS.Resource = NULL;
		}

	ResGuard& operator=(ResGuard&& RHS) {
		Release();
		Resource = RHS.Resource;
		RHS.Resource = NULL;
		}

	ResType& get(void) { return Resource; }

private:
	ResType Resource;
	FinType Finalize;

	};

using clMemObj = ResGuard<cl_mem, decltype(&clReleaseMemObject)>;
using clEvent = ResGuard<cl_event, decltype(&clReleaseEvent)>;
using clQueue = ResGuard<cl_command_queue, decltype(&clReleaseCommandQueue)>;

} // namespace CLPKM

namespace {

inline CLPKM::clMemObj getMemObj(cl_mem MemObj) {
	return CLPKM::clMemObj(
			MemObj, CLPKM::Lookup<CLPKM::OclAPI::clReleaseMemObject>());
	}

inline CLPKM::clEvent getEvent(cl_event Event) {
	return CLPKM::clEvent(Event, CLPKM::Lookup<CLPKM::OclAPI::clReleaseEvent>());
	}

inline CLPKM::clQueue getQueue(cl_command_queue Queue) {
	return CLPKM::clQueue(Queue, CLPKM::Lookup<CLPKM::OclAPI::clReleaseCommandQueue>());
	}

} // namespace



#endif
