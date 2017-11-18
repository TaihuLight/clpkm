/*
  CompilerDriver.cpp

  Driver to drive CLPKMCC

*/

#include "CompilerDriver.hpp"
#include "ErrorHandling.hpp"
#include "ScheduleService.hpp"

#include <cstring>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>



namespace {
	bool CloseFd(int& Fd) {
		bool Succeed = true;
		if (Fd != -1) {
			Succeed = (close(Fd) == 0);
			Fd = -1;
			}
		return Succeed;
		}
	ssize_t FullWrite(int Fd, const void* Buffer, size_t Size) {
		const char* CharBuf = static_cast<const char*>(Buffer);
		size_t Offset = 0;
		ssize_t Ret = 0;
		while (Offset < Size) {
			Ret = write(Fd, CharBuf + Offset, Size - Offset);
			if (Ret >= 0)
				Offset += Ret;
			else if (errno != EINTR)
				return Ret;
			}
		return Offset;
		}
}



bool CLPKM::Compile(std::string& Source, const char* Options, ProfileList& PL) {
	using pipe_t = int[2];

	pid_t Pid = 0;
	pipe_t SrcPipe = {-1, -1}, OutPipe = {-1, -1}, YamlPipe = {-1, -1};
	std::string Out, Yaml;

	auto Cleanup = [&](bool Succeed = false) -> bool {
		Succeed &= CloseFd(SrcPipe[0]);
		Succeed &= CloseFd(SrcPipe[1]);
		Succeed &= CloseFd(OutPipe[0]);
		Succeed &= CloseFd(OutPipe[1]);
		Succeed &= CloseFd(YamlPipe[0]);
		Succeed &= CloseFd(YamlPipe[1]);
		if (Pid != 0)
			kill(Pid, SIGTERM);
		if (!Succeed)
			Source = StrError(errno);
		return Succeed;
		};

	if (pipe(SrcPipe) || pipe(OutPipe) || pipe(YamlPipe))
		return Cleanup();

	Pid = fork();

	if (Pid == -1)
		return Cleanup();

	// Child: invoke the compiler
	if (Pid == 0) {

		std::string ErrorMsg;

		if (dup2(SrcPipe[0], STDIN_FILENO) == -1 ||
		    dup2(OutPipe[1], STDOUT_FILENO) == -1 ||
		    dup2(YamlPipe[1], STDERR_FILENO) == -1)
			ErrorMsg = StrError(errno);
		else if (!CloseFd(SrcPipe[0]) || !CloseFd(SrcPipe[1]) ||
		         !CloseFd(OutPipe[0]) || !CloseFd(OutPipe[1]) ||
		         !CloseFd(YamlPipe[0]) || !CloseFd(YamlPipe[1]))
			ErrorMsg = StrError(errno);
		// It won't return unless something went south
		else if (execlp(CLPKM::getScheduleService().getCompilerPath().c_str(),
		                CLPKM::getScheduleService().getCompilerPath().c_str(),
		                Options, NULL) == -1)
			ErrorMsg = StrError(errno);

		FullWrite(STDERR_FILENO, ErrorMsg.data(), ErrorMsg.size());
		exit(-1);

		}

	// Parent continues here
	if (!(CloseFd(SrcPipe[0]) && CloseFd(OutPipe[1]) && CloseFd(YamlPipe[1])))
		return Cleanup();

	// Write original source
	ssize_t Ret = FullWrite(SrcPipe[1], Source.data(), Source.size());

	if (!CloseFd(SrcPipe[1]) || Ret == -1)
		return Cleanup();

	Source.clear();

	char Buffer[1024];

	// Read instrumented code
	do {
		Ret = read(OutPipe[0], Buffer, sizeof(Buffer));
		} while (Ret > 0 && Out.append(Buffer, Ret).size());

	if (!CloseFd(OutPipe[0]) || Ret == -1)
		return Cleanup();

	// Read kernel profile list
	do {
		Ret = read(YamlPipe[0], Buffer, sizeof(Buffer));
		} while (Ret > 0 && Yaml.append(Buffer, Ret).size());

	if (!CloseFd(YamlPipe[0]) || Ret == -1)
		return Cleanup();

	int ChildStatus = 0;

	if (waitpid(Pid, &ChildStatus, 0) == -1 || WEXITSTATUS(ChildStatus) != 0) {
		Source = std::move(Yaml);
		Pid = 0;
		Cleanup(true);
		return false;
		}

	Pid = 0;

	// Locate YAML beginning
	// The loader might emit something like "no version information available"
	if (size_t StartPos = Yaml.find("---\n"); StartPos != std::string::npos)
		Yaml.erase(0, StartPos);

	// Locate YAML end
	if (size_t EndPos = Yaml.find("\n..."); EndPos != std::string::npos)
		Yaml.erase(EndPos + 4);

	// YAML-CPP throws exception on error
	try {
		YAML::Node KP = YAML::Load(Yaml);
		PL = KP.as<ProfileList>();
		}
	catch (const YAML::Exception& YE) {
		Source = std::string("Invalid kernel profile: ") + YE.what();
		Cleanup(true);
		return false;
		}
	catch (const std::exception& SE) {
		Source = std::string("Caught standard exception: ") + SE.what();
		Cleanup(true);
		return false;
		}

	Source = std::move(Out);
	return Cleanup(true);

	}
