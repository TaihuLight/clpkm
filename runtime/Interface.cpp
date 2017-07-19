/*
  Interface.cpp

  OpenCL interface

*/



#include "CompilerDriver.hpp"
#include "KernelProfile.hpp"
#include "RuntimeKeeper.hpp"

#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <dlfcn.h>
#include <CL/opencl.h>



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

	auto& PT = CLPKM::getRuntimeKeeper().getProgramTable();
	auto It = PT.find(Program);

	// FIXME: custom CL_PROGRAM_BUILD_LOG
	if (It != PT.end())
		Program = It->second.ShadowProgram;

	static auto Impl = reinterpret_cast<decltype(&clGetProgramBuildInfo)>(
	                   		dlsym(RTLD_NEXT, "clGetProgramBuildInfo"));

	return Impl(Program, Device, ParamName,
	            ParamValSize, ParamVal, ParamValSizeRet);

	}


cl_int clBuildProgram(cl_program Program,
                      cl_uint NumOfDevice, const cl_device_id* DeviceList,
                      const char* Options,
                      void (*Notify)(cl_program, void* ),
                      void* UserData) {

	std::unique_ptr<char[]> Source;
	size_t SourceLength = 0;

	cl_int Ret = clGetProgramInfo(Program, CL_PROGRAM_SOURCE,
                                 0, nullptr, &SourceLength);

	if (Ret != CL_SUCCESS)
		return Ret;

	// Retrieve the source to instrument
	Source = std::make_unique<char[]>(SourceLength);
	Ret = clGetProgramInfo(Program, CL_PROGRAM_SOURCE,
	                       SourceLength, Source.get(), nullptr);

	if (Ret != CL_SUCCESS)
		return Ret;

	// If the program is created via clCreateProgramWithBinary or so, it returns
	// a null string
	// TODO: we may want to support this?
	if (Source[0] == '\0')
		throw std::runtime_error("Yet impl'd");

	cl_context Context;

	// Retrieve the context to build a new program
	Ret = clGetProgramInfo(Program, CL_PROGRAM_CONTEXT,
	                       sizeof(Context), &Context, nullptr);

	if (Ret != CL_SUCCESS)
		return Ret;

	// Now invoke CLPKMCC
	ProfileList PL;
	std::string InstrumentedSource = CLPKM::Compile(Source.get(), SourceLength,
	                                                Options, PL);
	const char* Ptr = &InstrumentedSource[0];
	const size_t Len = InstrumentedSource.size();
	auto& PT = CLPKM::getRuntimeKeeper().getProgramTable();

	// FIXME
	if (InstrumentedSource.empty()) {
		PT[Program].BuildLog = "";
		return CL_BUILD_PROGRAM_FAILURE;
		}

	// Build a new program with instrumented code
	cl_program ShadowProgram =
			clCreateProgramWithSource(Context, 1, &Ptr, &Len, &Ret);

	if (Ret != CL_SUCCESS)
		return Ret;

	auto& Entry = PT[Program];
	Entry.ShadowProgram = ShadowProgram;
	Entry.KernelProfileList = std::move(PL);

	static auto Impl = reinterpret_cast<decltype(&clBuildProgram)>(
	                dlsym(RTLD_NEXT, "clBuildProgram"));

	// Call the vendor's impl to build the instrumented code
	Ret = Impl(ShadowProgram, NumOfDevice, DeviceList,
	           Options, Notify, UserData);

	return Ret;

	}

cl_kernel clCreateKernel(cl_program Program, const char* Name, cl_int* Ret) {

	auto& PT = CLPKM::getRuntimeKeeper().getProgramTable();
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

	static auto Impl = reinterpret_cast<decltype(&clCreateKernel)>(
	                   		dlsym(RTLD_NEXT, "clCreateKernel"));
	cl_kernel Kernel = Impl(It->second.ShadowProgram, Name, Ret);

	if (Kernel != NULL)
		CLPKM::getRuntimeKeeper().getKernelTable()[Kernel].Profile = &(*Pos);

	return Kernel;

	}

//cl_int clSetKernelArg(cl_kernel , cl_uint , size_t , const void* );

cl_int clEnqueueNDRangeKernel(cl_command_queue Queue,
                              cl_kernel Kernel,
                              cl_uint WorkDim,
                              const size_t* GlobalWorkOffset,
                              const size_t* GlobalWorkSize,
                              const size_t* LocalWorkSize,
                              cl_uint NumOfWaiting,
                              const cl_event* WaitingList,
                              cl_event* Event) {

	auto& KT = CLPKM::getRuntimeKeeper().getKernelTable();
	auto It = KT.find(Kernel);

	if (It == KT.end())
		return CL_INVALID_KERNEL;

	auto& Profile = *(It->second).Profile;
	cl_context Context;

	cl_int Ret = clGetKernelInfo(Kernel, CL_KERNEL_CONTEXT,
	                             sizeof(Context), &Context, nullptr);

	if (Ret != CL_SUCCESS)
		return Ret;

	if (WorkDim < 1)
		return CL_INVALID_WORK_DIMENSION;

	size_t NumOfThread = 1;

	for (size_t Idx = 0; Idx < WorkDim; Idx++)
		NumOfThread *= GlobalWorkSize[Idx];

	std::unique_ptr<int[]> Header = std::make_unique<int[]>(NumOfThread);

	clMemObj DevHeader(clCreateBuffer(Context, CL_MEM_READ_WRITE,
	                   sizeof(int) * NumOfThread, nullptr, &Ret));

	if (Ret != CL_SUCCESS)
		return Ret;

	cl_mem DevLocal = NULL;

	clMemObj DevPrv(clCreateBuffer(Context, CL_MEM_READ_WRITE,
	                Profile.ReqPrvSize, nullptr, &Ret));

	if (Ret != CL_SUCCESS)
		return Ret;

	cl_ulong Threshold = CLPKM::getRuntimeKeeper().getCRThreshold();

	std::fill(&Header[0], &Header[NumOfThread], 1);

	if ((Ret = clEnqueueWriteBuffer(Queue, DevHeader.get(), CL_TRUE, 0,
	                                sizeof(int) * NumOfThread, Header.get(),
	                                0, nullptr, nullptr)) != CL_SUCCESS)
		return Ret;

	// Set args
	auto Idx = Profile.NumOfParam;
	if ((Ret = clSetKernelArg(Kernel, Idx++, sizeof(cl_mem), &DevHeader.get())) != CL_SUCCESS ||
	    (Ret = clSetKernelArg(Kernel, Idx++, sizeof(cl_mem), &DevLocal)) != CL_SUCCESS ||
	    (Ret = clSetKernelArg(Kernel, Idx++, sizeof(cl_mem), &DevPrv.get())) != CL_SUCCESS ||
	    (Ret = clSetKernelArg(Kernel, Idx++, sizeof(cl_ulong), &Threshold)) != CL_SUCCESS)
		return Ret;

	static auto Impl = reinterpret_cast<decltype(&clEnqueueNDRangeKernel)>(
	                   		dlsym(RTLD_NEXT, "clEnqueueNDRangeKernel"));

	auto YetFinished = [&](cl_int* Return) -> bool {
		*Return = clEnqueueReadBuffer(Queue, DevHeader.get(), CL_TRUE, 0,
		                               sizeof(int) * NumOfThread, Header.get(),
		                               0, nullptr, nullptr);
		return (*Return == CL_SUCCESS) &&
		        std::find_if(&Header[0], &Header[NumOfThread],
		        [](int Val) -> bool { return Val != 0; }) != &Header[NumOfThread];
		};

	// Main loop
	do {
		Ret = Impl(Queue, Kernel, WorkDim, GlobalWorkOffset, GlobalWorkSize,
	              LocalWorkSize, NumOfWaiting, WaitingList, Event);
		} while (Ret == CL_SUCCESS && YetFinished(&Ret));

	return Ret;

	}

}
