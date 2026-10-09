#include "doctest.h"

#include "Test Support.h"

#include "ComponentMenu.h"
#include "ComponentRegistry.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>


namespace
{
	struct Uncategorised final : Loom::Component<Uncategorised>
	{ };

	struct Categorised final : Loom::Component<Categorised, "Gameplay / Enemies/">
	{ };

	// Left to register itself by name, since it needs an argument.
	struct NeedsArgument final : Loom::Component<NeedsArgument, "Gameplay">
	{
		explicit NeedsArgument(int) { };
	};

	// The node at path under root, or null.
	const Loom::ComponentMenu::Node* Find(const Loom::ComponentMenu::Node& root, const std::vector<std::string>& path)
	{
		const Loom::ComponentMenu::Node* node = &root;

		for (const std::string& name : path)
		{
			const Loom::ComponentMenu::Node* next = nullptr;

			for (const Loom::ComponentMenu::Node& child : node->children)
				if (child.name == name)
					next = &child;

			if (next == nullptr)
				return nullptr;

			node = next;
		};

		return node;
	};

	bool Holds(const Loom::ComponentMenu::Node* node, const std::string& component)
	{
		return node != nullptr && std::ranges::find(node->components, component) != node->components.end();
	};
};


TEST_SUITE("ComponentMenu")
{
	TEST_CASE("a type's own category is registered with it, and unregistering drops it")
	{
		CHECK(Loom::ComponentRegistry::CategoryOf("Categorised") == "Gameplay / Enemies/");
		CHECK(Loom::ComponentRegistry::CategoryOf("Uncategorised").empty());
		CHECK(Loom::ComponentRegistry::CategoryOf("Mesh") == "Rendering");

		Loom::ComponentRegistry::Register("Transient", nullptr, [](Loom::GameObject&) { return nullptr; }, "Somewhere");
		CHECK(Loom::ComponentRegistry::CategoryOf("Transient") == "Somewhere");

		Loom::ComponentRegistry::Unregister("Transient");
		CHECK(Loom::ComponentRegistry::CategoryOf("Transient").empty());
	};

	TEST_CASE("a type registered by name keeps its category")
	{
		constexpr int argument = 1;

		Loom::ComponentRegistry::Register<NeedsArgument>("Needs Argument", argument);
		CHECK(Loom::ComponentRegistry::CategoryOf("Needs Argument") == "Gameplay");

		Loom::ComponentRegistry::Unregister("Needs Argument");
	};

	TEST_CASE("categories are tidied into parts")
	{
		CHECK(Loom::ComponentMenu::Tidy(" Rendering / 2D/ ") == "Rendering/2D");
		CHECK(Loom::ComponentMenu::Tidy("//") == "");
		CHECK(Loom::ComponentMenu::Tidy("Big Things") == "Big Things");
	};

	TEST_CASE("engine components sit under the categories they declare")
	{
		const Loom::ComponentMenu::Node root = Loom::ComponentMenu{ }.Build({ });

		CHECK(Holds(Find(root, { "Rendering" }), "Mesh"));
		CHECK(Holds(Find(root, { "Rendering" }), "Camera"));
		CHECK(Holds(Find(root, { "Rendering", "2D" }), "Sprite"));
		CHECK(Holds(Find(root, { "Physics" }), "BoxCollider"));
		CHECK(Holds(Find(root, { "Physics" }), "SphereCollider"));
		CHECK(Holds(Find(root, { "Physics" }), "MeshCollider"));
		CHECK(Holds(Find(root, { "Logic" }), "StateMachine"));
		CHECK(Holds(Find(root, { "Gameplay", "Enemies" }), "Categorised"));
		CHECK(Holds(&root, "Uncategorised"));
		CHECK(Find(root, { Loom::ComponentMenu::custom_category }) == nullptr);
	};

	TEST_CASE("a script with no category of its own goes first, under Custom")
	{
		const Loom::ComponentMenu::Node root = Loom::ComponentMenu{ }.Build({ "Uncategorised", "Categorised" });

		REQUIRE_FALSE(root.children.empty());
		CHECK(root.children.front().name == Loom::ComponentMenu::custom_category);
		CHECK(Holds(&root.children.front(), "Uncategorised"));
		CHECK_FALSE(Holds(&root, "Uncategorised"));
		CHECK(Holds(Find(root, { "Gameplay", "Enemies" }), "Categorised"));
	};

	TEST_CASE("the project's category wins, and clearing it puts the component back")
	{
		Loom::ComponentMenu menu;

		menu.Set("Mesh", "Mine/Meshes");
		menu.Set("Uncategorised", "Custom/Helpers");

		Loom::ComponentMenu::Node root = menu.Build({ "Uncategorised" });

		CHECK(Holds(Find(root, { "Mine", "Meshes" }), "Mesh"));
		CHECK_FALSE(Holds(Find(root, { "Rendering" }), "Mesh"));
		CHECK(Holds(Find(root, { "Custom", "Helpers" }), "Uncategorised"));
		CHECK(menu.IsFiled("Mesh"));

		menu.Set("Mesh", " / ");
		root = menu.Build({ });

		CHECK_FALSE(menu.IsFiled("Mesh"));
		CHECK(Holds(Find(root, { "Rendering" }), "Mesh"));
	};

	TEST_CASE("categories read back from a project file, and saving keeps its other lines")
	{
		const std::filesystem::path file = std::filesystem::temp_directory_path() / "ComponentMenu Tests.loomproject";

		{
			std::ofstream out(file, std::ios::binary);
			out << "name=Example\r\ncategory.Old=Gone\r\nscripts=Scripts/Example.vcxproj\r\n";
		};

		Loom::ComponentMenu menu;
		menu.Read("category.Mesh=Mine / Meshes");
		menu.Read("category.=Nothing");
		menu.Read("name=Example");

		REQUIRE(menu.Save(file.string()));

		std::ifstream in(file);
		std::stringstream text;
		text << in.rdbuf();
		in.close();

		CHECK(text.str() == "name=Example\nscripts=Scripts/Example.vcxproj\ncategory.Mesh=Mine/Meshes\n");

		Loom::ComponentMenu read;
		std::string line;

		while (std::getline(text, line))
			read.Read(line);

		CHECK(read.CategoryOf("Mesh", false) == "Mine/Meshes");
		CHECK_FALSE(read.IsFiled("Old"));

		std::filesystem::remove(file);
	};
};
