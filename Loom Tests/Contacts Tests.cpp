#include "doctest.h"

#include "Contacts.h"

#include "glm/glm.hpp"
#include "glm/gtc/constants.hpp"

#include <cmath>
#include <vector>


namespace
{
	constexpr float HALF = 0.5f;

	// How far the upper box's edge sinks into the lower one's, and how near
	// straight up the normal between them has to be.
	constexpr float SINK = 0.01f;
	constexpr float UPRIGHT = 0.99f;

	// A unit cube at centre, turned an eighth of a turn about one world axis.
	Loom::Contacts::Shape Cube(const glm::vec3& centre, int about)
	{
		const float c = std::cos(glm::quarter_pi<float>());
		const float s = std::sin(glm::quarter_pi<float>());

		Loom::Contacts::Shape shape;
		shape.kind = Loom::Contacts::Shape::BOX;
		shape.box.center = centre;
		shape.box.half = glm::vec3(HALF);

		const int across = (about + 1) % Loom::Contacts::AXES;
		const int up = (about + 2) % Loom::Contacts::AXES;

		for (int axis = 0; axis < Loom::Contacts::AXES; axis++)
			shape.box.axes[axis] = glm::vec3(0.0f);

		shape.box.axes[about][about] = 1.0f;
		shape.box.axes[across][across] = c;
		shape.box.axes[across][up] = s;
		shape.box.axes[up][across] = -s;
		shape.box.axes[up][up] = c;

		return shape;
	};

	glm::vec3 NormalBetween(const Loom::Contacts::Shape& lower, const Loom::Contacts::Shape& upper)
	{
		std::vector<Loom::Contacts::Manifold> manifolds;
		Loom::Contacts::Collide(lower, upper, 0.0f, manifolds);

		REQUIRE(manifolds.size() == 1);

		return manifolds[0].normal;
	};
};

TEST_SUITE("Contacts")
{
	TEST_CASE("an edge resting across an edge is pushed up, off a box mirrored by its scale too")
	{
		constexpr int X = 0;
		constexpr int Z = 2;

		// The lower cube turned about x, so its top is an edge along x; the
		// upper turned about z, so its bottom is an edge along z, crossing it.
		const float reach = HALF * glm::root_two<float>();

		Loom::Contacts::Shape lower = Cube(glm::vec3(0.0f), X);
		const Loom::Contacts::Shape upper = Cube(glm::vec3(0.0f, reach + reach - SINK, 0.0f), Z);

		CHECK(NormalBetween(lower, upper).y > UPRIGHT);

		// The same cube, one axis the other way round.
		lower.box.axes[Z] = -lower.box.axes[Z];

		CHECK(NormalBetween(lower, upper).y > UPRIGHT);
	};
};
