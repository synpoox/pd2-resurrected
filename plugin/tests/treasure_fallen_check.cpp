#include "treasure_fallen_movement.h"
#include <cstdio>

using namespace treasure_fallen;

int main() {
	int failures = 0;
	const auto check = [&](bool ok, const char* name) {
		if (!ok) { std::printf("FAIL %s\n", name); ++failures; }
	};
	uint32_t position = 0;
	int32_t stationary = 0;
	check(Track(position, stationary, 20, 30, 5) == Movement::Flee, "first position flees");
	for (int i = 1; i <= 3; ++i) {
		check(Track(position, stationary, 20, 30, 5) == Movement::Wander && stationary == i,
		      "first three unchanged observations wander");
	}
	check(Track(position, stationary, 20, 30, 10) == Movement::Wander && stationary == 4,
	      "distance ten does not Blink");
	check(Track(position, stationary, 20, 30, 9) == Movement::Blink && stationary == 0,
	      "nearby stationary recovery Blinks and resets");
	Track(position, stationary, 20, 30, 5);
	check(Track(position, stationary, 21, 30, 5) == Movement::Flee && stationary == 0,
	      "movement resets stationary count");
	check(Track(position, stationary, 30, 21, 5) == Movement::Wander,
	      "same product retains PD2 position comparison");
	stationary = INT32_MAX;
	check(Track(position, stationary, 30, 21, 10) == Movement::Wander && stationary == INT32_MAX,
	      "distant stationary counter cannot overflow");
	check(Track(position, stationary, 30, 21, 9) == Movement::Blink && stationary == 0,
	      "saturated counter still recovers nearby");
	for (uint32_t roll = 0; roll < 3; ++roll) {
		const int step = 2 + static_cast<int>(roll);
		check(BlinkCoordinate(20, 30, roll) == 20 + step, "Blink toward greater coordinate");
		check(BlinkCoordinate(20, 10, roll) == 20 - step, "Blink toward lesser coordinate");
		check(BlinkCoordinate(20, 20, roll) == 20 + step, "equal coordinate Blinks positive");
	}
	std::printf("Treasure Fallen movement: %d failures\n", failures);
	return failures ? 1 : 0;
}
