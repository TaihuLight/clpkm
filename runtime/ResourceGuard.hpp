/*
  ResourceGuard.hpp

  Leveraging RAII to release OpenCL resources

*/

#ifndef __CLPKM__RESOURCE_GUARD_HPP__
#define __CLPKM__RESOURCE_GUARD_HPP__

#include "LookupVendorImpl.hpp"
#include <type_traits>
#include <CL/opencl.h>



namespace CLPKM {

template <class T>
struct Finalizer {
	cl_int Finalize(T ) {
		// Stop the compiler from instantiating it early
		static_assert(!std::is_same<T, T>::value,
		              "the finalizor of requested type shall be specialized");
		// This is to suppress warning of returning nothing
		return CL_SUCCESS;
		}
	};

template <>
struct Finalizer<cl_mem> {
	cl_int Finalize(cl_mem Mem) {
		return Lookup<OclAPI::clReleaseMemObject>()(Mem);
		}
	};

template <>
struct Finalizer<cl_event> {
	cl_int Finalize(cl_event Event) {
		return Lookup<OclAPI::clReleaseEvent>()(Event);
		}
	};

template <>
struct Finalizer<cl_command_queue> {
	cl_int Finalize(cl_command_queue Queue) {
		return Lookup<OclAPI::clReleaseCommandQueue>()(Queue);
		}
	};

template <>
struct Finalizer<cl_program> {
	cl_int Finalize(cl_program Program) {
		return Lookup<OclAPI::clReleaseProgram>()(Program);
		}
	};

// A special RAII guard tailored for OpenCL stuff
// The inheritence is to enable empty base class optimization
template <class ResType>
class ResGuard : private Finalizer<ResType> {
public:
	ResGuard(ResType R)
	: Resource(R) { }

	ResGuard(ResGuard&& RHS)
	: Resource(RHS.Resource) { RHS.Resource = NULL; }

	~ResGuard() { Release(); }

	ResGuard& operator=(ResGuard&& RHS) {
		std::swap(Resource, RHS.Resource);
		return (*this);
		}

	cl_int Release(void) {
		cl_int Ret = CL_SUCCESS;
		if (Resource != NULL) {
			Ret = Finalizer<ResType>::Finalize(Resource);
			Resource = NULL;
			}
		return Ret;
		}

	ResType& get(void) { return Resource; }

	ResGuard() = delete;
	ResGuard(const ResGuard& ) = delete;
	ResGuard& operator=(const ResGuard& ) = delete;

private:
	ResType Resource;

	};


using clMemObj = ResGuard<cl_mem>;
using clEvent = ResGuard<cl_event>;
using clQueue = ResGuard<cl_command_queue>;
using clProgram = ResGuard<cl_program>;

} // namespace CLPKM



#endif
