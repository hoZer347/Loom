#include "doctest.h"

#include "Utilities/SpeedManager.h"

#include "glm/glm.hpp"

using Loom::SpeedManager;


TEST_SUITE("SpeedManager")
{
	TEST_CASE("ClampMagnitude leaves a short vector alone")
	{
		const glm::vec3 shorter(0.3f, 0.4f, 0.0f);

		CHECK(SpeedManager::ClampMagnitude(shorter, 1.0f) == shorter);
	};

	TEST_CASE("ClampMagnitude shortens a long vector without turning it")
	{
		constexpr float MAX_LENGTH = 1.0f;

		const glm::vec3 clamped = SpeedManager::ClampMagnitude(glm::vec3(3.0f, 4.0f, 0.0f), MAX_LENGTH);

		CHECK(glm::length(clamped) == doctest::Approx(MAX_LENGTH));
		CHECK(clamped.x == doctest::Approx(0.6f));
		CHECK(clamped.y == doctest::Approx(0.8f));
	};

	TEST_CASE("ClampMagnitude of a zero vector is zero")
	{
		CHECK(SpeedManager::ClampMagnitude(glm::vec3(0.0f), 1.0f) == glm::vec3(0.0f));
	};

	TEST_CASE("MoveTowards steps by at most max_delta along the difference")
	{
		const glm::vec3 moved = SpeedManager::MoveTowards(glm::vec3(0.0f), glm::vec3(10.0f, 0.0f, 0.0f), 2.0f);

		CHECK(moved.x == doctest::Approx(2.0f));
		CHECK(moved.y == doctest::Approx(0.0f));
		CHECK(moved.z == doctest::Approx(0.0f));
	};

	TEST_CASE("MoveTowards never overshoots the target")
	{
		const glm::vec3 target(1.0f, 1.0f, 0.0f);

		CHECK(SpeedManager::MoveTowards(glm::vec3(0.0f), target, 100.0f) == target);
	};

	TEST_CASE("MoveTowards with nothing to cover stays put")
	{
		const glm::vec3 here(2.0f, -1.0f, 5.0f);

		CHECK(SpeedManager::MoveTowards(here, here, 0.0f) == here);
	};
};
