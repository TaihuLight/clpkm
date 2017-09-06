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
#include <mutex>
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

	QueueInfo NewInfo(Context, Device, std::move(ShadowQueue));

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
		INTER_ASSERT(false, "Build program from binary is yet impl'd");
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
		boost::unique_lock<boost::upgrade_mutex> WrLock(RT.getKTLock());
		const auto& It = RT.getKernelTable().emplace(Kernel, &(*Pos));
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
	const auto It = KT.find(Kernel);

	// Step 0
	// Pre-check
	if (QueueEntry == QT.end())
		return CL_INVALID_COMMAND_QUEUE;

	if (It == KT.end())
		return CL_INVALID_KERNEL;

	if ((NumOfWaiting > 0 && !WaitingList) || (NumOfWaiting <= 0 && WaitingList))
		return CL_INVALID_EVENT_WAIT_LIST;

	auto& QueueInfo = QueueEntry->second;
	auto& Profile = *(It->second).Profile;
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

	clMemObj DeviceHeader = clMemObj(
			venCreateBuffer(QueueInfo.Context, CL_MEM_READ_WRITE,
			                sizeof(cl_int) * NumOfThread, nullptr, &Ret));
	OCL_ASSERT(Ret);

	// TODO: runtime decided size
	clMemObj LocalBuffer = clMemObj(
			Profile.ReqLocSize > 0
			? venCreateBuffer(QueueInfo.Context, CL_MEM_READ_WRITE,
			                  Profile.ReqLocSize * NumOfWorkGrp,
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
	std::vector<cl_int> HostHeader(NumOfThread, 1);
	clEvent WriteHeaderEvent(NULL);

	// Write Header and put the event to the end of NewWaitingList
	Ret = venEnqWrBuf(QueueInfo.ShadowQueue.get(), DeviceHeader.get(), CL_FALSE,
	                  0, sizeof(cl_int) * NumOfThread, HostHeader.data(),
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
	auto Idx = Profile.NumOfParam;

	cl_uint Threshold = RT.getCRThreshold();

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
	clEvent Final = Lookup<OclAPI::clCreateUserEvent>()(
			QueueInfo.Context, &Ret);
	OCL_ASSERT(Ret);

	Ret = clEnqueueMarkerWithWaitList(Queue, 1, &Final.get(), Event);
	OCL_ASSERT(Ret);

	// New callback
	auto Work = std::make_unique<CallbackData>(
			QueueInfo.ShadowQueue.get(), Kernel, WorkDim,
			GWO ? std::vector<size_t>(GWO, GWO + WorkDim)
			    : std::vector<size_t>(WorkDim, 0),
			std::vector<size_t>(GWS, GWS + WorkDim),
			std::move(RealLWS), std::move(DeviceHeader),
			std::move(LocalBuffer), std::move(PrivateBuffer), std::move(HostHeader),
			std::move(WriteHeaderEvent), std::move(Final),
			std::chrono::high_resolution_clock::now());

	// This throws exception on error
	MetaEnqueue(Work.get(), NumOfWaiting + 1, NewWaitingList.data());

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

}
