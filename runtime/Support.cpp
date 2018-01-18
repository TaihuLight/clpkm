/*
  Support.cpp

  ;)

*/

#include "ErrorHandling.hpp"
#include "ResourceGuard.hpp"
#include "RuntimeKeeper.hpp"
#include "Support.hpp"

using namespace CLPKM;



std::string CLPKM::ToHumanReadable(size_t S) {

	float  R = S;
	size_t U = 0;

	const char* UntTbl[] = {" B", " KiB", " MiB", "GiB", "TiB"};
	constexpr size_t UntTblSize = sizeof(UntTbl) / sizeof(char*);

	while (R > 1024.0f && U < UntTblSize) {
		R /= 1024.0f;
		U++;
		}

	return std::to_string(R) + UntTbl[U];

	}

std::vector<size_t> CLPKM::FindWorkGroupSize(cl_kernel Kernel,
                                             cl_device_id Device,
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

// Core logic of reorder, excluding locking QueueTable or so
cl_int CLPKM::ReorderCore(QueueInfo& QueueInfo,
                          std::vector<cl_event>& WaitingList,
                          cl_event* Event,
                          const ReorderInvokee& Func) {

	cl_int Ret = CL_SUCCESS;

	// Make changes to TaskBlocker atomic
	std::lock_guard<std::mutex> BlockerLock(*QueueInfo.BlockerMutex);

	// Append blocker
	if (QueueInfo.TaskBlocker.get() != NULL)
		WaitingList.emplace_back(QueueInfo.TaskBlocker.get());

	// If no need to reorder, return immediately and don't update TaskBlocker
	if (!QueueInfo.ShallReorder) {
		Ret = Func(WaitingList.size() ? WaitingList.data() : nullptr,
		           WaitingList.size(), Event);
		return Ret;
		}

	clEvent AltEvent = NULL;

	Ret = Func(WaitingList.size() ? WaitingList.data() : nullptr,
	           WaitingList.size(), &AltEvent.get());
	OCL_ASSERT(Ret);

	// Only on success, the API call returns a valid event object

	// Gotta increase reference count if it's shared with user
	if (Event != nullptr) {
		Ret = Lookup<OclAPI::clRetainEvent>()(AltEvent.get());
		INTER_ASSERT(Ret == CL_SUCCESS,
		             "failed to retain event object: %" PRId32, Ret);
		*Event = AltEvent.get();
		}

	// Update last command
	QueueInfo.TaskBlocker = std::move(AltEvent);
	return CL_SUCCESS;

	}

cl_int CLPKM::Reorder(cl_command_queue OrigQueue, const cl_event* WaitingList,
                      size_t NumOfWaiting, cl_event* Event,
                      const ReorderInvokee& Func) {

	if ((NumOfWaiting > 0 && !WaitingList) || (NumOfWaiting <= 0 && WaitingList))
		return CL_INVALID_EVENT_WAIT_LIST;

	auto& RT = getRuntimeKeeper();
	auto& QT = RT.getQueueTable();

	boost::shared_lock<boost::upgrade_mutex> QTLock(RT.getQTLock());
	const auto QTEntry = QT.find(OrigQueue);

	if (QTEntry == QT.end())
		return CL_INVALID_COMMAND_QUEUE;

	auto& QueueInfo = QTEntry->second;

	// Prepare new wait list for ReorderCore
	std::vector<cl_event> NewWaitingList(WaitingList, WaitingList + NumOfWaiting);

	return ReorderCore(QueueInfo, NewWaitingList, Event, Func);

	}
