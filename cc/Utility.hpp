/*
  Utility.hpp

  Some stuff that I dunno where to put

*/



#ifndef __CLPKM__UTILITY_HPP__
#define __CLPKM__UTILITY_HPP__

#include "llvm/ADT/StringRef.h"
#include <string>



namespace CLPKM {

inline llvm::StringRef getStringRef(llvm::StringRef StrRef) noexcept {
	return StrRef;
	}

template <size_t N>
constexpr llvm::StringRef getStringRef(const char (&StrLit)[N]) noexcept {
	return llvm::StringRef(StrLit, N - 1);
	}



template <class ... T>
std::string Concat(T&& ... S) {
	constexpr size_t NumOfStr = sizeof...(S);
	llvm::StringRef StrList[NumOfStr] = { getStringRef(S)... };
	size_t TotalSize = 0;

	for (const auto& Str : StrList)
		TotalSize += Str.size();

	std::string Result;
	Result.reserve(TotalSize);

	for (const auto& Str : StrList)
		Result.append(Str.data(), Str.size());

	return Result;
	}

} // namespace



#endif
