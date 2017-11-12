/*
  ResourceGuard.hpp

  Leveraging RAII to release resources

*/

#ifndef __CLPKM__RESOURCE_GUARD_HPP__
#define __CLPKM__RESOURCE_GUARD_HPP__

#include <systemd/sd-bus.h>
#include <type_traits>



namespace CLPKM {

template <class T>
struct Finalizer {
	T* Finalize(T* ) {
		// Stop the compiler from instantiating it early
		static_assert(!std::is_same<T, T>::value,
		              "the finalizor of requested type shall be specialized");
		// This is to suppress warning of returning nothing
		return nullptr;
		}
	};

template <>
struct Finalizer<sd_bus_message> {
	sd_bus_message* Finalize(sd_bus_message* Msg) {
		return sd_bus_message_unref(Msg);
		}
	};

template <>
struct Finalizer<sd_bus_slot> {
	sd_bus_slot* Finalize(sd_bus_slot* Slot) {
		return sd_bus_slot_unref(Slot);
		}
	};

template <>
struct Finalizer<sd_bus> {
	sd_bus* Finalize(sd_bus* Bus) {
		return sd_bus_flush_close_unref(Bus);
		}
	};

// A special RAII guard tailored for OpenCL stuff
// The inheritence is to enable empty base class optimization
template <class ResType>
class ResGuard : private Finalizer<ResType> {
public:
	ResGuard(ResType* R)
	: Resource(R) { }

	ResGuard(ResGuard&& RHS)
	: Resource(RHS.Resource) { RHS.Resource = nullptr; }

	~ResGuard() { Release(); }

	ResGuard& operator=(ResGuard&& RHS) {
		std::swap(Resource, RHS.Resource);
		return (*this);
		}

	ResType* Release(void) {
		ResType* Ret = nullptr;
		if (Resource) {
			Ret = Finalizer<ResType>::Finalize(Resource);
			Resource = nullptr;
			}
		return Ret;
		}

	ResType*& get(void) { return Resource; }

	bool operator!(void) const { return Resource; }

	ResGuard() = delete;
	ResGuard(const ResGuard& ) = delete;
	ResGuard& operator=(const ResGuard& ) = delete;

private:
	ResType* Resource;

	};

using sdBus = ResGuard<sd_bus>;
using sdBusMessage = ResGuard<sd_bus_message>;
using sdBusSlot = ResGuard<sd_bus_slot>;

} // namespace CLPKM



#endif
