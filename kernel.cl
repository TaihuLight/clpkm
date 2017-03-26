__kernel void adder_2(__global const float* a, __global const float* b, __global float* result);

__kernel void adder_2(__global const float* a, __global const float* b, __global float* result)
{
  __local int foo[128];
  int idx = get_global_id(0);
  foo[idx] = idx;
  result[idx] = a[idx] + b[idx];
}
