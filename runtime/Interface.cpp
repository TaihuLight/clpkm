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
#include "Support.hpp"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <numeric>
#include <string>
#include <vector>

#include <CL/opencl.h>

using namespace CLPKM;



extern "C" {

cl_command_queue clCreateCommandQueue(cl_context Context, cl_device_id Device,
                                      cl_command_queue_properties Properties,
                                      cl_int* ErrorRet) try {

	auto Ret = CL_SUCCESS;

	// Create original queue
	auto RawQueue = Lookup<OclAPI::clCreateCommandQueue>()(
			Context, Device, Properties, &Ret);
	OCL_ASSERT(Ret);

	clQueue QueueWrap(RawQueue);

	// Create shadow queue
	constexpr auto Property = CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE |
	                          CL_QUEUE_PROFILING_ENABLE;

	clQueue ShadowQueue = Lookup<OclAPI::clCreateCommandQueue>()(
			Context, Device, Property, &Ret);
	OCL_ASSERT(Ret);

	auto& RT = getRuntimeKeeper();
	auto& QT = RT.getQueueTable();

	QueueInfo NewInfo(Context, Device, std::move(ShadowQueue),
	                  !(Properties & CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE));

	// Create a slot for the queue
	boost::unique_lock<boost::upgrade_mutex> Lock(RT.getQTLock());

	const auto It = QT.emplace(RawQueue, std::move(NewInfo));
	INTER_ASSERT(It.second, "insertion to queue table didn't take place");

	// If we reach here, things shall be fine
	// Set to NULL to prevent it from being released
	QueueWrap.get() = NULL;
	*ErrorRet = CL_SUCCESS;

	return RawQueue;

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

	auto& RT = getRuntimeKeeper();
	auto& QT = RT.getQueueTable();

	boost::upgrade_lock<boost::upgrade_mutex> RdLock(RT.getQTLock());
	const auto It = QT.find(Queue);

	if (It == QT.end())
		return CL_INVALID_COMMAND_QUEUE;

	auto venReleaseQueue = Lookup<OclAPI::clReleaseCommandQueue>();

	// Put mutex here to prevent multi-threads got old reference count and
	// nobody releases the shadow queue
	static std::mutex Mutex;
	std::lock_guard<std::mutex> Lock(Mutex);

	cl_uint RefCount = 0;

	cl_int Ret = Lookup<OclAPI::clGetCommandQueueInfo>()(
			Queue, CL_QUEUE_REFERENCE_COUNT, sizeof(cl_uint), &RefCount, nullptr);
	OCL_ASSERT(Ret);

	if (RefCount <= 1) {
		boost::upgrade_to_unique_lock<boost::upgrade_mutex> WrLock(RdLock);
		QT.erase(It);
		}

	return venReleaseQueue(Queue);

	}
catch (const __ocl_error& OclError) {
	return OclError;
	}

cl_int clGetProgramBuildInfo(cl_program Program, cl_device_id Device,
                             cl_program_build_info ParamName,
                             size_t ParamValSize, void* ParamVal,
                             size_t* ParamValSizeRet) {

	auto& RT = getRuntimeKeeper();
	auto& PT = RT.getProgramTable();

	boost::shared_lock<boost::upgrade_mutex> RdLock(RT.getPTLock());
	const auto It = PT.find(Program);

	// Found
	if (It != PT.end()) {
		// If its shadow is valid, use shadow
		if (It->second.ShadowProgram.get() != NULL)
			Program = It->second.ShadowProgram.get();
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
                      void* UserData) try {

	// FIXME: we can't handle clBuildProgram on the same program twice atm
	std::string Source;
	size_t SourceLength = 0;

	auto venGetProgramInfo = Lookup<OclAPI::clGetProgramInfo>();
	cl_int Ret = venGetProgramInfo(Program, CL_PROGRAM_SOURCE, 0, nullptr,
	                               &SourceLength);
	OCL_ASSERT(Ret);

	// Must be greater than zero because the size includes null terminator
	INTER_ASSERT(SourceLength > 0, "clGetProgramInfo returned zero source length");

	// If the program is created via clCreateProgramWithBinary or is a built-in
	// kernel, it returns a null string
	if (SourceLength == 1) {
		// TODO: we may want to support this?
		INTER_ASSERT(false, "build program from binary is yet impl'd");
		}

	// Retrieve the source to instrument and remove the null terminator
	Source.resize(SourceLength, '\0');
	Ret = venGetProgramInfo(Program, CL_PROGRAM_SOURCE, SourceLength,
	                        Source.data(), nullptr);
	OCL_ASSERT(Ret);
	Source.resize(SourceLength - 1);

	cl_context Context;

	// Retrieve the context to build a new program
	Ret = venGetProgramInfo(Program, CL_PROGRAM_CONTEXT, sizeof(Context),
	                        &Context, nullptr);
	OCL_ASSERT(Ret);

	auto& RT = getRuntimeKeeper();
	auto& PT = RT.getProgramTable();
	ProfileList PL;

	// Now invoke CLPKMCC
	if (!CLPKM::Compile(Source, Options, PL)) {

		// Build log is put in source
		ProgramInfo NewEntry(NULL, std::move(Source), ProfileList());

		boost::unique_lock<boost::upgrade_mutex> Lock(RT.getPTLock());
		const auto It = PT.emplace(Program, std::move(NewEntry));

		// FIXME: this is possible when a program is built serveral times
		INTER_ASSERT(It.second, "insertion to program table didn't take place");

		return CL_BUILD_PROGRAM_FAILURE;

		}

	const char* Ptr = Source.data();
	const size_t Len = Source.size();

	// Build a new program with instrumented code
	cl_program RawShadowProgram = Lookup<OclAPI::clCreateProgramWithSource>()(
			Context, 1, &Ptr, &Len, &Ret);
	OCL_ASSERT(Ret);

	clProgram ShadowProgram = RawShadowProgram;

	ProgramInfo NewEntry(std::move(ShadowProgram), std::string(), std::move(PL));

	boost::unique_lock<boost::upgrade_mutex> Lock(RT.getPTLock());
	const auto It = PT.emplace(Program, std::move(NewEntry));

	// FIXME: this is possible, if build a program serveral times
	INTER_ASSERT(It.second, "insertion to program table didn't take place");

	// Call the vendor's impl to build the instrumented code
	return Lookup<OclAPI::clBuildProgram>()(
			RawShadowProgram, NumOfDevice, DeviceList, Options, Notify, UserData);

	}
catch (const __ocl_error& OclError) {
	return OclError;
	}
catch (const std::bad_alloc& ) {
	return CL_OUT_OF_HOST_MEMORY;
	}

cl_kernel clCreateKernel(cl_program Program, const char* Name, cl_int* Ret) {

	auto& RT = getRuntimeKeeper();
	auto& PT = RT.getProgramTable();

	boost::shared_lock<boost::upgrade_mutex> RdLock(RT.getPTLock());
	const auto It = PT.find(Program);

	if (It == PT.end()) {
		if (Ret != nullptr)
			*Ret = CL_INVALID_PROGRAM;
		return NULL;
		}

	if (It->second.ShadowProgram.get() == NULL) {
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
			It->second.ShadowProgram.get(), Name, Ret);

	if (Kernel != NULL) {
		KernelInfo NewInfo(std::string(Name), &(*Pos),
		                   std::vector<cl_uint>(Pos->LocPtrParamIdx.size(), 0));
		boost::unique_lock<boost::upgrade_mutex> WrLock(RT.getKTLock());
		const auto& It = RT.getKernelTable().emplace(Kernel, std::move(NewInfo));
		INTER_ASSERT(It.second, "insertion to kernel table didn't table place");
		}

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

	boost::shared_lock<boost::upgrade_mutex> QTLock(RT.getQTLock());
	boost::shared_lock<boost::upgrade_mutex> KTLock(RT.getKTLock());
	const auto QueueEntry = QT.find(Queue);
	const auto KTEntry = KT.find(Kernel);

	// Step 0
	// Pre-check
	if (QueueEntry == QT.end())
		return CL_INVALID_COMMAND_QUEUE;

	if (KTEntry == KT.end())
		return CL_INVALID_KERNEL;

	if ((NumOfWaiting > 0 && !WaitingList) || (NumOfWaiting <= 0 && WaitingList))
		return CL_INVALID_EVENT_WAIT_LIST;

	// FIXME: Shall we also lock ProgramTable?
	auto& QueueInfo = QueueEntry->second;
	auto& Profile = *(KTEntry->second).Profile;
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

	size_t WorkGrpSize = NumOfThread / NumOfWorkGrp;

	// Calculate requested local buffer size, including statically and
	// dynamically sized local buffer
	const auto& DynLocSize = (KTEntry->second).DynLocSize;
	const size_t TotalReqLocSize = std::accumulate(DynLocSize.begin(),
	                                               DynLocSize.end(),
	                                               0) + Profile.ReqLocSize;
	size_t MetadataSize = (DynLocSize.size() + NumOfThread) * sizeof(cl_int);

	RT.Log(RuntimeKeeper::loglevel::INFO,
	       "\n==CLPKM== Launch kernel %p (%s)\n"
	       "==CLPKM==   metadata:  %s [4 Bytes x (%zu DSLB + %zu work-items)]\n"
	       "==CLPKM==   __private: %s [%zu Bytes x %zu work-items]\n"
	       "==CLPKM==   __local:   %s [%zu Bytes x %zu work-groups]\n",
	       Kernel, (KTEntry->second).Name.c_str(),
	       ToHumanReadable(MetadataSize).c_str(),
	       DynLocSize.size(), NumOfThread,
	       ToHumanReadable(Profile.ReqPrvSize * NumOfThread).c_str(),
	       Profile.ReqPrvSize, NumOfThread,
	       ToHumanReadable(TotalReqLocSize * NumOfWorkGrp).c_str(),
	       TotalReqLocSize, NumOfWorkGrp);

	// Step 2
	// Prepare header and live value buffers
	auto venCreateBuffer = Lookup<OclAPI::clCreateBuffer>();

	clMemObj DeviceMetadata = clMemObj(
			venCreateBuffer(QueueInfo.Context, CL_MEM_READ_WRITE,
			                MetadataSize, nullptr, &Ret));
	OCL_ASSERT(Ret);

	clMemObj LocalBuffer = clMemObj(
			TotalReqLocSize
			? venCreateBuffer(QueueInfo.Context, CL_MEM_READ_WRITE,
			                  TotalReqLocSize * NumOfWorkGrp,
			                  nullptr, &Ret)
			: NULL);
	OCL_ASSERT(Ret);

	clMemObj PrivateBuffer = clMemObj(
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
	std::vector<cl_int> HostMetadata(DynLocSize.size() + NumOfThread, 1);
	clEvent WriteMetadataEvent(NULL);

	// Prepare size info for dynamically sized local buffer
	for (size_t Idx = 0; Idx < DynLocSize.size(); ++Idx)
		HostMetadata[Idx] = reinterpret_cast<const cl_int&>(DynLocSize[Idx]);

	// Write metadata and put the event to the end of NewWaitingList
	Ret = venEnqWrBuf(QueueInfo.ShadowQueue.get(), DeviceMetadata.get(), CL_FALSE,
	                  0, MetadataSize, HostMetadata.data(), 0, nullptr,
	                  &WriteMetadataEvent.get());
	OCL_ASSERT(Ret);

	// Hold original waiting list, in addition to the event of writing header
	std::vector<cl_event> NewWaitingList(WaitingList, WaitingList + NumOfWaiting);

	NewWaitingList.emplace_back(WriteMetadataEvent.get());

	std::lock_guard<std::mutex> BlockerLock(*QueueInfo.BlockerMutex);

	if (QueueInfo.TaskBlocker.get() != NULL)
		NewWaitingList.emplace_back(QueueInfo.TaskBlocker.get());

	// Step 4
	// Set up additional CLPKM-related kernel arguments
	auto venSetKernelArg = Lookup<OclAPI::clSetKernelArg>();
	auto Idx = Profile.NumOfParam;

	cl_uint Threshold = RT.getCRThreshold();

	Ret = venSetKernelArg(Kernel, Idx++, sizeof(cl_mem), &DeviceMetadata.get());
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
	clEvent Final = Lookup<OclAPI::clCreateUserEvent>()(
			QueueInfo.Context, &Ret);
	OCL_ASSERT(Ret);

	Ret = Lookup<OclAPI::clRetainEvent>()(Final.get());
	INTER_ASSERT(Ret == CL_SUCCESS, "failed to retain user event");

	// New callback
	auto Work = std::make_unique<CallbackData>(
			QueueInfo.ShadowQueue.get(), Kernel, (KTEntry->second).Name, WorkDim,
			GWO ? std::vector<size_t>(GWO, GWO + WorkDim)
			    : std::vector<size_t>(WorkDim, 0),
			std::vector<size_t>(GWS, GWS + WorkDim),
			std::move(RealLWS), WorkGrpSize, std::move(DeviceMetadata),
			std::move(LocalBuffer), std::move(PrivateBuffer),
			std::move(HostMetadata), DynLocSize.size(),
			std::move(WriteMetadataEvent), Final.get(),
			std::chrono::high_resolution_clock::now());

	// This throws exception on error
	MetaEnqueue(Work.get(), NewWaitingList.size(), NewWaitingList.data());

	Ret = Lookup<OclAPI::clEnqueueMarkerWithWaitList>()(Queue, 1, &Final.get(),
	                                                    Event);
	OCL_ASSERT(Ret);

	if (QueueInfo.ShallReorder)
		QueueInfo.TaskBlocker = std::move(Final);

	// Prevent it from being released
	Work.release();

	return Ret;

	}
catch (const __ocl_error& OclError) {
	return OclError;
	}
catch (const std::bad_alloc& ) {
	return CL_OUT_OF_HOST_MEMORY;
	}

cl_int clReleaseKernel(cl_kernel Kernel) {

	auto& RT = getRuntimeKeeper();
	auto& KT = RT.getKernelTable();

	boost::upgrade_lock<boost::upgrade_mutex> RdLock(RT.getKTLock());
	const auto It = KT.find(Kernel);

	if (It == KT.end())
		return CL_INVALID_KERNEL;

	// Avoid from getting old reference count
	static std::mutex Mutex;
	std::lock_guard<std::mutex> Lock(Mutex);

	cl_uint RefCount = 0;

	cl_int Ret = Lookup<OclAPI::clGetKernelInfo>()(
			Kernel, CL_KERNEL_REFERENCE_COUNT, sizeof(cl_uint), &RefCount, nullptr);
	OCL_ASSERT(Ret);

	if (RefCount <= 1) {
		boost::upgrade_to_unique_lock<boost::upgrade_mutex> WrLock(RdLock);
		KT.erase(It);
		}

	return Lookup<OclAPI::clReleaseKernel>()(Kernel);

	}

cl_int clReleaseProgram(cl_program Program) try {

	auto venReleaseProgram = Lookup<OclAPI::clReleaseProgram>();

	auto& RT = getRuntimeKeeper();
	auto& PT = RT.getProgramTable();

	boost::upgrade_lock<boost::upgrade_mutex> RdLock(RT.getPTLock());
	const auto It = PT.find(Program);

	// Maybe a program never being built
	if (It == PT.end())
		return venReleaseProgram(Program);

	// Avoid from getting old reference count
	static std::mutex Mutex;
	std::lock_guard<std::mutex> Lock(Mutex);

	cl_uint RefCount = 0;

	cl_int Ret = Lookup<OclAPI::clGetProgramInfo>()(
		Program, CL_PROGRAM_REFERENCE_COUNT, sizeof(cl_uint), &RefCount, nullptr);
	OCL_ASSERT(Ret);

	if (RefCount <= 1) {
		boost::upgrade_to_unique_lock<boost::upgrade_mutex> WrLock(RdLock);
		PT.erase(It);
		}

	return venReleaseProgram(Program);

	}
catch (const __ocl_error& OclError) {
	return OclError;
	}

cl_int clSetKernelArg(cl_kernel Kernel, cl_uint ArgIndex, size_t ArgSize,
                      const void* ArgValue) {

	auto& RT = getRuntimeKeeper();
	auto& KT = RT.getKernelTable();

	boost::shared_lock<boost::upgrade_mutex> KTLock(RT.getKTLock());
	const auto& KTEntry = KT.find(Kernel);

	if (KTEntry == KT.end())
		return CL_INVALID_KERNEL;

	const auto& DynLocParams = (KTEntry->second).Profile->LocPtrParamIdx;

	// If the argument is a dynamically sized local buffer, ArgIndex should be in
	// DynLocParams
	// Note that CLPKMCC generate them in order and we can perform binary search
	const auto& It = std::lower_bound(DynLocParams.begin(),
	                                  DynLocParams.end(),
	                                  ArgIndex);

	cl_int Ret = Lookup<OclAPI::clSetKernelArg>()(Kernel, ArgIndex, ArgSize,
	                                              ArgValue);

	if (It == DynLocParams.end() || *It != ArgIndex || Ret != CL_SUCCESS)
		return Ret;

	// Pad to multiple of 4
	ArgSize = (ArgSize + 3) & ~static_cast<size_t>(0b11);

	// Only record the value on success
	size_t TblIdx = It - DynLocParams.begin();
	(KTEntry->second).DynLocSize[TblIdx] = ArgSize;

	INTER_ASSERT((KTEntry->second).DynLocSize[TblIdx] == ArgSize,
	             "cl_uint cannot hold %zu", ArgSize);

	return CL_SUCCESS;

	}

cl_int clEnqueueReadBuffer(cl_command_queue Queue,
                           cl_mem  Buffer,
                           cl_bool Blocking,
                           size_t  Offset,
                           size_t  Size,
                           void*   HostPtr,
                           cl_uint NumOfWaiting,
                           const cl_event* WaitingList,
                           cl_event* Event) try {

	// Invoke with new waiting list and pointer to return the event object
	auto ReadBuffer = [&](const cl_event* NewWaitingList, size_t NewNumOfWaiting,
	                      cl_event* AltEvent) -> cl_int {
		return Lookup<OclAPI::clEnqueueReadBuffer>()(
				Queue, Buffer, Blocking, Offset, Size, HostPtr, NewNumOfWaiting,
				NewWaitingList, AltEvent);
		};

	return Reorder(Queue, WaitingList, NumOfWaiting, Event, ReadBuffer);

	}
catch (const __ocl_error& OclError) {
	return OclError;
	}
catch (const std::bad_alloc& ) {
	return CL_OUT_OF_HOST_MEMORY;
	}

cl_int clEnqueueWriteBuffer(cl_command_queue Queue,
                            cl_mem  Buffer,
                            cl_bool Blocking,
                            size_t  Offset,
                            size_t  Size,
                            const void* HostPtr,
                            cl_uint NumOfWaiting,
                            const cl_event* WaitingList,
                            cl_event* Event) try {

	auto WriteBuffer = [&](const cl_event* NewWaitingList, size_t NewNumOfWaiting,
	                       cl_event* AltEvent) -> cl_int {
		return Lookup<OclAPI::clEnqueueWriteBuffer>()(
				Queue, Buffer, Blocking, Offset, Size, HostPtr, NewNumOfWaiting,
				NewWaitingList, AltEvent);
		};

	return Reorder(Queue, WaitingList, NumOfWaiting, Event, WriteBuffer);

	}
catch (const __ocl_error& OclError) {
	return OclError;
	}
catch (const std::bad_alloc& ) {
	return CL_OUT_OF_HOST_MEMORY;
	}

cl_int clEnqueueMarker(cl_command_queue Queue, cl_event* Event) try {

	auto EnqueueMarker = [&](const cl_event* WaitingList, size_t NumOfWaiting,
	                         cl_event* AltEvent) -> cl_int {
		return Lookup<OclAPI::clEnqueueMarkerWithWaitList>()(
				Queue, NumOfWaiting, WaitingList, AltEvent);
		};

	return Reorder(Queue, nullptr, 0, Event, EnqueueMarker);

	}
catch (const __ocl_error& OclError) {
	return OclError;
	}
catch (const std::bad_alloc& ) {
	return CL_OUT_OF_HOST_MEMORY;
	}

void* clEnqueueMapBuffer(cl_command_queue Queue,
                         cl_mem  Buffer,
                         cl_bool Blocking,
                         cl_map_flags MapFlags,
                         size_t  Offset,
                         size_t  Size,
                         cl_uint NumOfWaiting,
                         const cl_event* WaitingList,
                         cl_event* Event,
                         cl_int* ErrorCode) try {

	void* MapPtr = nullptr;

	auto MapBuffer = [&](const cl_event* NewWaitingList, size_t NewNumOfWaiting,
	                     cl_event* AltEvent) -> cl_int {
		cl_int Ret = CL_SUCCESS;
		MapPtr = Lookup<OclAPI::clEnqueueMapBuffer>()(
				Queue, Buffer, Blocking, MapFlags, Offset, Size, NewNumOfWaiting,
				NewWaitingList, AltEvent, &Ret);
		return Ret;
		};

	*ErrorCode = Reorder(Queue, WaitingList, NumOfWaiting, Event, MapBuffer);
	return MapPtr;

	}
catch (const __ocl_error& OclError) {
	*ErrorCode = OclError;
	return nullptr;
	}
catch (const std::bad_alloc& ) {
	*ErrorCode = CL_OUT_OF_HOST_MEMORY;
	return nullptr;
	}

cl_int clEnqueueUnmapMemObject(cl_command_queue Queue,
                               cl_mem  MemObj,
                               void*   MappedPtr,
                               cl_uint NumOfWaiting,
                               const cl_event* WaitingList,
                               cl_event* Event) try {

	auto UnmapMemObj = [&](const cl_event* NewWaitingList, size_t NewNumOfWaiting,
	                       cl_event* AltEvent) -> cl_int {
		return Lookup<OclAPI::clEnqueueUnmapMemObject>()(
				Queue, MemObj, MappedPtr, NewNumOfWaiting, NewWaitingList, AltEvent);
		};

	return Reorder(Queue, WaitingList, NumOfWaiting, Event, UnmapMemObj);

	}
catch (const __ocl_error& OclError) {
	return OclError;
	}
catch (const std::bad_alloc& ) {
	return CL_OUT_OF_HOST_MEMORY;
	}

cl_int clEnqueueWriteImage(cl_command_queue Queue,
                           cl_mem Image,
                           cl_bool Blocking,
                           const size_t Origin[3],
                           const size_t Region[3],
                           size_t InputRowPitch,
                           size_t InputSlicePitch,
                           const void* Ptr,
                           cl_uint NumOfWaiting,
                           const cl_event* WaitingList,
                           cl_event* Event) try {

	auto WriteImage = [&](const cl_event* NewWaitingList, size_t NewNumOfWaiting,
	                      cl_event* AltEvent) -> cl_int {
		return Lookup<OclAPI::clEnqueueWriteImage>()(
				Queue, Image, Blocking, Origin, Region, InputRowPitch,
				InputSlicePitch, Ptr, NewNumOfWaiting,
				NewWaitingList, AltEvent);
		};

	return Reorder(Queue, WaitingList, NumOfWaiting, Event, WriteImage);

	}
catch (const __ocl_error& OclError) {
	return OclError;
	}
catch (const std::bad_alloc& ) {
	return CL_OUT_OF_HOST_MEMORY;
	}

// TODO: reorder call to these functions
// clEnqueueReadBufferRect
// clEnqueueWriteBufferRect
// clEnqueueFillBuffer
// clEnqueueCopyBuffer
// clEnqueueCopyBufferRect
// clEnqueueReadImage
// clEnqueueFillImage
// clEnqueueCopyImage
// clEnqueueCopyImageToBuffer
// clEnqueueCopyBufferToImage
// clEnqueueMapImage
// clEnqueueMigrateMemObjects
// clEnqueueTask
// clEnqueueNativeKernel
// clEnqueueMarkerWithWaitList
// clEnqueueBarrierWithWaitList
// clEnqueueWaitForEvents
// clEnqueueBarrier

} // extern "C"
