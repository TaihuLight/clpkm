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
  __global char * __clpkm_lvb = (__global char *) __lvb;
  __private char * __clpkm_dst = (__private char *) __dst;
  while (__size-- > 0)
    * __clpkm_dst++ = * __clpkm_lvb++;
}
void __clpkm_store_private(__global void * __lvb,
                           __private const void * __src,
                           size_t __size) {
  __global char * __clpkm_lvb = (__global char *) __lvb;
  __private char * __clpkm_src = (__private char *) __src;
  while (__size-- > 0)
    * __clpkm_lvb++ = * __clpkm_src++;
}

//
// Work related functions
//
size_t __get_global_linear_id(void) {
  uint   __dim = get_work_dim();
  size_t __id = 0;
  while (__dim-- > 0)
    __id = __id * get_global_size(__dim) +
           get_global_id(__dim) - get_global_offset(__dim);
  return __id;
}
size_t __get_group_linear_id(void) {
  uint   __dim = get_work_dim();
  size_t __id = 0;
  while (__dim-- > 0)
    __id = __id * get_num_groups(__dim) + get_group_id(__dim);
  return __id;
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
