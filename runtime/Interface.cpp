/*
  Interface.cpp

  OpenCL interface

*/



#include "CompilerDriver.hpp"
#include "KernelProfile.hpp"
#include "LookupVendorImpl.hpp"
#include "RuntimeKeeper.hpp"

#include <cstring>
#include <string>
#include <vector>

#include <CL/opencl.h>

using namespace CLPKM;



namespace {
class clMemObj {
public:
	clMemObj(cl_mem M)
	: MemObj(M) { }

	~clMemObj() { if (MemObj != NULL) clReleaseMemObject(MemObj); }

	cl_mem& get(void) { return MemObj; }

private:
	cl_mem MemObj;
	};
}



extern "C" {

cl_int clGetProgramBuildInfo(cl_program Program, cl_device_id Device,
                             cl_program_build_info ParamName,
                             size_t ParamValSize, void* ParamVal,
                             size_t* ParamValSizeRet) {

	auto& PT = getRuntimeKeeper().getProgramTable();
	auto It = PT.find(Program);

	// Found
	if (It != PT.end()) {
		// If its shadow is valid, use shadow
		if (It->second.ShadowProgram != NULL)
			Program = It->second.ShadowProgram;
		// If not, we can intercept CL_PROGRAM_BUILD_LOG
		else if (ParamName == CL_PROGRAM_BUILD_LOG) {
			if (ParamVal != nullptr && ParamValSize > It->second.BuildLog.size())
				strcpy(static_cast<char*>(ParamVal), It->second.BuildLog.c_str());
			if (ParamValSizeRet != nullptr)
				*ParamValSizeRet = It->second.BuildLog.size() + 1;
			return CL_SUCCESS;
			}
		}

	return Lookup<OclAPI::clGetProgramBuildInfo>()(
			Program, Device, ParamName, ParamValSize, ParamVal, ParamValSizeRet);

	}


cl_int clBuildProgram(cl_program Program,
                      cl_uint NumOfDevice, const cl_device_id* DeviceList,
                      const char* Options,
                      void (*Notify)(cl_program, void* ),
                      void* UserData) {

	// FIXME: we can't handle clBuildProgram on the same program twice atm
	std::string Source;
	size_t SourceLength = 0;

	auto venGetProgramInfo = Lookup<OclAPI::clGetProgramInfo>();
	cl_int Ret = venGetProgramInfo(Program, CL_PROGRAM_SOURCE, 0, nullptr,
	                               &SourceLength);

	if (Ret != CL_SUCCESS)
		return Ret;

	// Must be greater than zero because the size includes null terminator
	assert(SourceLength > 0 && "clGetProgramInfo returned zero source length");

	// If the program is created via clCreateProgramWithBinary or is a built-in
	// kernel, it returns a null string
	// TODO: we may want to support this?
	if (SourceLength == 1)
		throw std::runtime_error("Yet impl'd");

	// Retrieve the source to instrument and remove the null terminator
	Source.resize(SourceLength, '\0');
	Ret = venGetProgramInfo(Program, CL_PROGRAM_SOURCE, SourceLength,
	                        Source.data(), nullptr);
	Source.resize(SourceLength - 1);

	if (Ret != CL_SUCCESS)
		return Ret;

	cl_context Context;

	// Retrieve the context to build a new program
	Ret = venGetProgramInfo(Program, CL_PROGRAM_CONTEXT, sizeof(Context),
	                        &Context, nullptr);

	if (Ret != CL_SUCCESS)
		return Ret;

	auto& PT = getRuntimeKeeper().getProgramTable();
	ProfileList PL;

	// Now invoke CLPKMCC
	if (!CLPKM::Compile(Source, Options, PL)) {
		auto& Entry = PT[Program];
		Entry.ShadowProgram = NULL;
		Entry.BuildLog = std::move(Source);
		Entry.KernelProfileList.clear();
		return CL_BUILD_PROGRAM_FAILURE;
		}

	const char* Ptr = Source.data();
	const size_t Len = Source.size();

	// Build a new program with instrumented code
	cl_program ShadowProgram = Lookup<OclAPI::clCreateProgramWithSource>()(
			Context, 1, &Ptr, &Len, &Ret);

	if (Ret != CL_SUCCESS)
		return Ret;

	auto& Entry = PT[Program];
	Entry.ShadowProgram = ShadowProgram;
	Entry.KernelProfileList = std::move(PL);

	// Call the vendor's impl to build the instrumented code
	return Lookup<OclAPI::clBuildProgram>()(
			ShadowProgram, NumOfDevice, DeviceList, Options, Notify, UserData);

	}

cl_kernel clCreateKernel(cl_program Program, const char* Name, cl_int* Ret) {

	auto& PT = getRuntimeKeeper().getProgramTable();
	auto It = PT.find(Program);

	if (It == PT.end()) {
		if (Ret != nullptr)
			*Ret = CL_INVALID_PROGRAM;
		return NULL;
		}

	if (It->second.ShadowProgram == NULL) {
		if (Ret != nullptr)
			*Ret = CL_INVALID_PROGRAM_EXECUTABLE;
		return NULL;
		}

	auto& List = It->second.KernelProfileList;
	auto Pos = std::find_if(List.begin(), List.end(), [&](auto& Entry) -> bool {
		return strcmp(Name, Entry.Name.c_str()) == 0;
		});

	if (Pos == List.end()) {
		if (Ret != nullptr)
			*Ret = CL_INVALID_KERNEL_NAME;
		return NULL;
		}

	cl_kernel Kernel = Lookup<OclAPI::clCreateKernel>()(
			It->second.ShadowProgram, Name, Ret);

	if (Kernel != NULL)
		getRuntimeKeeper().getKernelTable()[Kernel].Profile = &(*Pos);

	return Kernel;

	}

cl_int clEnqueueNDRangeKernel(cl_command_queue Queue,
                              cl_kernel Kernel,
                              cl_uint WorkDim,
                              const size_t* GlobalWorkOffset,
                              const size_t* GlobalWorkSize,
                              const size_t* LocalWorkSize,
                              cl_uint NumOfWaiting,
                              const cl_event* WaitingList,
                              cl_event* Event) {

	auto& KT = getRuntimeKeeper().getKernelTable();
	auto It = KT.find(Kernel);

	if (It == KT.end())
		return CL_INVALID_KERNEL;

	auto& Profile = *(It->second).Profile;
	cl_context Context;

	cl_int Ret = Lookup<OclAPI::clGetKernelInfo>()(
			Kernel, CL_KERNEL_CONTEXT, sizeof(Context), &Context, nullptr);

	if (Ret != CL_SUCCESS)
		return Ret;

	if (WorkDim < 1)
		return CL_INVALID_WORK_DIMENSION;

	size_t NumOfThread = 1;

	for (size_t Idx = 0; Idx < WorkDim; Idx++)
		NumOfThread *= GlobalWorkSize[Idx];

	auto venCreateBuffer = Lookup<OclAPI::clCreateBuffer>();

	// cl_int, i.e. signed 2's complement 32-bit integer, shall suffice
	std::vector<cl_int> Header(NumOfThread, 1);

	clMemObj DevHeader(venCreateBuffer(Context, CL_MEM_READ_WRITE,
	                   sizeof(cl_int) * NumOfThread, nullptr, &Ret));

	if (Ret != CL_SUCCESS)
		return Ret;

	// TODO
	cl_mem DevLocal = NULL;

	clMemObj DevPrv(Profile.ReqPrvSize > 0
	                ? venCreateBuffer(Context, CL_MEM_READ_WRITE,
	                                  Profile.ReqPrvSize * NumOfThread,
	                                  nullptr, &Ret)
	                : NULL);

	if (Ret != CL_SUCCESS)
		return Ret;

	cl_uint Threshold = getRuntimeKeeper().getCRThreshold();

	auto venEnqWrBuf = Lookup<OclAPI::clEnqueueWriteBuffer>();
	auto venEnqRdBuf = Lookup<OclAPI::clEnqueueReadBuffer>();

	if ((Ret = venEnqWrBuf(Queue, DevHeader.get(), CL_TRUE, 0,
	                       sizeof(cl_int) * NumOfThread, Header.data(),
	                       0, nullptr, nullptr)) != CL_SUCCESS)
		return Ret;

	auto venSetKernelArg = Lookup<OclAPI::clSetKernelArg>();

	// Set args
	auto Idx = Profile.NumOfParam;
	if ((Ret = venSetKernelArg(Kernel, Idx++, sizeof(cl_mem), &DevHeader.get())) != CL_SUCCESS ||
	    (Ret = venSetKernelArg(Kernel, Idx++, sizeof(cl_mem), &DevLocal)) != CL_SUCCESS ||
	    (Ret = venSetKernelArg(Kernel, Idx++, sizeof(cl_mem), &DevPrv.get())) != CL_SUCCESS ||
	    (Ret = venSetKernelArg(Kernel, Idx++, sizeof(cl_uint), &Threshold)) != CL_SUCCESS)
		return Ret;

	auto venEnqNDRKernel = Lookup<OclAPI::clEnqueueNDRangeKernel>();

	auto YetFinished = [&](cl_int* Return) -> bool {
		*Return = venEnqRdBuf(Queue, DevHeader.get(), CL_TRUE, 0,
		                      sizeof(cl_int) * NumOfThread,
		                      Header.data(), 0, nullptr, nullptr);
		return (*Return == CL_SUCCESS) &&
		       std::any_of(Header.begin(), Header.end(),
		                   [](int V) { return V != 0; });
		};

	// Main loop
	do {
		Ret = venEnqNDRKernel(Queue, Kernel, WorkDim, GlobalWorkOffset,
				GlobalWorkSize, LocalWorkSize, NumOfWaiting, WaitingList, Event);
		} while (Ret == CL_SUCCESS && YetFinished(&Ret));

	return Ret;

	}

cl_int clReleaseKernel(cl_kernel Kernel) {

	auto& KT = getRuntimeKeeper().getKernelTable();
	auto It = KT.find(Kernel);

	if (It == KT.end())
		return CL_INVALID_KERNEL;

	KT.erase(It);

	return Lookup<OclAPI::clReleaseKernel>()(Kernel);

	}

cl_int clReleaseProgram(cl_program Program) {

	auto venReleaseProgram = Lookup<OclAPI::clReleaseProgram>();

	auto& PT = getRuntimeKeeper().getProgramTable();
	auto It = PT.find(Program);

	// Release shadow program
	if (It != PT.end()) {
		if (It->second.ShadowProgram != NULL) {
			auto Ret = venReleaseProgram(It->second.ShadowProgram);
			assert(Ret == CL_SUCCESS && "clReleaseProgram failed on shadow program");
			}
		PT.erase(It);
		}

	return venReleaseProgram(Program);

	}

}
