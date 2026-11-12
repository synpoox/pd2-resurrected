// Cartographer layout: the generator's own random numbers. Pure integer code, so a host and a client (or any
// compiler) get the same stream from the same seed. No <random>, no floats.
#pragma once
#include <cstdint>

namespace cartographer::layout {

// splitmix64 as a stateless mixer: the output for input x. The retry seeds are splitmix64(seed ^ k).
inline uint64_t splitmix64(uint64_t x)
{
	uint64_t z = x + 0x9E3779B97F4A7C15ull;
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
	return z ^ (z >> 31);
}

// PCG32 (XSH-RR), seeded through splitmix64.
class Pcg32 {
public:
	explicit Pcg32(uint64_t seed)
	{
		const uint64_t s = splitmix64(seed);
		inc_ = (splitmix64(s) << 1) | 1u;
		state_ = 0;
		next();
		state_ += s;
		next();
	}

	uint32_t next()
	{
		const uint64_t old = state_;
		state_ = old * 6364136223846793005ull + inc_;
		const uint32_t xorshifted = static_cast<uint32_t>(((old >> 18) ^ old) >> 27);
		const uint32_t rot = static_cast<uint32_t>(old >> 59);
		return (xorshifted >> rot) | (xorshifted << ((0u - rot) & 31u));
	}

	// Uniform in [0, n), without modulo bias. n <= 1 gives 0 (and draws nothing).
	uint32_t below(uint32_t n)
	{
		if (n <= 1) return 0;
		const uint32_t threshold = (0u - n) % n;
		for (;;) {
			const uint32_t r = next();
			if (r >= threshold) return r % n;
		}
	}

	// Uniform in [lo, hi] (inclusive). hi <= lo gives lo.
	int range(int lo, int hi)
	{
		if (hi <= lo) return lo;
		return lo + static_cast<int>(below(static_cast<uint32_t>(hi - lo + 1)));
	}

	// True with probability p percent (p <= 0 never, p >= 100 always).
	bool percent(int p)
	{
		if (p <= 0) return false;
		if (p >= 100) return true;
		return static_cast<int>(below(100)) < p;
	}

private:
	uint64_t state_ = 0;
	uint64_t inc_ = 1;
};

} // namespace cartographer::layout
