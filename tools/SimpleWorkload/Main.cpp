#define CL_USE_DEPRECATED_OPENCL_1_2_APIS

#include <iostream>
#include <chrono>
#include <string>
#include <sstream>

#include <cassert>
#include <cstdlib>

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <CL/cl.h>



#define OCL_ASSERT(Ret) do { \
	cl_int __Ret = Ret; \
	if (__Ret != CL_SUCCESS) { \
		std::cerr << __FILE__ << ':' << __LINE__ << '(' << __func__ << ") got " \
		          << __Ret << std::endl; \
		std::abort(); \
		} \
	} while(0)


using Milli = std::chrono::duration<double, std::milli>;

template <class T>
inline Milli ToMilli(T D) {
	return std::chrono::duration_cast<Milli>(D);
	}



int main(int ArgCount, const char* ArgVar[]) {

	if (ArgCount != 4) {
		std::cerr << "Usage:\n"
		          << "\t \"" << ArgVar[0] << "\" <file> <kernel> <cl_uint>"
		          << std::endl;
		return -1;
		}

	cl_context       Context = nullptr;
	cl_platform_id   Platform = nullptr;
	cl_device_id     Device = nullptr;
	cl_command_queue Queue = nullptr;

	cl_program Program = nullptr;
	cl_kernel  Kernel = nullptr;

	std::string Source;

	{
		char Buffer [4096] = {};
		ssize_t NRead = 0;

		int Fd = open(ArgVar[1], O_RDONLY);
		assert (Fd != -1);

		while ((NRead = read(Fd, Buffer, sizeof(Buffer))) > 0)
			Source.append(Buffer, NRead);

		close(Fd);

		}

	cl_int Ret = 0;
	cl_uint NPlatform = 0;
	cl_uint NDevice = 0;

	Ret = clGetPlatformIDs(1, &Platform, &NPlatform);
	OCL_ASSERT(Ret);

	Ret = clGetDeviceIDs(Platform, CL_DEVICE_TYPE_DEFAULT, 1, &Device, &NDevice);
	OCL_ASSERT(Ret);

	std::cerr << "Creating context... " << std::flush;
	auto Start = std::chrono::high_resolution_clock::now();
	Context  = clCreateContext(nullptr, 1, &Device, nullptr, nullptr, &Ret);
	OCL_ASSERT(Ret);
	auto End = std::chrono::high_resolution_clock::now();
	std::cerr << "done.\t("
	          << ToMilli(End - Start).count() << " ms)"
	          << std::endl;

	std::cerr << "Creating command queue... " << std::flush;
	Start = std::chrono::high_resolution_clock::now();
	Queue = clCreateCommandQueue(Context, Device, 0, &Ret);
	OCL_ASSERT(Ret);
	End = std::chrono::high_resolution_clock::now();
	std::cerr << "done.\t("
	          << ToMilli(End - Start).count() << " ms)"
	          << std::endl;


	const char* SrcStr = Source.c_str();
	const size_t SrcSize = Source.size();

	Program = clCreateProgramWithSource(Context, 1, &SrcStr, &SrcSize, &Ret);
	OCL_ASSERT(Ret);

	std::cerr << "Building program... " << std::flush;
	Start = std::chrono::high_resolution_clock::now();
	Ret = clBuildProgram(Program, 1, &Device, "", nullptr, nullptr);
	if (Ret != CL_SUCCESS) {
		size_t LogSize = 0;
		Ret = clGetProgramBuildInfo(
				Program, Device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &LogSize);
		OCL_ASSERT(Ret);
		char* Log = new char [LogSize];
		Ret = clGetProgramBuildInfo(
				Program, Device, CL_PROGRAM_BUILD_LOG, LogSize, Log, nullptr);
		std::cerr << "failed!\nBuild log: \n"
		          << Log << std::endl;
		std::abort();
		}
	End = std::chrono::high_resolution_clock::now();
	std::cerr << "done.\t("
	          << ToMilli(End - Start).count() << " ms)"
	          << std::endl;

	Kernel = clCreateKernel(Program, ArgVar[2], &Ret);
	OCL_ASSERT(Ret);

	{
		std::stringstream SS;
		SS << ArgVar[3];
		cl_uint N = 0;
		SS >> N;
		assert(SS);

		Ret = clSetKernelArg(Kernel, 0, sizeof(cl_uint), &N);
		OCL_ASSERT(Ret);
		}

	const size_t GblWorkSize[3] = {128, 7, 64};

	std::cerr << "Running the kernel... " << std::flush;
	Start = std::chrono::high_resolution_clock::now();
	Ret = clEnqueueNDRangeKernel(
			Queue, Kernel, 3, nullptr, GblWorkSize, nullptr,
			0, nullptr, nullptr);
	OCL_ASSERT(Ret);

	Ret = clFinish(Queue);
	OCL_ASSERT(Ret);
	End = std::chrono::high_resolution_clock::now();
	std::cerr << "done.\t("
	          << ToMilli(End - Start).count() << " ms)"
	          << std::endl;

	Ret = clReleaseKernel(Kernel);
	OCL_ASSERT(Ret);
	Ret = clReleaseProgram(Program);
	OCL_ASSERT(Ret);
	Ret = clReleaseCommandQueue(Queue);
	OCL_ASSERT(Ret);
	Ret = clReleaseContext(Context);
	OCL_ASSERT(Ret);
	Ret = clReleaseDevice(Device);
	OCL_ASSERT(Ret);

	return 0;

	}
