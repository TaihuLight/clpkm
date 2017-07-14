/*
  Interface.cpp

  OpenCL interface

*/



#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <dlfcn.h>
#include <CL/opencl.h>



extern "C" {

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

	// TODO: call CLPKMCC
	std::string InstrumentedSource;
	const char* Ptr = &InstrumentedSource[0];
	const size_t Len = InstrumentedSource.size();

	// Build a new program with instrumented code
	cl_program InstrumentedProgram =
			clCreateProgramWithSource(Context, 1, &Ptr, &Len, &Ret);

	if (Ret != CL_SUCCESS)
		return Ret;

	static auto Impl = reinterpret_cast<decltype(&clBuildProgram)>(
	                dlsym(RTLD_NEXT, "clBuildProgram"));

	// Call the vendor's impl to build the instrumented code
	Ret = Impl(InstrumentedProgram, NumOfDevice, DeviceList,
	           Options, Notify, UserData);

	if (Ret != CL_SUCCESS)
		return Ret;

	// Critical session
	// TODO: Update runtime info here!

	return Ret;

	}

cl_int clSetKernelArg(cl_kernel , cl_uint , size_t , const void* );

}
