/*
  TaskKind.hpp

  Kinds of tasks that considered by CLPKM, and types for data interchange b/w
  the daemon and client

*/

#ifndef __CLPKM__TASK_KIND_HPP__
#define __CLPKM__TASK_KIND_HPP__

#include <cstdint>



namespace CLPKM {

// Helper used in this header
template <class T>
inline constexpr bool is_unsigned_integral_v =
		std::is_integral_v<T> && std::is_unsigned_v<T>;

// Each bit in task_bitmap refers to one type of task
using task_bitmap = uint32_t;
using task_kind_base = uint32_t;

// Kinds of GPU tasks
enum class task_kind : task_kind_base {
	COMPUTING = 0,
	MEMCPY,
	NUM_OF_TASK_KIND
	};

#define TASK_KIND_DBUS_TYPE_CODE   "u"
#define TASK_KIND_PRINTF_SPECIFIER "u"

#define TASK_BITMAP_DBUS_TYPE_CODE   "u"
#define TASK_BITMAP_PRINTF_SPECIFIER "u"

// Make sure things are still alright if we chang the typedef above
static_assert((sizeof(task_bitmap) << 3)
              >= static_cast<size_t>(task_kind::NUM_OF_TASK_KIND),
              "bitmap too small!");
static_assert(is_unsigned_integral_v<task_bitmap>,
              "task_bitmap is not an unsigned integral type!");
static_assert(is_unsigned_integral_v<task_kind_base>,
              "task_kind_base is not an unsigned integral type!");

// Helper function to quickly check if a number is 0
// If the number is not 0, it returns 1, 0 otherwise
template <class T>
inline T IsNotZero(T Num) {
	return ((Num | -Num) >> ((sizeof(T) << 3) - 1)) & 1;
	}

// Useful function to update task count and global bitmap
template <class T>
void UpdateGlobalBitmap(task_bitmap& GblMap, T& Count, size_t BitIdx,
                        task_bitmap OldProcMap, task_bitmap NewProcMap) {

	task_bitmap NewGblMap = GblMap;
	T NewCount = Count;

	task_bitmap BitMask = static_cast<task_bitmap>(1) << BitIdx;

	// Update the count of process running this kind of task
	NewCount -= (OldProcMap & BitMask) >> BitIdx;
	NewCount += (NewProcMap & BitMask) >> BitIdx;

	// Update the corresponding bit of this kind in the global bitmap
	NewGblMap ^= (-static_cast<task_bitmap>(IsNotZero(NewCount)) ^ NewGblMap)
	             & BitMask;

	GblMap = NewGblMap;
	Count = NewCount;

	}

} // namespace CLPKM



#endif
