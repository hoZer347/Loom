#include "doctest.h"

#include "EditorIcons.h"
#include "KindFilter.h"
#include "UiScript.h"

#include "imgui.h"

#include <filesystem>
#include <memory>
#include <string>

namespace
{
	constexpr Loom::KindFilter::Kind kinds[] =
	{
		{ Loom::EditorIcons::Script, "Scripts" },
		{ Loom::EditorIcons::State, "States" },
		{ Loom::EditorIcons::Shader, "Shaders" },
	};

	// A KindFilter in a window of the given width, in an ImGui context of the
	// test's own with no renderer, driven by a UiScript.
	struct Driven
	{
		Driven(const std::string& text, float width)
			: width(width)
		{
			constexpr float frame_time = 1.0f / 60.0f;

			context = ImGui::CreateContext();

			ImGuiIO& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.DisplaySize = ImVec2(display_width, display_height);
			io.DeltaTime = frame_time;

			unsigned char* pixels = nullptr;
			int atlas_width = 0;
			int atlas_height = 0;
			io.Fonts->GetTexDataAsRGBA32(&pixels, &atlas_width, &atlas_height);

			script = std::make_unique<Loom::UiScript>(std::filesystem::path());
			script->LoadText(text);
		};

		~Driven()
		{
			script.reset();
			ImGui::DestroyContext(context);
		};

		void Run()
		{
			constexpr int most_frames = 600;

			for (int frame = 0; frame < most_frames && !script->Finished(); frame++)
			{
				script->Step();

				ImGui::NewFrame();
				ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
				ImGui::SetNextWindowSize(ImVec2(width, display_height));

				if (ImGui::Begin("Filter"))
				{
					height = Loom::KindFilter::Height(kinds);
					line = ImGui::GetFrameHeight();

					const bool before = filter.Shows(Loom::EditorIcons::State);

					filter.DrawUnder(kinds);

					// What a list drawn after the row would see, and whether the
					// mouse was still down, as it is from the press on.
					if (filter.Shows(Loom::EditorIcons::State) != before)
					{
						toggled_before_list = true;
						toggled_on_press = ImGui::IsMouseDown(ImGuiMouseButton_Left);
					};
				};

				ImGui::End();
				ImGui::Render();
			};
		};

		static constexpr float display_width = 800.0f;
		static constexpr float display_height = 600.0f;

		float width;
		float height = 0.0f;
		float line = 0.0f;
		bool toggled_before_list = false;
		bool toggled_on_press = false;

		ImGuiContext* context = nullptr;
		std::unique_ptr<Loom::UiScript> script;
		Loom::KindFilter filter;
	};

	// The filter is drawn before anything in it can be aimed at.
	constexpr const char* drawn = "wait 2\n";

	// What the filter draws for a kind, for a UiScript to aim at.
	std::string Click(const char* icon)
	{
		return std::string("click \"Filter\" \"") + icon + "\"\n";
	};
};


TEST_SUITE("KindFilter")
{
	TEST_CASE("every kind shows until it is hidden, known by its icon's text")
	{
		Loom::KindFilter filter;

		CHECK(filter.Shows(Loom::EditorIcons::Shader));
		CHECK(filter.Shows(Loom::EditorIcons::File));

		filter.Show(std::string(Loom::EditorIcons::Shader), false);

		CHECK_FALSE(filter.Shows(Loom::EditorIcons::Shader));
		CHECK(filter.Shows(Loom::EditorIcons::Script));

		filter.Show(Loom::EditorIcons::Shader, true);
		filter.Show(Loom::EditorIcons::Script, true);

		CHECK(filter.Shows(Loom::EditorIcons::Shader));
		CHECK(filter.Shows(Loom::EditorIcons::Script));
	};

	TEST_CASE("clicking a kind's icon hides it, and clicking it again shows it")
	{
		constexpr float wide = 400.0f;

		Driven hidden(std::string(drawn) + Click(Loom::EditorIcons::State) + drawn, wide);
		hidden.Run();

		CHECK_FALSE(hidden.script->Failed());
		CHECK_FALSE(hidden.filter.Shows(Loom::EditorIcons::State));
		CHECK(hidden.filter.Shows(Loom::EditorIcons::Script));
		CHECK(hidden.filter.Shows(Loom::EditorIcons::Shader));
		CHECK(hidden.toggled_before_list);
		CHECK(hidden.toggled_on_press);

		Driven shown(std::string(drawn) + Click(Loom::EditorIcons::State) + drawn + Click(Loom::EditorIcons::State) + drawn, wide);
		shown.Run();

		CHECK_FALSE(shown.script->Failed());
		CHECK(shown.filter.Shows(Loom::EditorIcons::State));
	};

	TEST_CASE("the row is one line in a wide panel and wraps in a narrow one")
	{
		constexpr float wide = 400.0f;
		constexpr float narrow = 60.0f;

		Driven one(drawn, wide);
		one.Run();

		CHECK(one.height == doctest::Approx(one.line));

		Driven wrapped(drawn, narrow);
		wrapped.Run();

		CHECK(wrapped.height > wrapped.line);
	};
};
