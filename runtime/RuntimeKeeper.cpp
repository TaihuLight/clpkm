/*
  RuntimeKeeper.cpp

  Globals or so

*/



#include "RuntimeKeeper.hpp"
#include <cstdlib>
#include <cstring>

using namespace CLPKM;



// Override config if specified from environment variable
RuntimeKeeper::RuntimeKeeper() : Priority(LOW), LogLevel(FATAL) {

	if (const char* Fine = getenv("CLPKM_PRIORITY")) {
		if (!strcmp(Fine, "high"))
			Priority = HIGH;
		else if (strcmp(Fine, "low"))
			this->Log("==CLPKM== Unrecognised priority: \"%s\"\n", Fine);
		}

	if (const char* Level = getenv("CLPKM_LOGLEVEL")) {
		if (!strcmp(Level, "error"))
			LogLevel = ERROR;
		else if (!strcmp(Level, "info"))
			LogLevel = INFO;
		else if (!strcmp(Level, "debug"))
			LogLevel = DEBUG;
		else if (strcmp(Level, "fatal"))
			this->Log("==CLPKM== Unrecognised log level: \"%s\"\n", Level);
		}

	}



// Customize initializer here if needed
CLPKM::RuntimeKeeper& CLPKM::getRuntimeKeeper(void) {
	static RuntimeKeeper RT;
	return RT;
	}
