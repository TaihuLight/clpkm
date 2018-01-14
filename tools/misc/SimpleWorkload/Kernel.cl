__kernel void Workload(uint Num) {

	uint foo = 0x01234567;

	for (uint i = 0; i < Num; ++i)
		foo ^= i;

	// Try to stop the compiler from optimizing out the loop
	*(volatile uint*) &foo ^= 100;

	}
