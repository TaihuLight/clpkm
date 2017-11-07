/*
  toolkit.cl

  Helper functions for CLPKM generated code

*/

//
// Memory copy functions
//
void __clpkm_load_private_align_4(__global void * __lvb,
                                  __private const void * __dst,
                                  size_t __size) {
  // OpenCL 1.2 §6.3.k: The sizeof operator yields the size (in bytes) of its
  // operand, including any padding bytes needed for alignment
  // So I think if the alignment is 4, the size must be multiple of 4
  __global uint * __w_lvb = (__global uint *) __lvb;
  __private uint * __w_dst = (__private uint *) __dst;
  while (__size >= 4) {
    * __w_dst++ = * __w_lvb++;
    __size -= 4;
  }
}
void __clpkm_store_private_align_4(__global void * __lvb,
                                   __private const void * __src,
                                   size_t __size) {
  __global uint * __w_lvb = (__global uint *) __lvb;
  __private uint * __w_src = (__private uint *) __src;
  while (__size >= 4) {
    * __w_lvb++ = * __w_src++;
    __size -= 4;
  }
}
void __clpkm_load_private_align_2(__global void * __lvb,
                                  __private const void * __dst,
                                  size_t __size) {
  __global ushort * __w_lvb = (__global ushort *) __lvb;
  __private ushort * __w_dst = (__private ushort *) __dst;
  while (__size >= 2) {
    * __w_dst++ = * __w_lvb++;
    __size -= 2;
  }
}
void __clpkm_store_private_align_2(__global void * __lvb,
                                   __private const void * __src,
                                   size_t __size) {
  __global ushort * __w_lvb = (__global ushort *) __lvb;
  __private ushort * __w_src = (__private ushort *) __src;
  while (__size >= 2) {
    * __w_lvb++ = * __w_src++;
    __size -= 2;
  }
}
void __clpkm_load_private(__global void * __lvb,
                          __private const void * __dst,
                          size_t __size) {
  __global uchar * __w_lvb = (__global uchar *) __lvb;
  __private uchar * __w_dst = (__private uchar *) __dst;
  while (__size > 0) {
    * __w_dst++ = * __w_lvb++;
    --__size;
  }
}
void __clpkm_store_private(__global void * __lvb,
                           __private const void * __src,
                           size_t __size) {
  __global uchar * __w_lvb = (__global uchar *) __lvb;
  __private uchar * __w_src = (__private uchar *) __src;
  while (__size > 0) {
    * __w_lvb++ = * __w_src++;
    --__size;
  }
}
// XXX: assumption: __local variables are always 4-aligned
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
// CR-related stuff
//
#if 1
ulong clock64(void) {
  ulong __clock_val;
  asm volatile ("mov.u64 %0, %%clock64;"
               : /* output */ "=l"(__clock_val)
               : /* input */
               : /* clobbers */ // "memory"
               );
  return __clock_val;
}
ulong clock32_lo(void) {
  uint __clock_val;
  asm volatile ("mov.u32 %0, %%clock;"
               : /* output */ "=r"(__clock_val)
               : /* input */
               : /* clobbers */ // "memory"
               );
  return __clock_val;
}
ulong clock32_hi(void) {
  uint __clock_val;
  asm volatile ("mov.u32 %0, %%clock_hi;"
               : /* output */ "=r"(__clock_val)
               : /* input */
               : /* clobbers */ // "memory"
               );
  return __clock_val;
}
#endif
void __clpkm_init_cost_ctr(uint * __cost_ctr, const uint __clpkm_tlv) {
  //* __cost_ctr = 0;
  * __cost_ctr = clock64() + __clpkm_tlv;
}
void __clpkm_update_ctr(uint * __cost_ctr, uint __esti_cost) {
  //* __cost_ctr += __esti_cost;
}
bool __clpkm_should_chkpnt(uint __cost_ctr, uint __clpkm_tlv) {
  //return __cost_ctr > __clpkm_tlv;
  return clock64() > __cost_ctr;
}
