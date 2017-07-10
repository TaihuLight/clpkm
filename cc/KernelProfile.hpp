/*
  KernelProfile.hpp

  Structure to store kernel profiles

*/

#ifndef __CLPKM__KERNEL_PROFILE_HPP__
#define __CLPKM__KERNEL_PROFILE_HPP__

#include "llvm/Support/YAMLTraits.h"
#include <cstddef>
#include <string>
#include <vector>



struct KernelProfile {

	std::string Name;
	unsigned    NumOfParam;
	size_t      ReqPrvSize;
	size_t      ReqLocSize;

	// Hope it won't be too long and the STL impl got SVO
	std::vector<unsigned> LocPtrParamIdx;

	KernelProfile() = default;

	KernelProfile(std::string&& N, unsigned NP)
	: Name(std::move(N)), NumOfParam(NP), ReqPrvSize(0), ReqLocSize(0) { }

	};

using ProfileList = std::vector<KernelProfile>;



template <>
struct llvm::yaml::MappingTraits<KernelProfile> {
	static void mapping(llvm::yaml::IO& Io, KernelProfile& KP) {
		Io.mapRequired("kernel", KP.Name);
		Io.mapRequired("num-of-param", KP.NumOfParam);
		Io.mapRequired("req-private",    KP.ReqPrvSize);
		Io.mapOptional("req-local",  KP.ReqLocSize, std::size_t(0));
		Io.mapOptional("loc-ptr-param-idx", KP.LocPtrParamIdx);
		}
	};



// For STL containers
template <
	template <class, class> class C,
	class T,
	class A
	>
struct llvm::yaml::SequenceTraits<C<T, A>> {

	static size_t size(llvm::yaml::IO& , C<T, A>& List) {
		return List.size();
		}

	static T& element(llvm::yaml::IO& , C<T, A>& List, std::size_t Index) {
		if(Index >= List.size())
			List.resize(Index + 1);
		return List[Index];
		}

	};



#endif
