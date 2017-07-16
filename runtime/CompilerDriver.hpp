/*
  CompilerDriver.hpp

  Driver for CLPKMCC

*/

#ifndef __CLPKM__COMPILER_DRIVER_HPP__
#define __CLPKM__COMPILER_DRIVER_HPP__



#include "KernelProfile.hpp"
#include "RuntimeKeeper.hpp"

#include <cstdio>
#include <string>

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>



namespace CLPKM {

	std::string Compile(const char* Source, size_t Length, const char* Options,
	                    ProfileList& PL) {
		int Fd = open("/tmp/cc.cl", O_WRONLY | O_CREAT, 0600);
		assert(Fd > 0);
		write(Fd, Source, Length);
		close(Fd);
		FILE* CC = popen("env LD_LIBRARY_PATH=$(realpath ~/llvm-4.0.1-dbg/lib) "
		                 "~/CLPKM/cc/clpkmcc /tmp/cc.cl "
		                 "  --profile-output=/tmp/kp.yaml"
		                 "  -- -include clc/clc.h -std=cl1.2 > /tmp/cco.cl &&"
		                 "  cat /tmp/cco.cl", "r");
		std::string New;
		char Buff[4096];
		size_t Pos;
		while ((Pos = fread(Buff, 1, 4095, CC)) > 0)
			Buff[Pos] = '\0', New += Buff;
		if (pclose(CC) != 0)
			return std::string();
		YAML::Node config = YAML::LoadFile("/tmp/kp.yaml");
		PL = config.as<ProfileList>();
//		unlink("/tmp/cc.cl");
//		unlink("/tmp/kp.yaml");
		return New;
		}

}



#endif
