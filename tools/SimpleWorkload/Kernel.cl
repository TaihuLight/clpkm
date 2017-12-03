__kernel void Workload(uint Num) {
	uint foo = 0x01234567;
	for (uint i = 0; i < Num; ++i)
		foo ^= i;
	if (get_global_id(0) == 0 && get_global_id(1) == 0 && get_global_id(2) == 0)
		printf("%u\n", foo);
	}
