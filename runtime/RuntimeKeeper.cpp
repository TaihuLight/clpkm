/*
  RuntimeKeeper.cpp

  Globals or so

*/



#include "RuntimeKeeper.hpp"
#include <cstdlib>
#include <sys/types.h>
#include <pwd.h>



namespace {

struct InitFromFile : public CLPKM::RuntimeKeeper::InitHelper {
	CLPKM::RuntimeKeeper::state_t operator()(CLPKM::RuntimeKeeper& RT) override {
		// yaml-cpp throws exception on error
		try {
			std::string ConfigPath;
			if (const char* Home = getenv("HOME"); Home != nullptr)
				ConfigPath = Home;
			else
				// FIXME: this is not thread-safe
				ConfigPath = getpwuid(getuid())->pw_dir;
			ConfigPath += "/.clpkmrc";
			YAML::Node Config = YAML::LoadFile(ConfigPath);
			// TODO: work with daemon via UNIX domain socket or so
			if (Config["compiler"])
				RT.setCompilerPath(Config["compiler"].as<std::string>());
			if (Config["threshold"])
				RT.setCRThreshold(Config["threshold"].as<CLPKM::tlv_t>());
			}
		catch (...) {
			return CLPKM::RuntimeKeeper::INVALID_CONFIG;
			}
		return CLPKM::RuntimeKeeper::SUCCEED;
		}
	};

}



// Customize initializer here if needed
CLPKM::RuntimeKeeper& CLPKM::getRuntimeKeeper(void) {
	InitFromFile Initializer;
	static RuntimeKeeper RT(Initializer);
	return RT;
	}
