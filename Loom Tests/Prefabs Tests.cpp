#include "doctest.h"

#include "Test Support.h"

#include "Prefabs.h"

#include "GameObject.h"
#include "Guid.h"
#include "Prefab.h"
#include "Scene.h"

#include <string>
#include <vector>

using Loom::Prefabs::ObjectNode;
using Loom::Prefabs::Part;

namespace
{
	std::string NewGuid()
	{
		return Loom::Guid::New().ToString();
	};

	// The keys GameObject's transform is written under, as a scene file has
	// them: after the two thread fields.
	const std::vector<std::string> transform_keys = { "2", "3", "4" };

	// A prefab-shaped tree: a top object with a Marker pointing at its child,
	// and the child, each with guids of their own.
	struct Tree
	{
		std::string top = NewGuid();
		std::string marker = NewGuid();
		std::string child = NewGuid();
		std::string outside = NewGuid();

		std::string Text(
			const std::string& name = "Goblin",
			const std::string& position = "(1, 2, 3)",
			const std::string& weight = "5",
			const std::string& link = "") const
		{
			std::string text =
				"GameObject " + name + "(" + top + ")\n"
				"\t0 = 0\n"
				"\t2 = " + position + "\n"
				"\tComponent Marker(" + marker + ")\n"
				"\t\t0 = " + weight + "\n"
				"\t\t1 = " + child + "\n"
				"\t\t2 = " + outside + "\n";

			if (!link.empty())
				text += "\tComponent " + Loom::Prefabs::PrefabType() + "(" + link + ")\n\t\t0 = \"Prefabs/Goblin.loomprefab\"\n";

			return text +
				"\tGameObject Sword(" + child + ")\n"
				"\t\t2 = (0, 1, 0)\n";
		};

		ObjectNode Node(
			const std::string& name = "Goblin",
			const std::string& position = "(1, 2, 3)",
			const std::string& weight = "5",
			const std::string& link = "") const
		{
			ObjectNode node;
			REQUIRE(Loom::Prefabs::Parse(Text(name, position, weight, link), node));
			return node;
		};
	};

	std::string Shared(const ObjectNode& node)
	{
		return Loom::Prefabs::Shape(node, transform_keys, Part::Shared);
	};

	std::string Whole(const ObjectNode& node)
	{
		return Loom::Prefabs::Shape(node, transform_keys, Part::Whole);
	};
};


TEST_SUITE("Prefabs")
{
	TEST_CASE("a GameObject's text parses and writes back the same")
	{
		const Tree tree;
		const std::string text = tree.Text();

		ObjectNode node;
		REQUIRE(Loom::Prefabs::Parse(text, node));

		CHECK(node.name == "Goblin");
		CHECK(node.components.size() == 1);
		CHECK(node.children.size() == 1);
		CHECK(Loom::Prefabs::Write(node) == text);
	};

	TEST_CASE("text that is not one GameObject does not parse")
	{
		ObjectNode node;
		std::string error;

		for (const char* text : { "", "0 = 1\n", "Something else\n", "GameObject NoGuid\n" })
		{
			CHECK_FALSE(Loom::Prefabs::Parse(text, node, &error));
			CHECK_FALSE(error.empty());
		};

		const std::string twice =
			"GameObject A(" + NewGuid() + ")\n"
			"GameObject B(" + NewGuid() + ")\n";

		CHECK_FALSE(Loom::Prefabs::Parse(twice, node));
	};

	TEST_CASE("instances compare the same whatever their guids, transforms, name, link and references out")
	{
		const Tree prefab;
		const Tree instance;

		CHECK(Shared(prefab.Node()) == Shared(instance.Node("Goblin 2", "(9, 9, 9)", "5", NewGuid())));
		CHECK(Shared(prefab.Node()) != Shared(instance.Node("Goblin", "(1, 2, 3)", "6")));
	};

	TEST_CASE("a component's field under a key a transform has on a GameObject still counts")
	{
		const Tree tree;

		ObjectNode edited = tree.Node();
		REQUIRE(edited.components[0].fields[2].key == transform_keys[0]);
		edited.components[0].fields[2].value = "false";

		CHECK(Shared(tree.Node()) != Shared(edited));
	};

	TEST_CASE("the whole of a prefab takes in its transforms and name")
	{
		const Tree prefab;
		const Tree copy;

		CHECK(Whole(prefab.Node()) == Whole(copy.Node()));
		CHECK(Whole(prefab.Node()) != Whole(copy.Node("Goblin", "(0, 0, 0)")));
		CHECK(Whole(prefab.Node()) != Whole(copy.Node("Orc")));
	};

	TEST_CASE("retargeting an edit keeps the target's own identities")
	{
		const Tree edited;
		const Tree target;
		const std::string link = NewGuid();

		const ObjectNode result = Loom::Prefabs::Retarget(
			edited.Node("Goblin", "(1, 2, 3)", "7"),
			target.Node("Goblin 2", "(4, 5, 6)", "5", link),
			transform_keys,
			Part::Shared);

		CHECK(result.guid == target.top);
		CHECK(result.name == "Goblin 2");
		CHECK(result.fields[1].value == "(4, 5, 6)");

		REQUIRE(result.components.size() == 2);
		CHECK(result.components[0].guid == target.marker);
		CHECK(result.components[0].fields[0].value == "7");

		// The reference to its own child follows to the target's child, and
		// the one out of it stays what the target had.
		CHECK(result.components[0].fields[1].value == target.child);
		CHECK(result.components[0].fields[2].value == target.outside);

		CHECK(result.components[1].guid == link);
		CHECK(result.children[0].guid == target.child);
	};

	TEST_CASE("retargeting the whole takes the source's transforms and name")
	{
		const Tree source;
		const Tree target;

		const ObjectNode result = Loom::Prefabs::Retarget(
			source.Node("Orc", "(7, 7, 7)"),
			target.Node(),
			transform_keys,
			Part::Whole);

		CHECK(result.guid == target.top);
		CHECK(result.name == "Orc");
		CHECK(result.fields[1].value == "(7, 7, 7)");
		CHECK(Whole(result) == Whole(source.Node("Orc", "(7, 7, 7)")));
	};

	TEST_CASE("something added to an instance gets guids of its own in the prefab")
	{
		const Tree edited;
		const Tree target;

		ObjectNode source = edited.Node();
		source.children.push_back(ObjectNode{ "Shield", NewGuid() });

		const ObjectNode result = Loom::Prefabs::Retarget(source, target.Node(), transform_keys, Part::Shared);

		REQUIRE(result.children.size() == 2);
		CHECK(result.children[0].guid == target.child);
		CHECK(result.children[1].name == "Shield");
		CHECK(result.children[1].guid != source.children[1].guid);
		CHECK(Shared(result) == Shared(source));
	};

	TEST_CASE("an instance's lines in a scene are swapped for its new text, at its depth")
	{
		const Tree instance;
		const std::string sibling = NewGuid();

		std::string scene = "Main(" + NewGuid() + "):\n\tGameObject Root(" + NewGuid() + ")\n";

		for (const std::string& line : { std::string("GameObject Goblin(") + instance.top + ")", std::string("\t0 = 0") })
			scene += "\t\t" + line + "\n";

		scene += "\t\tGameObject After(" + sibling + ")\n";

		const std::string replacement = "GameObject Goblin(" + instance.top + ")\n\t0 = 1\n";
		const std::string spliced = Loom::Prefabs::Splice(scene, instance.top, replacement);

		CHECK(spliced.find("\t\tGameObject Goblin(" + instance.top + ")\n\t\t\t0 = 1\n\t\tGameObject After(") != std::string::npos);
		CHECK(Loom::Prefabs::Splice(scene, NewGuid(), replacement) == scene);
	};

	TEST_CASE_FIXTURE(LoomTests::EngineFixture, "a GameObject's transform is written under the keys the tests use")
	{
		Loom::Scene scene("Transforms");
		LoomTests::Pump();

		CHECK(Loom::Prefabs::TransformKeys(scene.GetRoot()) == transform_keys);
	};
};
