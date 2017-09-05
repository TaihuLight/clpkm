/*
  RuntimeKeeper.cpp

  Globals or so

*/



#include "RuntimeKeeper.hpp"
#include <cstdlib>
#include <cstring>
#include <sys/types.h>
#include <pwd.h>



namespace {

struct DefaultLoader : public CLPKM::RuntimeKeeper::ConfigLoader {
	bool operator()(CLPKM::RuntimeKeeper& RT) const override {
		// Load global config from config file
		// yaml-cpp throws exception on error
		try {
			std::string ConfigPath = []() -> std::string {
				const char* Home = getenv("HOME");
				if (Home == nullptr)
					// FIXME: this is not thread-safe
					Home = getpwuid(getuid())->pw_dir;
				return std::string(Home) + "/.clpkmrc";
				}();
			YAML::Node Config = YAML::LoadFile(ConfigPath);
			// TODO: work with daemon via UNIX domain socket or so
			if (Config["compiler"])
				ConfigLoader::setCompilerPath(RT, Config["compiler"].as<std::string>());
			if (Config["threshold"])
				ConfigLoader::setCRThreshold(RT, Config["threshold"].as<CLPKM::tlv_t>());
			}
		catch (...) {
			return false;
			}

		// Override config if specified from environment variable
		if (const char* Fine = getenv("CLPKM_PRIORITY"))
			if (strcmp(Fine, "high") == 0)
				ConfigLoader::setPriority(RT, CLPKM::RuntimeKeeper::priority::HIGH);
		if (const char* LogLevel = getenv("CLPKM_LOGLEVEL")) {
			if (strcmp(LogLevel, "error") == 0)
				ConfigLoader::setLogLevel(RT, CLPKM::RuntimeKeeper::loglevel::ERROR);
			else if (strcmp(LogLevel, "info") == 0)
				ConfigLoader::setLogLevel(RT, CLPKM::RuntimeKeeper::loglevel::INFO);
			}

		return true;
		}
	};

}



// Customize initializer here if needed
CLPKM::RuntimeKeeper& CLPKM::getRuntimeKeeper(void) {
	DefaultLoader Loader;
	static RuntimeKeeper RT(Loader);
	return RT;
	}
