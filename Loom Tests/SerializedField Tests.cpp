#include "doctest.h"

#include "Test Support.h"

#ifndef __EMSCRIPTEN__
#include "FieldNames.h"
#endif
#include "GameObject.h"
#include "LoomObject.h"
#include "Material.h"
#include "Mesh.h"
#include "Scene.h"
#include "SceneSerializer.h"
#include "SerializedField.h"

#include <string>
#include <vector>

using LoomTests::Pump;

namespace
{
	// One member of every type the format knows about, so a single object can
	// stand in for all of them.
	struct EveryField final : Loom::Component<EveryField>
	{
		Loom::Serial<bool> flag;
		Loom::Serial<int> count;
		Loom::Serial<unsigned int> unsigned_count;
		Loom::Serial<long long> big;
		Loom::Serial<unsigned long long> unsigned_big;
		Loom::Serial<float> ratio;
		Loom::Serial<double> precise;
		Loom::Serial<std::string> label;
		Loom::Serial<glm::vec2> pair;
		Loom::Serial<glm::vec3> triple;
		Loom::Serial<glm::vec4> quad;
		Loom::Serial<std::vector<float>> numbers;
	};

	struct Pointer final : Loom::Component<Pointer>
	{
		Loom::Serial<Loom::GameObject*> target;
	};

	// One reference of each kind the editor fills by dragging.
	struct Slots final : Loom::Component<Slots>
	{
		Loom::Serial<Loom::Scene*> scene;
		Loom::Serial<Loom::GameObject*> gameObject;
		Loom::Serial<Loom::Material*> material;
	};

	// A field belongs to the object being built around it, whichever class in
	// the hierarchy declared it.
	struct Base : Loom::LoomObject
	{
		Loom::Serial<int> first = 1;
	};

	struct Derived final : Base
	{
		Loom::Serial<int> second = 2;
	};

	// The shape Scene has: a LoomObject of its own, declared after the fields of
	// the object that owns it.
	struct Host final : Loom::LoomObject
	{
		Loom::Serial<int> mine = 3;

		Derived owned;
	};

	// The same object the other way round, which is the rule Serial documents:
	// everything declared after the LoomObject member belongs to that member.
	struct LateHost final : Loom::LoomObject
	{
		Derived owned;

		Loom::Serial<int> late;
	};

	// Serials in a plain struct member belong to the object holding it.
	struct Settings
	{
		Loom::Serial<float> m_strength = 1.0f;
	};

	struct Nested final : Loom::LoomObject
	{
		Loom::Serial<int> m_before;
		Settings settings;
	};

	enum Shade : int { Light = -1, Medium, DarkGrey = 7 };

	enum struct Size : unsigned int { Small = 10, ExtraLarge = 4000000000 };

	struct Enumerated final : Loom::LoomObject
	{
		Loom::Serial<Shade> m_shade = Medium;
		Loom::Serial<Size> size = Size::Small;
		Loom::Serial<int> m_plain;
	};

	// Fields are numbered by declaration order; these are EveryField's.
	enum Every { Flag, Count, UnsignedCount, Big, UnsignedBig, Ratio, Precise, Label, Pair, Triple, Quad, Numbers };

	const Loom::SerializedField& Field(const Loom::LoomObject& object, size_t index)
	{
		REQUIRE(index < object.GetFields().size());

		return object.GetFields()[index];
	};
};


TEST_SUITE("SerializedField")
{
	TEST_CASE("an identifier reads as words")
	{
		CHECK(Loom::NameFromIdentifier("position") == "Position");
		CHECK(Loom::NameFromIdentifier("m_fieldOfView") == "Field Of View");
		CHECK(Loom::NameFromIdentifier("m_inherit_thread_id") == "Inherit Thread Id");
		CHECK(Loom::NameFromIdentifier("m_threadID") == "Thread ID");
	};

#ifndef __EMSCRIPTEN__
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "the editor labels a field after its member")
	{
		EveryField object;
		Pump();

		const std::vector<std::string> labels = Loom::FieldNames::Of(object);

		REQUIRE(labels.size() == object.GetFields().size());

		CHECK(labels[Flag] == "Flag");
		CHECK(labels[UnsignedCount] == "Unsigned Count");
		CHECK(labels[Numbers] == "Numbers");
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a field declared in a base class is labelled after its member")
	{
		Derived object;
		Pump();

		CHECK(Loom::FieldNames::Of(object) == std::vector<std::string>{ "First", "Second" });
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a field inside a struct member is labelled after its own member")
	{
		Nested object;
		Pump();

		CHECK(Loom::FieldNames::Of(object) == std::vector<std::string>{ "Before", "Strength" });
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a field that is not one of the object's members is numbered")
	{
		// late is LateHost's, but registered with owned, which was built last.
		LateHost host;
		Pump();

		CHECK(Loom::FieldNames::Of(host.owned) == std::vector<std::string>{ "First", "Second", "Field 2" });
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "an enum field offers its enumerators, labelled like members")
	{
		Enumerated object;
		Pump();

		const std::vector<const std::vector<Loom::FieldNames::Enumerator>*> enumerators = Loom::FieldNames::EnumeratorsOf(object);

		REQUIRE(enumerators.size() == 3);
		REQUIRE(enumerators[0] != nullptr);
		REQUIRE(enumerators[1] != nullptr);

		const std::vector<Loom::FieldNames::Enumerator>& shades = *enumerators[0];
		const std::vector<Loom::FieldNames::Enumerator>& sizes = *enumerators[1];

		REQUIRE(shades.size() == 3);
		REQUIRE(sizes.size() == 2);

		CHECK(shades[0].label == "Light");
		CHECK(shades[0].value == Light);
		CHECK(shades[2].label == "Dark Grey");
		CHECK(shades[2].value == DarkGrey);
		CHECK(sizes[1].label == "Extra Large");
		CHECK(sizes[1].value == (long long)Size::ExtraLarge);
		CHECK(enumerators[2] == nullptr);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a mesh's primitive and draw type are dropdowns")
	{
		Loom::Mesh mesh;
		Pump();

		const std::vector<std::string> labels = Loom::FieldNames::Of(mesh);
		const std::vector<const std::vector<Loom::FieldNames::Enumerator>*> enumerators = Loom::FieldNames::EnumeratorsOf(mesh);

		REQUIRE(labels.size() == 4);
		REQUIRE(enumerators[1] != nullptr);
		REQUIRE(enumerators[2] != nullptr);

		CHECK(labels[1] == "Primitive Id");
		CHECK(enumerators[1]->size() == 7);
		CHECK(labels[2] == "Draw Type");
		CHECK(enumerators[2]->size() == 3);
	};
#endif

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "an enum is written as its underlying integer")
	{
		Enumerated object;
		Pump();

		CHECK(Field(object, 0).type == Loom::FieldType::Int);
		CHECK(Field(object, 1).type == Loom::FieldType::UInt);

		object.m_shade = Light;
		object.size = Size::ExtraLarge;

		CHECK(Field(object, 0).Write() == "-1");
		CHECK(Field(object, 1).Write() == "4000000000");

		REQUIRE(Field(object, 0).Read("7"));
		REQUIRE(Field(object, 1).Read("10"));

		CHECK(*object.m_shade == DarkGrey);
		CHECK(*object.size == Size::Small);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "fields are declared in the order they are registered")
	{
		EveryField object;
		Pump();

		REQUIRE(object.GetFields().size() == 12);

		CHECK(Field(object, Flag).type == Loom::FieldType::Bool);
		CHECK(Field(object, Ratio).type == Loom::FieldType::Float);
		CHECK(Field(object, Numbers).type == Loom::FieldType::FloatArray);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a base class and the type deriving from it declare onto the same object")
	{
		Derived object;
		Pump();

		REQUIRE(object.GetFields().size() == 2);

		// The base's field is numbered first, because it was built first.
		CHECK(Field(object, 0).Write() == "1");
		CHECK(Field(object, 1).Write() == "2");

		REQUIRE(Field(object, 1).Read("9"));
		CHECK(object.second == 9);
	};

	// Which is what keeps a Scene's root GameObject from taking the fields of
	// the object that owns it.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "an object that owns another keeps its own fields")
	{
		Host host;
		Pump();

		REQUIRE(host.GetFields().size() == 1);
		CHECK(Field(host, 0).Write() == "3");

		CHECK(host.owned.GetFields().size() == 2);
	};

	// Documented rather than fixed: there is no hook for the end of a
	// constructor, so a field declared past a nested object cannot tell that the
	// object it is landing on has finished being built.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a field declared after a LoomObject member lands on that member")
	{
		LateHost host;
		Pump();

		CHECK(host.GetFields().empty());
		CHECK(host.owned.GetFields().size() == 3);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a field points at the member it was declared from")
	{
		EveryField object;
		Pump();

		object.count = 7;
		CHECK(Field(object, Count).Write() == "7");

		REQUIRE(Field(object, Count).Read("12"));
		CHECK(object.count == 12);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "every type survives being written and read back")
	{
		EveryField written;
		Pump();

		written.flag = true;
		written.count = -42;
		written.unsigned_count = 4000000000u;
		written.big = -9000000000LL;
		written.unsigned_big = 18000000000000000000ull;
		written.ratio = 0.1f;
		written.precise = 0.1;
		written.label = "a name with spaces";
		written.pair = { 1.5f, -2.5f };
		written.triple = { 1.0f, 2.0f, 3.0f };
		written.quad = { 0.25f, 0.0f, 0.0f, 4.0f };
		written.numbers = { -1.0f, 0.0f, 0.5f, 2.0f };

		EveryField read;
		Pump();

		for (size_t i = 0; i < written.GetFields().size(); i++)
			REQUIRE(Field(read, i).Read(Field(written, i).Write()));

		CHECK(read.flag == written.flag);
		CHECK(read.count == written.count);
		CHECK(read.unsigned_count == written.unsigned_count);
		CHECK(read.big == written.big);
		CHECK(read.unsigned_big == written.unsigned_big);
		CHECK(read.ratio == doctest::Approx(written.ratio));
		CHECK(read.precise == doctest::Approx(written.precise));
		CHECK(*read.label == *written.label);
		CHECK(read.pair->y == doctest::Approx(-2.5f));
		CHECK(read.triple->z == doctest::Approx(3.0f));
		CHECK(read.quad->w == doctest::Approx(4.0f));
		CHECK(*read.numbers == *written.numbers);
	};

	// The value has to come back exactly, not nearly: a scene saved and loaded
	// twice should not drift.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "floating point round trips exactly")
	{
		EveryField object;
		Pump();

		object.ratio = 3.14159274f;
		object.precise = 2.718281828459045;

		const std::string ratio = Field(object, Ratio).Write();
		const std::string precise = Field(object, Precise).Write();

		object.ratio = 0.0f;
		object.precise = 0.0;

		REQUIRE(Field(object, Ratio).Read(ratio));
		REQUIRE(Field(object, Precise).Read(precise));

		CHECK(object.ratio == 3.14159274f);
		CHECK(object.precise == 2.718281828459045);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a string keeps the characters that would break the format")
	{
		EveryField object;
		Pump();

		object.label = "tab\there \"quoted\" and\nnewline";

		const std::string written = Field(object, Label).Write();

		CHECK(written.find('\n') == std::string::npos);
		CHECK(written.find('\t') == std::string::npos);

		object.label->clear();

		REQUIRE(Field(object, Label).Read(written));

		CHECK(*object.label == "tab\there \"quoted\" and\nnewline");
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "an empty float list is still a float list")
	{
		EveryField object;
		Pump();

		CHECK(Field(object, Numbers).Write() == "[]");

		object.numbers = { 1.0f };

		REQUIRE(Field(object, Numbers).Read("[]"));
		CHECK(object.numbers->empty());
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a reference is written as the guid of what it points at")
	{
		Loom::Scene scene("references");
		Pump();

		Pointer* pointer = scene.GetRoot().Attach<Pointer>();
		Pump();

		CHECK(Field(*pointer, 0).Write() == "null");

		pointer->target = &scene.GetRoot();

		CHECK(Field(*pointer, 0).Write() == scene.GetRoot().GetGuid().ToString());

		pointer->target = nullptr;

		REQUIRE(Field(*pointer, 0).Read(scene.GetRoot().GetGuid().ToString()));
		CHECK(pointer->target == &scene.GetRoot());

		REQUIRE(Field(*pointer, 0).Read("null"));
		CHECK(pointer->target == nullptr);
	};

	// The loader leans on this: a reference to something further down the file
	// is not an error, it is a fixup to come back to.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a reference to nothing yet reads as unresolved")
	{
		Loom::Scene scene("unresolved");
		Pump();

		Pointer* pointer = scene.GetRoot().Attach<Pointer>();
		Pump();

		CHECK_FALSE(Field(*pointer, 0).Read(Loom::Guid::New().ToString()));
		CHECK(pointer->target == nullptr);
	};

	// Detaching a component, or destroying a GameObject, has to take every
	// reference to it with it. One left behind is read again the next frame -
	// by the serializer writing its guid, by the inspector drawing its name -
	// and by then the object is gone.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "detaching a component clears references to it")
	{
		Loom::Scene scene("detached target");
		Pump();

		Pointer* pointer = scene.GetRoot().Attach<Pointer>();
		Loom::GameObject* target = scene.AddChild("Target");
		Pump();

		pointer->target = target;

		REQUIRE(pointer->target == target);

		target->Destroy();
		Pump();

		CHECK(pointer->target == nullptr);
		CHECK(Field(*pointer, 0).Write() == "null");
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a component pointed at by another clears it on the way out")
	{
		Loom::Scene scene("detached component");
		Pump();

		Loom::Mesh* mesh = scene.GetRoot().Attach<Loom::Mesh>(Loom::Mesh::Triangles);
		Loom::Material* material = scene.GetRoot().Attach<Loom::Material>();
		Pump();

		mesh->material = material;

		scene.GetRoot().DetachComponent(material);
		Pump();

		CHECK(mesh->material == nullptr);

		// And the scene it writes out says so, rather than the guid of something
		// that is no longer there. The mesh declares its material first, so the
		// field is number zero.
		CHECK(Loom::SceneSerializer::Serialize(scene).find("0 = null") != std::string::npos);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a reference only accepts what it can point at")
	{
		Loom::Scene scene("wrong type");
		Pump();

		Pointer* pointer = scene.GetRoot().Attach<Pointer>();
		Pump();

		pointer->target = &scene.GetRoot();

		// The scene is a LoomObject, but it is not a GameObject: the field
		// declines rather than handing back something of the wrong type.
		CHECK_FALSE(Field(*pointer, 0).Read(scene.GetGuid().ToString()));
		CHECK(pointer->target == nullptr);
	};

	// What the editor's inspector does with a dragged scene or GameObject.
	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a reference takes a dropped object, or the component of it that fits")
	{
		Loom::Scene scene("drops");
		Pump();

		Slots* slots = scene.GetRoot().Attach<Slots>();
		Loom::GameObject* plain = scene.AddChild("Plain");
		Loom::GameObject* textured = scene.AddChild("Textured");
		Loom::Material* material = textured->Attach<Loom::Material>();
		Loom::Material* rootMaterial = scene.GetRoot().Attach<Loom::Material>();
		Pump();

		const Loom::SerializedField& sceneField = Field(*slots, 0);
		const Loom::SerializedField& gameObjectField = Field(*slots, 1);
		const Loom::SerializedField& materialField = Field(*slots, 2);

		CHECK(sceneField.ReferenceFor(&scene) == &scene);
		CHECK(sceneField.ReferenceFor(plain) == nullptr);

		CHECK(gameObjectField.ReferenceFor(plain) == plain);

		// The hierarchy draws the root as the scene, so the scene is how the
		// root and its components get dragged.
		CHECK(gameObjectField.ReferenceFor(&scene) == &scene.GetRoot());
		CHECK(materialField.ReferenceFor(&scene) == rootMaterial);

		CHECK(materialField.ReferenceFor(textured) == material);
		CHECK(materialField.ReferenceFor(plain) == nullptr);

		CHECK(gameObjectField.ReferenceFor(nullptr) == nullptr);

		EveryField values;
		Pump();

		CHECK(Field(values, Count).ReferenceFor(plain) == nullptr);
	};
};
