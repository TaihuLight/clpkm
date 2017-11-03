/*
  Callback.cpp

  Callback function to set up remain works if it has yet finished

*/



#include "Callback.hpp"
#include "ErrorHandling.hpp"
#include <algorithm>

using namespace CLPKM;



namespace {

cl_uint getRefCount(cl_event Event) {

	cl_uint RefCount = 0;

	cl_int Ret = Lookup<OclAPI::clGetEventInfo>()(
			Event, CL_EVENT_REFERENCE_COUNT, sizeof(cl_uint), &RefCount, nullptr);
	OCL_ASSERT(Ret);

	return RefCount;

	}

cl_ulong getExecTime(cl_event Event) {

	cl_ulong Start = 0;
	cl_ulong End = 0;

	auto venGetEvProfInfo = Lookup<OclAPI::clGetEventProfilingInfo>();

	// CL_PROFILING_COMMAND_QUEUED
	// CL_PROFILING_COMMAND_SUBMIT

	cl_int Ret = venGetEvProfInfo(Event, CL_PROFILING_COMMAND_START,
	                              sizeof(cl_ulong), &Start, nullptr);
	OCL_ASSERT(Ret);

	Ret = venGetEvProfInfo(Event, CL_PROFILING_COMMAND_END, sizeof(cl_ulong),
	                       &End, nullptr);
	OCL_ASSERT(Ret);

	return (End - Start);

	}

void LogEventProfInfo(RuntimeKeeper& RT, cl_event Event) {

	// The timestamp is in nanosecs
	constexpr double ToMilli = 0.000001f;
	cl_ulong ExecTime = getExecTime(Event);

	RT.Log(RuntimeKeeper::loglevel::INFO,
	       "==CLPKM==   prev work run for %f ms\n",
	       ExecTime * ToMilli);

	}

// Return 0 if finished, 1 if yet finished, -1 if yet finished and require
// updating header
template <class I>
int UpdateHeader(I First, I Last, size_t WorkGrpSize) {
	int Progress = 0;

	while (First != Last) {
		// Rear is the margin of this work group
		I Rear = First + WorkGrpSize;
		INTER_ASSERT(Rear <= Last, "gws can't be perfectly divided by lws");
		// Tag stores the last barrier blocking this work group
		cl_int Tag = 0;
		size_t TagCount = 0;
		// Traverse all work items in this group
		for (I Front = First; Front < Rear; ++Front) {
			// This work-item has finished the task
			if (*Front == 0)
				continue;
			Progress |= 1;
			// Checkpoint'd due to exceeding time slice
			if (*Front > 0)
				continue;
			// If it's the first work-item in the group being blocked
			if (Tag == 0)
				Tag = *Front;
			// Check if some work-items are blocked by different barrier
			else
				INTER_ASSERT(Tag == *Front, "some threads reach different barrier!");
			++TagCount;
			}
		// If all work-items in the groups are being blocking by the same barrier,
		// let them pass
		if (TagCount == WorkGrpSize) {
			for (I Front = First; Front < Rear; ++Front)
				*Front = -Tag;
			Progress = -1;
			}
		First = Rear;
		}

	return Progress;
	}

void CallbackCleanup(CallbackData* Work) {

	KernelInfo& KInfo = *Work->KInfo;

	std::unique_lock<std::mutex> Lock(*KInfo.Mutex);

	KInfo.Pool.emplace_back(std::move(Work->Kernel));

	Lock.unlock();
	Lock.release();

	delete Work;

	}

}



void CLPKM::MetaEnqueue(CallbackData* Work, cl_uint NumWaiting,
                        cl_event* WaitingList) {

	clEvent EventRead(NULL);

	// Enqueue kernel and read data
	cl_int Ret = Lookup<OclAPI::clEnqueueNDRangeKernel>()(
			Work->Queue, Work->Kernel.get(), Work->WorkDim, Work->GWO.data(),
			Work->GWS.data(), Work->LWS.data(), NumWaiting, WaitingList,
			&Work->PrevWork[0].get());
	OCL_ASSERT(Ret);

	cl_int* HostHeader = Work->HostMetadata.data() + Work->HeaderOffset;
	const size_t HeaderSize = Work->HostMetadata.size() - Work->HeaderOffset;

	Ret = Lookup<OclAPI::clEnqueueReadBuffer>()(
		Work->Queue, Work->DeviceHeader.get(), CL_FALSE,
		Work->HeaderOffset * sizeof(cl_int),
		HeaderSize * sizeof(cl_int), HostHeader, 1,
		&Work->PrevWork[0].get(), &EventRead.get());
	OCL_ASSERT(Ret);

	// Set up callback to continue
	// Note: This could lead to a problem! clSetEventCallback is a blocking call
	//       under some systems, e.g. mesa. In such case, this call will only
	//       return when the event is ready
	Ret = Lookup<OclAPI::clSetEventCallback>()(
			EventRead.get(), CL_COMPLETE, ResumeOrFinish, Work);
	OCL_ASSERT(Ret);

	Ret = clFlush(Work->Queue);
	OCL_ASSERT(Ret);

	// If nothing went south, set to NULL so the guard won't release it
	EventRead.get() = NULL;

	}




void CL_CALLBACK CLPKM::ResumeOrFinish(cl_event Event, cl_int ExecStatus,
                                       void* UserData) try {

	auto* Work = static_cast<CallbackData*>(UserData);
	clEvent ThisEvent(Event);
	cl_int Ret = CL_SUCCESS;
	auto& RT = getRuntimeKeeper();

	// Update timestamp
	auto Now = std::chrono::high_resolution_clock::now();
	std::chrono::duration<double, std::milli> Interval = Now - Work->LastCall;

	RT.Log(RuntimeKeeper::loglevel::INFO,
	       "\n==CLPKM== Callback called on kernel %p (%s) run #%u\n"
	       "==CLPKM==   interval between last call %f ms\n",
	       Work->Kernel.get(), Work->KInfo->Profile->Name.c_str(),
	       ++Work->Counter,
	       Interval.count());
	Work->LastCall = Now;

	// Step 1
	// Check the status of associated run

	for (size_t Idx : {1, 0}) {
		if (Work->PrevWork[Idx].get() == NULL)
			continue;
		cl_int Status = CL_SUCCESS;
		Ret = Lookup<OclAPI::clGetEventInfo>()(
				Work->PrevWork[Idx].get(), CL_EVENT_COMMAND_EXECUTION_STATUS,
				sizeof(cl_int), &Status, nullptr);
		// Status of the call to clGetEventInfo
		OCL_ASSERT(Ret);
		// Status of the event associated to previous enqueued commands
		OCL_ASSERT(Status);
		// Log execution time
		LogEventProfInfo(RT, Work->PrevWork[Idx].get());
		// Release so MetaEnqueue can use the slot
		Work->PrevWork[Idx].Release();
		}
	// Status of the event associated to clEnqueueReadBuffer
	OCL_ASSERT(ExecStatus);
	// Log execution time
	LogEventProfInfo(RT, Event);

	// Step 2
	// Inspect header, summarizing progress
	int Progress = UpdateHeader(Work->HostMetadata.begin() + Work->HeaderOffset,
	                            Work->HostMetadata.end(), Work->WorkGrpSize);

	// If finished
	if (Progress == 0) {
		Ret = Lookup<OclAPI::clSetUserEventStatus>()(Work->Final.get(), CL_COMPLETE);
		// Note: if the call failed here, following commands are likely to get
		//       stuck forever...
		INTER_ASSERT(Ret == CL_SUCCESS, "failed to set user event status");
		CallbackCleanup(Work);
		return;
		}

	// Step 3
	// If yet finished, setting up following run
	cl_uint   NumOfWaiting = 0;
	cl_event* WaitingList = nullptr;

	// If need to update the header
	if (Progress < 0) {

		cl_int* HostHeader = Work->HostMetadata.data() + Work->HeaderOffset;
		const size_t HeaderSize = Work->HostMetadata.size() - Work->HeaderOffset;

		Ret = Lookup<OclAPI::clEnqueueWriteBuffer>()(
				Work->Queue, Work->DeviceHeader.get(), CL_FALSE,
				Work->HeaderOffset * sizeof(cl_int), HeaderSize * sizeof(cl_int),
				HostHeader, 0, nullptr, &Work->PrevWork[1].get());
		OCL_ASSERT(Ret);

		NumOfWaiting = 1;
		WaitingList = &Work->PrevWork[1].get();

		}

	MetaEnqueue(Work, NumOfWaiting, WaitingList);

	}
catch (const __ocl_error& OclError) {
	auto* Work = static_cast<CallbackData*>(UserData);
	cl_int Ret = Lookup<OclAPI::clSetUserEventStatus>()(
			Work->Final.get(), OclError);
	INTER_ASSERT(Ret == CL_SUCCESS, "failed to set user event status");
	CallbackCleanup(Work);
	}
