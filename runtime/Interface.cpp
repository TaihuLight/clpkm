/*
  Interface.cpp

  OpenCL interface

*/



#include "Callback.hpp"
#include "CompilerDriver.hpp"
#include "ErrorHandling.hpp"
#include "KernelProfile.hpp"
#include "LookupVendorImpl.hpp"
#include "ResourceGuard.hpp"
#include "RuntimeKeeper.hpp"

#include <cstring>
#include <string>
#include <vector>

#include <CL/opencl.h>

using namespace CLPKM;



namespace {

std::vector<size_t> FindWorkGroupSize(cl_kernel Kernel, cl_device_id Device,
                                      size_t WorkDim, size_t MaxDim,
                                      const size_t* WorkSize,
                                      cl_int* Ret) {

	auto venGetDevInfo = Lookup<OclAPI::clGetDeviceInfo>();
	std::vector<size_t> MaxThreadSize(MaxDim, 1);

	// Find out max size on each dimension
	*Ret = venGetDevInfo(Device, CL_DEVICE_MAX_WORK_ITEM_SIZES,
	                     sizeof(size_t) * MaxDim, MaxThreadSize.data(),
	                     nullptr);
	OCL_ASSERT(*Ret);

	// Find out max number of work items
	size_t MaxNumOfThread = 0;
	auto venGetKernelBlockInfo = Lookup<OclAPI::clGetKernelWorkGroupInfo>();

	*Ret = venGetKernelBlockInfo(Kernel, Device, CL_KERNEL_WORK_GROUP_SIZE,
	                             sizeof(size_t), &MaxNumOfThread, nullptr);
	OCL_ASSERT(*Ret);

	std::vector<std::vector<size_t>> Factor(WorkDim);

	// Compute the factors of global work size on each dimension
	for (size_t Dim = 0; Dim < WorkDim; ++Dim) {
		for (size_t Num = 1, Quotient;
		     Num <= (Quotient = WorkSize[Dim] / Num)
		     && Num <= MaxThreadSize[Dim];
		     ++Num) {
			if (WorkSize[Dim] % Num)
				continue;
			Factor[Dim].emplace_back(Num);
			if (Quotient != Num && Quotient <= MaxThreadSize[Dim])
				Factor[Dim].emplace_back(Quotient);
			}
		}

	// Each entry in the set stores an index to a factor in the factor table
	std::vector<size_t> TrySet(WorkDim, 0);
	std::vector<size_t> MaxSet(WorkDim, 0);
	size_t MaxSize = 1;

	auto NextTry = [&]() -> bool {
		size_t Dim = 0;
		while (Dim < WorkDim && TrySet[Dim] == Factor[Dim].size() - 1)
			TrySet[Dim++] = 0;
		if (Dim >= WorkDim)
			return false;
		++TrySet[Dim];
		return true;
		};

	// Start from the second try
	while (NextTry()) {
		size_t ThisSize = 1;
		for (size_t Dim = 0; Dim < WorkDim; ++Dim)
			ThisSize *= Factor[Dim][TrySet[Dim]];
		if (ThisSize > MaxSize && ThisSize < MaxNumOfThread) {
			MaxSet = TrySet;
			MaxSize = ThisSize;
			}
		}

	for (size_t Dim = 0; Dim < WorkDim; ++Dim)
		MaxSet[Dim] = Factor[Dim][MaxSet[Dim]];

	auto ToString = [](auto Start, auto End) -> std::string {
		std::string S;
		auto It = Start;
		if (It != End)
			S += std::to_string(*It++);
		while (It != End) {
			S.append(", ");
			S.append(std::to_string(*It++));
			}
		return S;
		};

	getRuntimeKeeper().Log(RuntimeKeeper::loglevel::INFO,
	                       "\n==CLPKM== auto decide local work size\n"
	                       "==CLPKM==   kernel work-group size: %zu\n"
	                       "==CLPKM==   device limit: (%s)\n"
	                       "==CLPKM==   gws: (%s) lws: (%s)\n",
	                       MaxNumOfThread,
	                       ToString(MaxThreadSize.begin(),
	                                MaxThreadSize.end()).c_str(),
	                       ToString(WorkSize, WorkSize + WorkDim).c_str(),
	                       ToString(MaxSet.begin(), MaxSet.end()).c_str());

	return MaxSet;

	}

} // namespace



extern "C" {

cl_command_queue clCreateCommandQueue(cl_context Context, cl_device_id Device,
                                      cl_command_queue_properties Properties,
                                      cl_int* ErrorRet) try {

	// Create original queue
	auto Ret = CL_SUCCESS;
	auto Queue = Lookup<OclAPI::clCreateCommandQueue>()(
			Context, Device, Properties, &Ret);
	OCL_ASSERT(Ret);

	auto QueueWrap = getQueue(Queue);

	// Create shadow queue
	clQueue ShadowQueue = getQueue(Lookup<OclAPI::clCreateCommandQueue>()(
			Context, Device, CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, &Ret));
	OCL_ASSERT(Ret);

	// Create a slot for the queue
	auto& Entry = getRuntimeKeeper().getQueueTable()[Queue];

	Entry.Context = Context;
	Entry.Device = Device;
	Entry.ShadowQueue = ShadowQueue.get();

	// If we reach here, things shall be fine
	// Set to NULL to prevent it from being released
	QueueWrap.get() = NULL;
	ShadowQueue.get() = NULL;
	*ErrorRet = CL_SUCCESS;

	return Queue;

	}
catch (const __ocl_error& OclError) {
	*ErrorRet =  OclError;
	return NULL;
	}
catch (const std::bad_alloc& ) {
	*ErrorRet = CL_OUT_OF_HOST_MEMORY;
	return NULL;
	}

cl_int clReleaseCommandQueue(cl_command_queue Queue) try {
	// TODO
	}
catch (const __ocl_error& OclError) {
	return OclError;
	}

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
                              const size_t* GWO,
                              const size_t* GWS,
                              const size_t* LWS,
                              cl_uint NumOfWaiting,
                              const cl_event* WaitingList,
                              cl_event* Event) try {

	auto& RT = getRuntimeKeeper();
	auto& QT = RT.getQueueTable();
	auto& KT = RT.getKernelTable();

	auto QueueEntry = QT.find(Queue);
	auto It = KT.find(Kernel);

	// Step 0
	// Pre-check
	if (QueueEntry == QT.end())
		return CL_INVALID_COMMAND_QUEUE;

	if (It == KT.end())
		return CL_INVALID_KERNEL;

	if ((NumOfWaiting > 0 && !WaitingList) || (NumOfWaiting <= 0 && WaitingList))
		return CL_INVALID_EVENT_WAIT_LIST;

	auto& Profile = *(It->second).Profile;
	auto& QueueInfo = QueueEntry->second;
	cl_int Ret = CL_SUCCESS;

	// Check whether the kernel and queue are in the same context
	{
		cl_context   Context;

		Ret = Lookup<OclAPI::clGetKernelInfo>()(
				Kernel, CL_KERNEL_CONTEXT, sizeof(Context), &Context, nullptr);
		OCL_ASSERT(Ret);

		if (Context != QueueInfo.Context)
			return CL_INVALID_CONTEXT;

		}

	cl_uint MaxDim = 0;

	Ret = Lookup<OclAPI::clGetDeviceInfo>()(
			QueueInfo.Device, CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS, sizeof(cl_uint),
			&MaxDim, nullptr);
	OCL_ASSERT(Ret);

	if (WorkDim < 1 || WorkDim > MaxDim)
		return CL_INVALID_WORK_DIMENSION;

	if (GWS == nullptr ||
	    std::any_of(GWS, GWS + WorkDim, [](size_t S) -> bool { return !S; }))
		return CL_INVALID_GLOBAL_WORK_SIZE;

	// Step 1
	// Compute total number of work-groups and work-items
	size_t NumOfThread = 1;
	size_t NumOfWorkGrp = 1;

	std::vector<size_t> RealLWS =
			(LWS == nullptr)
			? FindWorkGroupSize(Kernel, QueueInfo.Device, WorkDim, MaxDim,
			                    GWS, &Ret)
			: std::vector<size_t>(LWS, LWS + WorkDim);

	for (size_t Idx = 0; Idx < WorkDim; Idx++) {
		if (GWS[Idx] % RealLWS[Idx])
			return CL_INVALID_WORK_GROUP_SIZE;
		NumOfThread  *= GWS[Idx];
		NumOfWorkGrp *= GWS[Idx] / RealLWS[Idx];
		}

	// Step 2
	// Prepare header and live value buffers
	auto venCreateBuffer = Lookup<OclAPI::clCreateBuffer>();

	clMemObj DeviceHeader = getMemObj(
			venCreateBuffer(QueueInfo.Context, CL_MEM_READ_WRITE,
			                sizeof(cl_int) * NumOfThread, nullptr, &Ret));
	OCL_ASSERT(Ret);

	// TODO: runtime decided size
	clMemObj LocalBuffer = getMemObj(
			Profile.ReqLocSize > 0
			? venCreateBuffer(QueueInfo.Context, CL_MEM_READ_WRITE,
			                  Profile.ReqLocSize * NumOfWorkGrp,
			                  nullptr, &Ret)
			: NULL);
	OCL_ASSERT(Ret);

	clMemObj PrivateBuffer = getMemObj(
			Profile.ReqPrvSize > 0
			? venCreateBuffer(QueueInfo.Context, CL_MEM_READ_WRITE,
			                  Profile.ReqPrvSize * NumOfThread,
			                  nullptr, &Ret)
			: NULL);
	OCL_ASSERT(Ret);

	// Step 3
	// Initialize the header, creating a new waiting event list to include
	// the initializing event
	auto venEnqWrBuf = Lookup<OclAPI::clEnqueueWriteBuffer>();

	// cl_int, i.e. signed 2's complement 32-bit integer, shall suffice
	std::vector<cl_int> HostHeader(NumOfThread, 1);
	clEvent WriteHeaderEvent = getEvent(NULL);

	// Write Header and put the event to the end of NewWaitingList
	Ret = venEnqWrBuf(QueueInfo.ShadowQueue, DeviceHeader.get(), CL_FALSE, 0,
	                  sizeof(cl_int) * NumOfThread, HostHeader.data(),
	                  0, nullptr, &WriteHeaderEvent.get());
	OCL_ASSERT(Ret);

	// Hold original waiting list, in addition to the event of writing header
	std::vector<cl_event> NewWaitingList(NumOfWaiting + 1, NULL);

	if (NumOfWaiting > 0)
		std::copy(WaitingList, WaitingList + NumOfWaiting, NewWaitingList.begin());

	NewWaitingList.back() = WriteHeaderEvent.get();

	// Step 4
	// Set up additional CLPKM-related kernel arguments
	auto venSetKernelArg = Lookup<OclAPI::clSetKernelArg>();
	cl_uint Threshold = RT.getCRThreshold();
	auto Idx = Profile.NumOfParam;

	Ret = venSetKernelArg(Kernel, Idx++, sizeof(cl_mem), &DeviceHeader.get());
	OCL_ASSERT(Ret);
	Ret = venSetKernelArg(Kernel, Idx++, sizeof(cl_mem), &LocalBuffer.get());
	OCL_ASSERT(Ret);
	Ret = venSetKernelArg(Kernel, Idx++, sizeof(cl_mem), &PrivateBuffer.get());
	OCL_ASSERT(Ret);
	Ret = venSetKernelArg(Kernel, Idx++, sizeof(cl_uint), &Threshold);
	OCL_ASSERT(Ret);

	// Step 5
	// Set up the works

	// This event will be set if the kernel really finished
	clEvent Final = getEvent(Lookup<OclAPI::clCreateUserEvent>()(
			QueueInfo.Context, &Ret));
	OCL_ASSERT(Ret);

	Ret = clEnqueueMarkerWithWaitList(Queue, 1, &Final.get(), Event);
	OCL_ASSERT(Ret);

	// New callback
	CallbackData* Work = new CallbackData(
			QueueInfo.ShadowQueue, Kernel, WorkDim,
			GWO ? std::vector<size_t>(GWO, GWO + WorkDim)
			    : std::vector<size_t>(WorkDim, 0),
			std::vector<size_t>(GWS, GWS + WorkDim),
			std::move(RealLWS), std::move(DeviceHeader),
			std::move(LocalBuffer), std::move(PrivateBuffer), std::move(HostHeader),
			std::move(WriteHeaderEvent), std::move(Final));

	// This throws exception on error
	MetaEnqueue(Work, NewWaitingList.size(), NewWaitingList.data());

	return Ret;

	}
catch (const __ocl_error& OclError) {
	return OclError;
	}
catch (const std::bad_alloc& ) {
	return CL_OUT_OF_HOST_MEMORY;
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
