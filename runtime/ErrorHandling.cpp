/*
  ErrorHandling.cpp

  Impl helper function that wraps strerror

*/

#include "ErrorHandling.hpp"
#include <cstring>



std::string StrError(int ErrorNum) {
	// FIXME: fixed size buffer
	char ErrorMsg[1024] = {};
#if (_POSIX_C_SOURCE >= 200112L || _XOPEN_SOURCE >= 600) && ! _GNU_SOURCE
	strerror_r(ErrorNum, ErrorMsg, sizeof(ErrorMsg));
	return ErrorMsg;
#else
	return strerror_r(ErrorNum, ErrorMsg, sizeof(ErrorMsg));
#endif
	}
