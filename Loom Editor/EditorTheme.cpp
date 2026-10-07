#include "EditorTheme.h"

#include "ProjectTemplate.h"

#include <filesystem>
#include <iostream>


namespace Loom
{
	namespace
	{
		constexpr float ChannelMax = 255.0f;
		constexpr unsigned int ChannelMask = 0xFF;
		constexpr int RedShift = 16;
		constexpr int GreenShift = 8;

		// Vendored with ImGui, so they are wherever the engine is.
		std::filesystem::path FontFolder()
		{
			return std::filesystem::path(ProjectTemplate::EngineRoot()) / "External Libraries/imgui/misc/fonts";
		};

		constexpr ImVec4 Rgb(unsigned int hex, float alpha = 1.0f)
		{
			return ImVec4(
				((hex >> RedShift) & ChannelMask) / ChannelMax,
				((hex >> GreenShift) & ChannelMask) / ChannelMax,
				(hex & ChannelMask) / ChannelMax,
				alpha);
		};

		constexpr ImVec4 WithAlpha(ImVec4 colour, float alpha)
		{
			return ImVec4(colour.x, colour.y, colour.z, alpha);
		};

		// Neutral greys from the deepest well up to the most raised control.
		constexpr ImVec4 Well = Rgb(0x141517);
		constexpr ImVec4 Panel = Rgb(0x1B1C1F);
		constexpr ImVec4 Chrome = Rgb(0x232428);
		constexpr ImVec4 Control = Rgb(0x2B2D31);
		constexpr ImVec4 ControlHovered = Rgb(0x36383D);
		constexpr ImVec4 ControlActive = Rgb(0x43464C);
		constexpr ImVec4 Edge = Rgb(0x313338);

		constexpr ImVec4 Text = Rgb(0xDCDEE3);
		constexpr ImVec4 TextDim = Rgb(0x7A7E87);

		constexpr ImVec4 Accent = Rgb(0x4C8DF6);
		constexpr ImVec4 AccentHovered = Rgb(0x6AA1F8);
		constexpr ImVec4 AccentActive = Rgb(0x3A73D6);
		constexpr ImVec4 AccentLight = Rgb(0x9DC1FF);
		constexpr ImVec4 Warning = Rgb(0xE5A445);
		constexpr ImVec4 Error = Rgb(0xF0625D);

		constexpr float SelectionAlpha = 0.35f;
		constexpr float SelectionActiveAlpha = 0.5f;
		constexpr float DimAlpha = 0.6f;
		constexpr float GripAlpha = 0.25f;

		constexpr ImVec4 Clear = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
		constexpr ImVec4 Dimmer = ImVec4(0.0f, 0.0f, 0.0f, DimAlpha);

		constexpr float ControlRounding = 4.0f;
		constexpr float GrabRounding = 3.0f;
		constexpr float ScrollbarRounding = 6.0f;
		constexpr float ScrollbarSize = 12.0f;
		constexpr float GrabMinSize = 10.0f;
		constexpr float IndentSpacing = 14.0f;
		constexpr float DockingSeparatorSize = 2.0f;
		constexpr ImVec2 WindowPadding = ImVec2(8.0f, 8.0f);
		constexpr ImVec2 FramePadding = ImVec2(6.0f, 4.0f);
		constexpr ImVec2 ItemSpacing = ImVec2(8.0f, 5.0f);
		constexpr ImVec2 ItemInnerSpacing = ImVec2(5.0f, 4.0f);
		constexpr ImVec2 CellPadding = ImVec2(6.0f, 3.0f);
	};

	const ImVec4 EditorTheme::AccentText = AccentLight;
	const ImVec4 EditorTheme::WarningText = Warning;
	const ImVec4 EditorTheme::ErrorText = Error;

	std::vector<std::string> EditorTheme::Fonts()
	{
		std::vector<std::string> fonts;
		std::error_code code;

		for (const auto& entry : std::filesystem::directory_iterator(FontFolder(), code))
			if (entry.path().extension() == ".ttf")
				fonts.push_back(entry.path().filename().string());

		return fonts;
	};

	void EditorTheme::LoadFont(const std::string& file, float size)
	{
		ImFontAtlas& atlas = *ImGui::GetIO().Fonts;
		atlas.Clear();

		const std::filesystem::path path = FontFolder() / file;
		std::error_code code;

		if (std::filesystem::exists(path, code))
			atlas.AddFontFromFileTTF(path.string().c_str(), size);
		else
		{
			std::cerr << "EditorTheme: no font at " << path.string() << std::endl;
			atlas.AddFontDefault();
		};
	};

	void EditorTheme::ApplyStyle()
	{
		ImGuiStyle& style = ImGui::GetStyle();

		style.WindowPadding = WindowPadding;
		style.FramePadding = FramePadding;
		style.ItemSpacing = ItemSpacing;
		style.ItemInnerSpacing = ItemInnerSpacing;
		style.CellPadding = CellPadding;
		style.IndentSpacing = IndentSpacing;
		style.ScrollbarSize = ScrollbarSize;
		style.GrabMinSize = GrabMinSize;
		style.DockingSeparatorSize = DockingSeparatorSize;

		// Square panels, since nearly all of them are docked edge to edge, and
		// rounded controls inside them.
		style.WindowRounding = 0.0f;
		style.ChildRounding = ControlRounding;
		style.FrameRounding = ControlRounding;
		style.PopupRounding = ControlRounding;
		style.TabRounding = ControlRounding;
		style.GrabRounding = GrabRounding;
		style.ScrollbarRounding = ScrollbarRounding;

		style.WindowBorderSize = 1.0f;
		style.ChildBorderSize = 1.0f;
		style.PopupBorderSize = 1.0f;
		style.FrameBorderSize = 0.0f;
		style.TabBorderSize = 0.0f;
		style.TabBarBorderSize = 1.0f;

		// Docked panels already show their tabs, so the tab-list menu button is noise.
		style.WindowMenuButtonPosition = ImGuiDir_None;

		ImVec4* colours = style.Colors;

		colours[ImGuiCol_Text] = Text;
		colours[ImGuiCol_TextDisabled] = TextDim;
		colours[ImGuiCol_TextLink] = Accent;
		colours[ImGuiCol_TextSelectedBg] = WithAlpha(Accent, SelectionAlpha);

		colours[ImGuiCol_WindowBg] = Panel;
		colours[ImGuiCol_ChildBg] = Clear;
		colours[ImGuiCol_PopupBg] = Chrome;
		colours[ImGuiCol_Border] = Edge;
		colours[ImGuiCol_BorderShadow] = Clear;

		colours[ImGuiCol_FrameBg] = Control;
		colours[ImGuiCol_FrameBgHovered] = ControlHovered;
		colours[ImGuiCol_FrameBgActive] = ControlActive;

		colours[ImGuiCol_TitleBg] = Chrome;
		colours[ImGuiCol_TitleBgActive] = Chrome;
		colours[ImGuiCol_TitleBgCollapsed] = Chrome;
		colours[ImGuiCol_MenuBarBg] = Chrome;

		colours[ImGuiCol_ScrollbarBg] = Clear;
		colours[ImGuiCol_ScrollbarGrab] = ControlHovered;
		colours[ImGuiCol_ScrollbarGrabHovered] = ControlActive;
		colours[ImGuiCol_ScrollbarGrabActive] = TextDim;

		colours[ImGuiCol_CheckMark] = Accent;
		colours[ImGuiCol_SliderGrab] = Accent;
		colours[ImGuiCol_SliderGrabActive] = AccentHovered;

		colours[ImGuiCol_Button] = Control;
		colours[ImGuiCol_ButtonHovered] = ControlHovered;
		colours[ImGuiCol_ButtonActive] = ControlActive;

		colours[ImGuiCol_Header] = WithAlpha(Accent, SelectionAlpha);
		colours[ImGuiCol_HeaderHovered] = ControlHovered;
		colours[ImGuiCol_HeaderActive] = WithAlpha(Accent, SelectionActiveAlpha);

		colours[ImGuiCol_Separator] = Edge;
		colours[ImGuiCol_SeparatorHovered] = AccentHovered;
		colours[ImGuiCol_SeparatorActive] = Accent;

		colours[ImGuiCol_ResizeGrip] = WithAlpha(Accent, GripAlpha);
		colours[ImGuiCol_ResizeGripHovered] = AccentHovered;
		colours[ImGuiCol_ResizeGripActive] = Accent;

		colours[ImGuiCol_Tab] = Chrome;
		colours[ImGuiCol_TabHovered] = ControlHovered;
		colours[ImGuiCol_TabSelected] = Panel;
		colours[ImGuiCol_TabSelectedOverline] = Accent;
		colours[ImGuiCol_TabDimmed] = Chrome;
		colours[ImGuiCol_TabDimmedSelected] = Panel;
		colours[ImGuiCol_TabDimmedSelectedOverline] = Clear;

		colours[ImGuiCol_DockingPreview] = WithAlpha(Accent, SelectionActiveAlpha);
		colours[ImGuiCol_DockingEmptyBg] = Well;

		colours[ImGuiCol_PlotLines] = Accent;
		colours[ImGuiCol_PlotLinesHovered] = AccentHovered;
		colours[ImGuiCol_PlotHistogram] = Accent;
		colours[ImGuiCol_PlotHistogramHovered] = AccentHovered;

		colours[ImGuiCol_TableHeaderBg] = Chrome;
		colours[ImGuiCol_TableBorderStrong] = Edge;
		colours[ImGuiCol_TableBorderLight] = Control;
		colours[ImGuiCol_TableRowBg] = Clear;
		colours[ImGuiCol_TableRowBgAlt] = WithAlpha(Control, SelectionAlpha);

		colours[ImGuiCol_DragDropTarget] = Accent;
		colours[ImGuiCol_NavHighlight] = Accent;
		colours[ImGuiCol_NavWindowingHighlight] = Accent;
		colours[ImGuiCol_NavWindowingDimBg] = Dimmer;
		colours[ImGuiCol_ModalWindowDimBg] = Dimmer;
	};
};
