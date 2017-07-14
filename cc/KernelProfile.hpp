/*
  KernelProfile.hpp

  Structure to store kernel profiles

*/

#ifndef __CLPKM__KERNEL_PROFILE_HPP__
#define __CLPKM__KERNEL_PROFILE_HPP__

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

	KernelProfile()
	: Name(), NumOfParam(0), ReqPrvSize(0), ReqLocSize(0), LocPtrParamIdx() { }

	KernelProfile(std::string&& N, unsigned NP)
	: Name(std::move(N)), NumOfParam(NP), ReqPrvSize(0), ReqLocSize(0),
	  LocPtrParamIdx() { }

	struct Key {
		static inline char Name[] = "kernel";
		static inline char NumOfParam[] = "num-of-param";
		static inline char ReqPrvSize[] = "req-private";
		static inline char ReqLocSize[] = "req-local";
		static inline char LocPtrParamIdx[] = "loc-ptr-param-idx";
		};

	};

using ProfileList = std::vector<KernelProfile>;



#ifdef HAVE_LLVM
#include "llvm/Support/YAMLTraits.h"

template <>
struct llvm::yaml::MappingTraits<KernelProfile> {
	static void mapping(llvm::yaml::IO& Io, KernelProfile& KP) {
		Io.mapRequired(KernelProfile::Key::Name,       KP.Name);
		Io.mapRequired(KernelProfile::Key::NumOfParam, KP.NumOfParam);
		Io.mapRequired(KernelProfile::Key::ReqPrvSize, KP.ReqPrvSize);
		Io.mapOptional(KernelProfile::Key::ReqLocSize, KP.ReqLocSize, std::size_t(0));
		Io.mapOptional(KernelProfile::Key::LocPtrParamIdx, KP.LocPtrParamIdx);
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

#ifdef HAVE_YAMLCPP
#include "yaml-cpp/yaml.h"

template <>
struct YAML::convert<KernelProfile> {
	static bool decode(const YAML::Node& YNode, KernelProfile& KP) {
		// Required keys
		if (!YNode[KernelProfile::Key::Name] ||
		    !YNode[KernelProfile::Key::NumOfParam] ||
		    !YNode[KernelProfile::Key::ReqPrvSize])
			return false;
		KP.Name = YNode[KernelProfile::Key::Name].as<std::string>();
		KP.NumOfParam = YNode[KernelProfile::Key::NumOfParam].as<unsigned>();
		KP.ReqPrvSize = YNode[KernelProfile::Key::ReqPrvSize].as<size_t>();
		// Optional keys
		if (YNode[KernelProfile::Key::ReqLocSize])
			KP.ReqLocSize = YNode[KernelProfile::Key::ReqLocSize].as<size_t>();
		if (YNode[KernelProfile::Key::LocPtrParamIdx])
			// I really want to avoid deep copy here
			KP.LocPtrParamIdx = YNode[KernelProfile::Key::LocPtrParamIdx].as<std::vector<unsigned>>();
		return true;
		}
	};

#endif



#endif
