/*
  TaskKind.hpp

  Kinds of tasks that considered by CLPKM, and types for data interchange b/w
  the daemon and client

*/

#ifndef __CLPKM__TASK_KIND_HPP__
#define __CLPKM__TASK_KIND_HPP__

#include <cstdint>



namespace CLPKM {

using task_kind_base = uint32_t;

// Kinds of GPU tasks
enum class task_kind : task_kind_base {
	COMPUTING = 0,
	MEMCPY,
	NUM_OF_TASK_KIND
	};

// Each bit in the bitmap refers to one type of task
using task_bitmap = uint32_t;

static_assert((sizeof(task_bitmap) << 3)
              >= static_cast<size_t>(task_kind::NUM_OF_TASK_KIND),
              "bitmap too small!");

#define TASK_KIND_DBUS_TYPE_CODE   "u"
#define TASK_KIND_PRINTF_SPECIFIER "u"

#define TASK_BITMAP_DBUS_TYPE_CODE   "u"
#define TASK_BITMAP_PRINTF_SPECIFIER "u"

template <class T>
T IsNotZero(T Num) {
	return ((Num | -Num) >> ((sizeof(T) << 3) - 1)) & 1;
	}

} // namespace CLPKM



#endif
