/*
  toolkit.cl

  Helper functions for CLPKM generated code

*/

//
// Memory copy functions
//
void __clpkm_load_private(__global void * __lvb,
                          __private const void * __dst,
                          size_t __size) {
  __global uint * __w_lvb = (__global uint *) __lvb;
  __private uint * __w_dst = (__private uint *) __dst;
  while (__size >= 4) {
    * __w_dst++ = * __w_lvb++;
    __size -= 4;
    }
  __global uchar * __b_lvb = (__global uchar *) __w_lvb;
  __private uchar * __b_dst = (__private uchar *) __w_dst;
  switch (__size) {
  case 3:
    * __b_dst++ = * __b_lvb++;
  case 2:
    * __b_dst++ = * __b_lvb++;
  case 1:
    * __b_dst++ = * __b_lvb++;
  case 0:
    break;
  default:
    __builtin_unreachable();
  }
}
void __clpkm_store_private(__global void * __lvb,
                           __private const void * __src,
                           size_t __size) {
  __global uint * __w_lvb = (__global uint *) __lvb;
  __private uint * __w_src = (__private uint *) __src;
  while (__size >= 4) {
    * __w_lvb++ = * __w_src++;
    __size -= 4;
  }
  __global uchar * __b_lvb = (__global uchar *) __w_lvb;
  __private uchar * __b_src = (__private uchar *) __w_src;
  switch (__size) {
  case 3:
    * __b_lvb++ = * __b_src++;
  case 2:
    * __b_lvb++ = * __b_src++;
  case 1:
    * __b_lvb++ = * __b_src++;
  case 0:
    break;
  default:
    __builtin_unreachable();
  }
}
void __clpkm_store_local(__global void * __lvb, __local const void * __src,
                         size_t __size, size_t __loc_id, size_t __batch_size) {
  __global uint * __w_lvb = ((__global uint *) __lvb) + __loc_id;
  __local uint * __w_src = ((__local uint *) __src) + __loc_id;
  __local uchar * __b_last = ((__local uchar *) __src) + __size;
  // This loop keeps running until there are less than 4 bytes left
  while (((__local uchar *) __w_src) + 4 <= __b_last) {
    * __w_lvb = * __w_src;
    __w_lvb += __batch_size;
    __w_src += __batch_size;
  }
  __global uchar * __b_lvb = (__global uchar *) __w_lvb;
  __local uchar * __b_src = (__local uchar *) __w_src;
  if (__b_src < __b_last) {
    switch (__b_last - __b_src) {
    case 3:
      * __b_lvb++ = * __b_src++;
    case 2:
      * __b_lvb++ = * __b_src++;
    case 1:
      * __b_lvb++ = * __b_src++;
      break;
    default:
      __builtin_unreachable();
    }
  }
}

//
// Work related functions
//
void __get_linear_id(size_t * __global_id, size_t * __group_id,
                     size_t * __local_id, size_t * __group_size) {
  uint   __dim = get_work_dim();
  size_t __grp_id = 0; // group id
  size_t __loc_id = 0; // local id within a work group
  size_t __grp_sz = 1; // num of work-items in each work group
  while (__dim-- > 0) {
    __grp_id = __grp_id * get_num_groups(__dim) + get_group_id(__dim);
    __loc_id = __loc_id * get_local_size(__dim) + get_local_id(__dim);
    __grp_sz = __grp_sz * get_local_size(__dim);
  }
  * __global_id  = __grp_id * __grp_sz + __loc_id;
  * __group_id   = __grp_id;
  * __local_id   = __loc_id;
  * __group_size = __grp_sz;
}

//
// Others
//
#if 0
ulong clock64(void) {
  ulong clock_val;
  asm volatile ("mov.u64 %0, %%clock64;"
               : /* output */ "=l"(clock_val)
               : /* input */
               : /* clobbers */ "memory");
  return clock_val;
}
#endif
