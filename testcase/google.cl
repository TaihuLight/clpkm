__kernel void adder_2(__global const float* a, __global const float* b,
                      __global float* result);

__kernel void nyet();

__kernel void aaa();

__kernel void bbb() { aaa(); }

int __inc(int i) {
	return i+1;
	}

int inc(int i) {
  return __inc(i+1);
  }

__kernel void aaa() {}

__kernel void adder(__global const float* a, __global const float* b,
                    __global float* result) {
  {
    int a = 1;
    nyet();
  }
  for (int i = 0; i < 100; i++)
    result[i] = a[i] + b[i];
  if (1 > 1)
    ;
}
int blyat() {
  while (1) 1 + 1;

  return inc(9);
}

__kernel void nyet() { blyat(); }

__kernel void adder_2(__global const float* a, __global const float* b,
                      __global float* result) {
  __local int foo[128];
  int idx = get_global_id(0) + get_global_id(0);
  foo[idx] = idx;
  result[idx] = a[idx] + b[idx];

  int jh = (int)a[0];

  jh = blyat();
  blyat() + blyat();

  while (0) blyat();

  while (0) {
    blyat();
  }

  for (int i = ({
         int b = blyat();
         b;
       });
       i < 100; i++)
    nyet();

  while (blyat() + blyat()) blyat();

  inc(inc(0) + inc(1));

  {
    int a, b, c, jh;
    jh = a + b + c;
  }
  adder(a, b, result);
}
