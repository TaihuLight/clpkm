/*
  RuntimeKeeper.cpp

  Globals or so

*/



#include "RuntimeKeeper.hpp"
#include <cstdlib>
#include <cstring>

using namespace CLPKM;



// Override config if specified from environment variable
RuntimeKeeper::RuntimeKeeper() : LogLevel(loglevel::FATAL) {
	if (const char* Level = getenv("CLPKM_LOGLEVEL")) {
		if (!strcmp(Level, "error"))
			LogLevel = loglevel::ERROR;
		else if (!strcmp(Level, "info"))
			LogLevel = loglevel::INFO;
		else if (!strcmp(Level, "debug"))
			LogLevel = loglevel::DEBUG;
		else if (strcmp(Level, "fatal"))
			this->Log("==CLPKM== Unrecognised log level: \"%s\"\n", Level);
		}
	}



// Customize initializer here if needed
CLPKM::RuntimeKeeper& CLPKM::getRuntimeKeeper(void) {
	static RuntimeKeeper RT;
	return RT;
	}
