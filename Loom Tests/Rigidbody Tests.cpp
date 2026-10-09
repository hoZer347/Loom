#include "doctest.h"

#include "Test Support.h"

#include "Collider.h"
#include "Collision.h"
#include "ComponentRegistry.h"
#include "Mesh.h"
#include "MeshCollider.h"
#include "Physics.h"
#include "Rigidbody.h"
#include "Scene.h"

#include "glm/glm.hpp"

#include <string>
#include <vector>

using LoomTests::Pump;


namespace
{
	// Steps a second takes, and how close a resting body has to settle.
	const int SECOND = (int)std::round(1.0f / Loom::Physics::FixedDeltaTime());
	constexpr float REST = 0.03f;

	constexpr float FLOOR_SIZE = 40.0f;
	constexpr float HALF = 0.5f;

	// Slower than this counts as still; the share a speed may be off by.
	constexpr float STILL = 0.1f;
	constexpr float CLOSE = 0.01f;

	float Gravity()
	{
		return -Loom::Physics::GetGravity().y;
	};

	// Steps the scene's physics for seconds.
	void Run(Loom::Scene& scene, float seconds)
	{
		for (int step = 0; step < (int)(seconds * SECOND); step++)
			scene.Physics();
	};

	Loom::GameObject* Spawn(Loom::Scene& scene, const std::string& name, const glm::vec3& position, Loom::GameObject* parent = nullptr)
	{
		Loom::GameObject* object = parent ? parent->AddChild(name) : scene.AddChild(name);
		Pump();

		object->transform.position = position;

		return object;
	};

	// A static box whose top is at y = 0.
	void Floor(Loom::Scene& scene)
	{
		Loom::GameObject* floor = Spawn(scene, "Floor", glm::vec3(0.0f, -0.5f, 0.0f));
		floor->Attach<Loom::BoxCollider>()->size = glm::vec3(FLOOR_SIZE, 1.0f, FLOOR_SIZE);
		Pump();
	};

	template <typename Shape>
	Loom::Rigidbody* Body(Loom::Scene& scene, const glm::vec3& position, Shape** shape = nullptr)
	{
		Loom::GameObject* object = Spawn(scene, "Body", position);

		Shape* collider = object->Attach<Shape>();
		Loom::Rigidbody* body = object->Attach<Loom::Rigidbody>();
		Pump();

		if (shape)
			*shape = collider;

		return body;
	};

	glm::vec3 Position(const Loom::Rigidbody& body)
	{
		return glm::vec3(body.GetGameObject()->WorldMatrix()[3]);
	};

	// Records what it is told about collisions and triggers.
	struct Toucher final : Loom::Component<Toucher>
	{
		void OnCollisionEnter(const Loom::Collision& collision) override { Record("enter", collision); };
		void OnCollisionStay(const Loom::Collision& collision) override { Record("stay", collision); };
		void OnCollisionExit(const Loom::Collision& collision) override { Record("exit", collision); };

		void OnTriggerEnter(Loom::Collider&) override { events.push_back("trigger enter"); };
		void OnTriggerStay(Loom::Collider&) override { events.push_back("trigger stay"); };
		void OnTriggerExit(Loom::Collider&) override { events.push_back("trigger exit"); };

		size_t Count(const std::string& event) const
		{
			return (size_t)std::count(events.begin(), events.end(), event);
		};

		std::vector<std::string> events{ };
		std::vector<Loom::Collision> collisions{ };

	private:
		void Record(const std::string& event, const Loom::Collision& collision)
		{
			events.push_back(event);
			collisions.push_back(collision);
		};
	};
};


TEST_SUITE("Rigidbody")
{
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a rigidbody is a component the editor can add by name")
	{
		Loom::Scene scene("rigidbody by name");
		Pump();

		Loom::ComponentBase* created = Loom::ComponentRegistry::Create("Rigidbody", scene.GetRoot());
		Pump();

		CHECK(dynamic_cast<Loom::Rigidbody*>(created) != nullptr);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a body falls under gravity, and a kinematic one does not")
	{
		Loom::Scene scene("falling");
		Pump();

		Loom::GameObject* object = Spawn(scene, "Falling", glm::vec3(0.0f));
		Loom::Rigidbody* falling = object->Attach<Loom::Rigidbody>();

		Loom::GameObject* held = Spawn(scene, "Held", glm::vec3(5.0f, 0.0f, 0.0f));
		Loom::Rigidbody* kinematic = held->Attach<Loom::Rigidbody>();
		kinematic->isKinematic = true;
		Pump();

		Run(scene, 1.0f);

		// Half g t squared over the second, give or take the step's own error.
		constexpr float STEPPING = 0.03f;

		CHECK(Position(*falling).y == doctest::Approx(-HALF * Gravity()).epsilon(STEPPING));
		CHECK(falling->velocity.y == doctest::Approx(-Gravity()).epsilon(CLOSE));

		CHECK(Position(*kinematic) == glm::vec3(5.0f, 0.0f, 0.0f));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a sphere comes to rest on a box, and a tumbling box on one of its faces")
	{
		Loom::Scene scene("resting");
		Pump();

		Floor(scene);

		Loom::Rigidbody* sphere = Body<Loom::SphereCollider>(scene, glm::vec3(0.0f, 3.0f, 0.0f));
		Loom::Rigidbody* box = Body<Loom::BoxCollider>(scene, glm::vec3(4.0f, 3.0f, 0.0f));
		scene.GetRoot().GetChildren().back()->transform.rotation = glm::vec3(20.0f, 30.0f, 40.0f);

		Run(scene, 4.0f);

		CHECK(Position(*sphere).y == doctest::Approx(HALF).epsilon(REST));
		CHECK(Position(*box).y == doctest::Approx(HALF).epsilon(REST));
		CHECK(glm::length(sphere->velocity) < STILL);
		CHECK(glm::length(box->velocity) < STILL);
		CHECK(box->IsSleeping());
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a stack of boxes stands")
	{
		constexpr int HEIGHT = 5;

		Loom::Scene scene("stack");
		Pump();

		Floor(scene);

		std::vector<Loom::Rigidbody*> boxes{ };

		for (int i = 0; i < HEIGHT; i++)
			boxes.push_back(Body<Loom::BoxCollider>(scene, glm::vec3(0.0f, 0.5f + i * 1.01f, 0.0f)));

		Run(scene, 5.0f);

		const glm::vec3 top = Position(*boxes.back());
		// How far the top may have slid off the middle.
		constexpr float DRIFT = 0.05f;

		CHECK(top.y == doctest::Approx(HEIGHT - HALF).epsilon(REST));
		CHECK(std::abs(top.x) < DRIFT);
		CHECK(std::abs(top.z) < DRIFT);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a bouncy ball bounces back nearly as high")
	{
		Loom::Scene scene("bounce");
		Pump();

		Floor(scene);

		constexpr float DROP = 3.0f;

		// The share of the drop it has to climb back, all but what the step
		// and the slop lose.
		constexpr float KEPT = 0.8f;

		Loom::Rigidbody* ball = Body<Loom::SphereCollider>(scene, glm::vec3(0.0f, DROP, 0.0f));
		ball->bounciness = 1.0f;

		float highest = 0.0f;
		bool bounced = false;

		for (int step = 0; step < 2 * SECOND; step++)
		{
			scene.Physics();

			bounced |= ball->velocity.y > 0.0f;

			if (bounced)
				highest = std::max(highest, Position(*ball).y);
		};

		CHECK(highest > DROP * KEPT);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "equal balls meeting head on trade velocities when perfectly bouncy")
	{
		Loom::Scene scene("head on");
		Pump();

		const glm::vec3 gravity = Loom::Physics::GetGravity();
		Loom::Physics::SetGravity(glm::vec3(0.0f));

		Loom::Rigidbody* moving = Body<Loom::SphereCollider>(scene, glm::vec3(-2.0f, 0.0f, 0.0f));
		Loom::Rigidbody* still = Body<Loom::SphereCollider>(scene, glm::vec3(0.0f, 0.0f, 0.0f));

		for (Loom::Rigidbody* body : { moving, still })
			body->bounciness = 1.0f;

		constexpr float SPEED = 3.0f;
		constexpr float TRADED = 0.07f;

		moving->velocity = glm::vec3(SPEED, 0.0f, 0.0f);

		Run(scene, 1.0f);

		Loom::Physics::SetGravity(gravity);

		CHECK(std::abs(moving->velocity.x) < SPEED * TRADED);
		CHECK(still->velocity.x == doctest::Approx(SPEED).epsilon(TRADED));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "friction stops a sliding box, and none lets it slide on")
	{
		Loom::Scene scene("sliding");
		Pump();

		Floor(scene);

		Loom::Rigidbody* rough = Body<Loom::BoxCollider>(scene, glm::vec3(0.0f, 0.5f, 0.0f));
		Loom::Rigidbody* smooth = Body<Loom::BoxCollider>(scene, glm::vec3(0.0f, 0.5f, 5.0f));
		smooth->friction = 0.0f;

		constexpr float SPEED = 5.0f;

		// Friction is applied once a step, so it stops a little off the sum.
		constexpr float STEPPING = 0.15f;

		for (Loom::Rigidbody* body : { rough, smooth })
			body->velocity = glm::vec3(SPEED, 0.0f, 0.0f);

		Run(scene, 2.0f);

		// v squared over twice the friction's deceleration.
		const float stoppingDistance = SPEED * SPEED * HALF / (rough->friction * Gravity());

		CHECK(glm::length(rough->velocity) < STILL);
		CHECK(Position(*rough).x == doctest::Approx(stoppingDistance).epsilon(STEPPING));
		CHECK(smooth->velocity.x == doctest::Approx(SPEED).epsilon(CLOSE));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a fast small ball cannot pass through a thin box or a mesh between steps")
	{
		// Several times its own size, and the floor's thickness, every step.
		constexpr float FAST = 60.0f;
		constexpr float THIN = 0.1f;
		constexpr float SMALL = 0.1f;

		Loom::Scene scene("tunnelling");
		Pump();

		Loom::GameObject* slab = Spawn(scene, "Slab", glm::vec3(0.0f, -THIN * HALF, 0.0f));
		slab->Attach<Loom::BoxCollider>()->size = glm::vec3(FLOOR_SIZE, THIN, FLOOR_SIZE);

		Loom::GameObject* sheet = Spawn(scene, "Sheet", glm::vec3(FLOOR_SIZE, 0.0f, 0.0f));
		sheet->Attach<Loom::Mesh>()->m_vertices =
		{
			-10.0f, 0.0f, -10.0f,   10.0f, 0.0f, -10.0f,   10.0f, 0.0f, 10.0f,
			-10.0f, 0.0f, -10.0f,   10.0f, 0.0f,  10.0f,  -10.0f, 0.0f, 10.0f,
		};
		sheet->Attach<Loom::MeshCollider>();
		Pump();

		std::vector<Loom::Rigidbody*> balls{ };

		for (const glm::vec3& above : { glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(FLOOR_SIZE, 5.0f, 0.0f) })
		{
			Loom::SphereCollider* sphere = nullptr;
			Loom::Rigidbody* ball = Body<Loom::SphereCollider>(scene, above, &sphere);
			sphere->radius = SMALL;
			ball->velocity = glm::vec3(0.0f, -FAST, 0.0f);
			balls.push_back(ball);
		};

		Run(scene, 2.0f);

		for (Loom::Rigidbody* ball : balls)
			CHECK(Position(*ball).y == doctest::Approx(SMALL).epsilon(REST));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a kinematic body pushes others aside and is not pushed back")
	{
		Loom::Scene scene("pushing");
		Pump();

		Floor(scene);

		Loom::Rigidbody* pusher = Body<Loom::BoxCollider>(scene, glm::vec3(-2.0f, 0.5f, 0.0f));
		pusher->isKinematic = true;
		pusher->velocity = glm::vec3(2.0f, 0.0f, 0.0f);

		Loom::Rigidbody* pushed = Body<Loom::SphereCollider>(scene, glm::vec3(0.0f, 0.5f, 0.0f));

		Run(scene, 2.0f);

		CHECK(pusher->velocity == glm::vec3(2.0f, 0.0f, 0.0f));
		// Two seconds at two a second from -2; the ball ahead of the box's
		// face, by nearly its radius plus the box's half.
		constexpr float REACHED = 2.0f;
		constexpr float AHEAD = 0.9f;

		CHECK(Position(*pusher).x == doctest::Approx(REACHED).epsilon(CLOSE * 2.0f));
		CHECK(Position(*pushed).x > Position(*pusher).x + AHEAD);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "spheres and boxes come to rest on a mesh")
	{
		Loom::Scene scene("mesh ground");
		Pump();

		Loom::GameObject* ground = Spawn(scene, "Ground", glm::vec3(0.0f));
		ground->Attach<Loom::Mesh>()->m_vertices =
		{
			-10.0f, 0.0f, -10.0f,   10.0f, 0.0f, -10.0f,   10.0f, 0.0f, 10.0f,
			-10.0f, 0.0f, -10.0f,   10.0f, 0.0f,  10.0f,  -10.0f, 0.0f, 10.0f,
		};
		ground->Attach<Loom::MeshCollider>();
		Pump();

		Loom::Rigidbody* sphere = Body<Loom::SphereCollider>(scene, glm::vec3(1.0f, 2.0f, 1.0f));
		Loom::Rigidbody* box = Body<Loom::BoxCollider>(scene, glm::vec3(-3.0f, 2.0f, -2.0f));
		scene.GetRoot().GetChildren().back()->transform.rotation = glm::vec3(10.0f, 0.0f, 25.0f);

		Run(scene, 4.0f);

		CHECK(Position(*sphere).y == doctest::Approx(HALF).epsilon(REST));
		CHECK(Position(*box).y == doctest::Approx(HALF).epsilon(REST));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "both sides are told when a collision starts, goes on and ends, normals out of the other side")
	{
		Loom::Scene scene("callbacks");
		Pump();

		Floor(scene);
		Toucher* floorToucher = scene.GetRoot().GetChildren()[0]->Attach<Toucher>();

		Loom::Rigidbody* ball = Body<Loom::SphereCollider>(scene, glm::vec3(0.0f, 1.0f, 0.0f));
		Toucher* ballToucher = scene.GetRoot().GetChildren().back()->Attach<Toucher>();
		Pump();

		Run(scene, 1.0f);

		REQUIRE(ballToucher->Count("enter") == 1);
		CHECK(ballToucher->Count("stay") > 0);
		CHECK(floorToucher->Count("enter") == 1);

		const Loom::Collision& toBall = ballToucher->collisions.front();
		REQUIRE_FALSE(toBall.contacts.empty());
		CHECK(toBall.contacts[0].normal.y == doctest::Approx(1.0f).epsilon(CLOSE));
		CHECK(toBall.rigidbody == nullptr);
		CHECK(floorToucher->collisions.front().rigidbody == ball);
		CHECK(floorToucher->collisions.front().contacts[0].normal.y == doctest::Approx(-1.0f).epsilon(CLOSE));

		ball->AddForce(glm::vec3(0.0f, 10.0f, 0.0f), Loom::ForceMode::VelocityChange);
		Run(scene, 0.25f);

		CHECK(ballToucher->Count("exit") == 1);
		CHECK(floorToucher->Count("exit") == 1);
		CHECK(ballToucher->events.back() == "exit");
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a body falls through a trigger, which is told it came and went")
	{
		Loom::Scene scene("trigger");
		Pump();

		Loom::GameObject* zone = Spawn(scene, "Zone", glm::vec3(0.0f));
		Loom::BoxCollider* trigger = zone->Attach<Loom::BoxCollider>();
		trigger->isTrigger = true;
		Toucher* zoneToucher = zone->Attach<Toucher>();
		Pump();

		Loom::Rigidbody* ball = Body<Loom::SphereCollider>(scene, glm::vec3(0.0f, 2.0f, 0.0f));

		Run(scene, 1.5f);

		CHECK(Position(*ball).y < -2.0f);
		CHECK(zoneToucher->Count("trigger enter") == 1);
		CHECK(zoneToucher->Count("trigger exit") == 1);
		CHECK(zoneToucher->Count("enter") == 0);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a body at rest falls asleep and wakes when something lands on it")
	{
		Loom::Scene scene("sleeping");
		Pump();

		Floor(scene);

		Loom::Rigidbody* lower = Body<Loom::BoxCollider>(scene, glm::vec3(0.0f, 0.5f, 0.0f));

		Run(scene, 2.0f);
		REQUIRE(lower->IsSleeping());

		Loom::Rigidbody* upper = Body<Loom::BoxCollider>(scene, glm::vec3(0.0f, 3.0f, 0.0f));
		bool woke = false;

		for (int step = 0; step < SECOND; step++)
		{
			scene.Physics();
			woke |= !lower->IsSleeping();
		};

		CHECK(woke);
		Run(scene, 3.0f);

		CHECK(Position(*upper).y == doctest::Approx(1.0f + HALF).epsilon(REST));
		CHECK(Position(*lower).y == doctest::Approx(HALF).epsilon(REST));
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a body under a moved and turned parent lands where the world says, and a disabled one stays put")
	{
		Loom::Scene scene("parented");
		Pump();

		Floor(scene);

		Loom::GameObject* parent = Spawn(scene, "Parent", glm::vec3(3.0f, 2.0f, -1.0f));
		parent->transform.rotation = glm::vec3(0.0f, 45.0f, 0.0f);

		Loom::GameObject* child = Spawn(scene, "Child", glm::vec3(1.0f, 1.0f, 0.0f), parent);
		child->Attach<Loom::SphereCollider>();
		Loom::Rigidbody* body = child->Attach<Loom::Rigidbody>();

		Loom::GameObject* off = Spawn(scene, "Off", glm::vec3(-5.0f, 3.0f, 0.0f));
		off->Attach<Loom::Rigidbody>();
		off->SetDisabled(true);
		Pump();

		const glm::vec3 start = Position(*body);

		Run(scene, 3.0f);

		const glm::vec3 landed = Position(*body);
		CHECK(landed.y == doctest::Approx(HALF).epsilon(REST));
		CHECK(landed.x == doctest::Approx(start.x).epsilon(CLOSE));
		CHECK(landed.z == doctest::Approx(start.z).epsilon(CLOSE));

		CHECK(*off->transform.position == glm::vec3(-5.0f, 3.0f, 0.0f));
	};
};
