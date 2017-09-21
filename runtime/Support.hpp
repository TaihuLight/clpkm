/*
  Support.hpp

  Support stuffs for Interface.cpp

*/

#ifndef __CLPKM__SUPPORT_HPP__
#define __CLPKM__SUPPORT_HPP__



#include <functional>
#include <string>
#include <vector>

#include <CL/opencl.h>



namespace CLPKM {

// Convert a size to human readable string, e.g. 1024 -> 1 KiB
std::string ToHumanReadable(size_t );

// Helper function to find work group size if the user didn't specify one
std::vector<size_t> FindWorkGroupSize(cl_kernel Kernel, cl_device_id Device,
                                      size_t WorkDim, size_t MaxDim,
                                      const size_t* WorkSize,
                                      cl_int* Ret);

using ReorderInvokee = std::function<cl_int(const cl_event*, size_t, cl_event*)>;

// Core logic of reorder, excluding locking QueueTable or so
cl_int ReorderCore(QueueInfo& QueueInfo, std::vector<cl_event>& WaitingList,
                   cl_event* Event, const ReorderInvokee& Func);

// Lock, producing a new waiting list, and call ReorderCore
cl_int Reorder(cl_command_queue OrigQueue, const cl_event* WaitingList,
               size_t NumOfWaiting, cl_event* Event, const ReorderInvokee& Func);

} // CLPKM



#endif
