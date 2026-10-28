#include "Editor.h"

#include "EditorFileDialogs.h"
#include "EditorGui.h"
#include "EditorIcons.h"
#include "FileTab.h"
#include "EditorTheme.h"
#include "ModelImporter.h"
#include "Prefab.h"
#include "ProjectAssets.h"
#include "ProjectTemplate.h"
#include "SpriteAsset.h"
#include "TextFit.h"

#include "Engine.h"
#include "Renderer.h"
#include "Scene.h"
#include "SceneSerializer.h"
#include "Textures.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <numbers>
#include <optional>
#include <span>
#include <thread>
#include <utility>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <objbase.h>
#include <oleauto.h>
#include <wrl/client.h>

#pragma comment(lib, "shlwapi.lib")


namespace Loom
{
	namespace
	{
		#pragma region Constants

		// Fields take files by ASSET_PAYLOAD, so a folder goes by its own.
		constexpr const char* FOLDER_PAYLOAD = "LoomFolder";

		constexpr float NAME_PROMPT_WIDTH = 320.0f;
		constexpr float PROMPT_BUTTON_WIDTH = 120.0f;

		// Long enough that reading takes the time, short enough that the bar still moves.
		constexpr double OPENING_SLICE_SECONDS = 1.0 / 15.0;

		// The widest the bar of a scene being opened gets, in lines of text.
		constexpr float OPENING_BAR_LINES = 20.0f;

		// A pinged file's row flashes this many times over this long, fading.
		constexpr double PING_SECONDS = 1.5;
		constexpr double PING_FLASHES = 3.0;
		constexpr float PING_FILL_ALPHA = 0.5f;
		constexpr float PING_OUTLINE_THICKNESS = 2.0f;
		constexpr float PING_ROUNDING = 3.0f;

		// How far NewShader1, NewShader2... counts before the user picks a name.
		constexpr int MAX_DEFAULT_NAME_SUFFIX = 1000;

		#pragma endregion

		#pragma region Field Pickers

		// A field picker's list: at least this wide, and this many rows tall.
		constexpr float PICKER_MIN_WIDTH = 240.0f;
		constexpr float PICKER_ROWS = 12.0f;
		constexpr const char* PICKER = "##picker";

		// Cut as FitPath cuts, so a path keeps its file's name.
		std::string FitPickerLabel(const char* icon, const std::string& text, float width, bool& cut)
		{
			const std::string lead = icon ? std::string(icon) + " " : std::string();
			const std::string fitted = FitPath(text, width - ImGui::CalcTextSize(lead.c_str()).x);

			cut = fitted != text;

			return lead + fitted;
		};

		void PickerTip(const std::string& tip, const std::string& text, bool cut)
		{
			if (!tip.empty())
				ImGui::SetItemTooltip("%s", tip.c_str());
			else if (cut)
				ImGui::SetItemTooltip("%s", text.c_str());
		};

		#pragma endregion

		#pragma region Asset Kinds

		struct AssetKindInfo
		{
			AssetKind kind;
			const char* icon;
			const char* label;
			const char* defaultName;

			// Scripts only: the header's template, and where its compiled type turns up.
			ProjectAssets::ScriptKind script = ProjectAssets::ScriptKind::Component;
			const char* appearsIn = nullptr;
		};

		constexpr AssetKindInfo assetKinds[] =
		{
			{ AssetKind::Folder, EditorIcons::Folder, "Folder", "New Folder" },
			{ AssetKind::Scene, EditorIcons::Scene, "Scene", "New Scene" },
			{ AssetKind::Prefab, EditorIcons::Prefab, "Prefab", "New Prefab" },
			{ AssetKind::Script, EditorIcons::Script, "Script", "NewScript", ProjectAssets::ScriptKind::Component, "Add Component" },
			{ AssetKind::State, EditorIcons::State, "State", "NewState", ProjectAssets::ScriptKind::State, "a state machine's Start and Current" },
			{ AssetKind::StateMachine, EditorIcons::Script, "State Machine", "NewStateMachine", ProjectAssets::ScriptKind::StateMachine, "Add Component" },
			{ AssetKind::DialogueManager, EditorIcons::Dialogue, "Dialogue Manager", "NewDialogueManager", ProjectAssets::ScriptKind::DialogueManager, "Add Component" },
			{ AssetKind::Shader, EditorIcons::Shader, "Shader", "NewShader" },
			{ AssetKind::Dialogue, EditorIcons::Dialogue, "Dialogue", "NewDialogue" },
			{ AssetKind::Texture, EditorIcons::Texture, "Texture", "NewTexture" },
			{ AssetKind::Palette, EditorIcons::Palette, "Palette", "NewPalette" },
		};

		const AssetKindInfo& InfoOf(AssetKind kind)
		{
			for (const AssetKindInfo& info : assetKinds)
				if (info.kind == kind)
					return info;

			return assetKinds[0];
		};

		bool IsScript(AssetKind kind)
		{
			return InfoOf(kind).appearsIn != nullptr;
		};

		// New Folder, or the first free New Folder1..., renamed in place.
		void AddNewFolder(VirtualFolders& folders, FolderTree& tree, const std::string& parent)
		{
			constexpr const char* newFolder = "New Folder";

			std::string path = folders.AddFolder(parent, newFolder);

			for (int suffix = 1; path.empty() && suffix < MAX_DEFAULT_NAME_SUFFIX; suffix++)
				path = folders.AddFolder(parent, newFolder + std::to_string(suffix));

			tree.Rename(path);
		};

		bool IsScriptsPanels(AssetKind kind)
		{
			return IsScript(kind) || kind == AssetKind::Shader || kind == AssetKind::Dialogue;
		};

		const FileTabKind* TabMaking(AssetKind kind)
		{
			return kind == AssetKind::Texture || kind == AssetKind::Palette ? &FileTabs::TwoD : nullptr;
		};

		// The FileTab that holds the file, or none.
		const FileTabKind* TabHolding(const std::string& path)
		{
			if (ProjectAssets::IsTwoDFile(path))
				return &FileTabs::TwoD;

			if (IsModelFile(path))
				return &FileTabs::ThreeD;

			if (ProjectAssets::IsSoundFile(path))
				return &FileTabs::Sounds;

			if (ProjectAssets::IsFontFile(path))
				return &FileTabs::Fonts;

			return nullptr;
		};

		#pragma endregion

		#pragma region Icons

		// What each tab lists, in the order its filter shows them.
		constexpr KindFilter::Kind projectKinds[] =
		{
			{ EditorIcons::Scene, "Scenes" },
			{ EditorIcons::Prefab, "Prefabs" },
		};

		constexpr KindFilter::Kind scriptKinds[] =
		{
			{ EditorIcons::Script, "Scripts" },
			{ EditorIcons::State, "States" },
			{ EditorIcons::Shader, "Shaders" },
			{ EditorIcons::Dialogue, "Dialogues" },
		};

		const char* IconOf(ScriptEntryKind kind)
		{
			switch (kind)
			{
			case ScriptEntryKind::State:
				return EditorIcons::State;

			case ScriptEntryKind::Shader:
				return EditorIcons::Shader;

			case ScriptEntryKind::Dialogue:
				return EditorIcons::Dialogue;

			default:
				return EditorIcons::Script;
			};
		};

		// Textures are uploaded bottom row first, so V is flipped.
		const ImVec2 thumbnailTop(0.0f, 1.0f);
		const ImVec2 thumbnailBottom(1.0f, 0.0f);

		const char* IconOf(const std::string& path)
		{
			return IsModelFile(path) ? EditorIcons::Model : EditorIcons::Of(path);
		};

		// One of ProjectTabs with its name left off.
		std::string IconOfTab(std::string_view label)
		{
			return std::string(label.substr(0, label.find(' ')));
		};

		// As wide as its icon and a line high, keeping the picture's shape.
		ImVec2 ThumbnailSize(const Texture& texture)
		{
			const float side = (std::min)(ImGui::GetFontSize(), ImGui::CalcTextSize(EditorIcons::Texture).x);
			const float longest = (float)(std::max)(texture.width, texture.height);

			return ImVec2(side * texture.width / longest, side * texture.height / longest);
		};

		// Spaces as wide as the icon a thumbnail stands in for, to draw it over.
		std::string ThumbnailSpaces()
		{
			return std::string((size_t)std::ceil(ImGui::CalcTextSize(EditorIcons::Texture).x / ImGui::CalcTextSize(" ").x), ' ');
		};

		// Over the icon's place in the tree node just drawn.
		void DrawThumbnail(const Texture& texture)
		{
			const ImVec2 size = ThumbnailSize(texture);
			const ImVec2 item = ImGui::GetItemRectMin();
			const ImVec2 corner(
				item.x + ImGui::GetTreeNodeToLabelSpacing() + (ImGui::CalcTextSize(EditorIcons::Texture).x - size.x) / 2.0f,
				item.y + (ImGui::GetItemRectSize().y - size.y) / 2.0f);

			ImGui::GetWindowDrawList()->AddImage(
				Renderer::Get()->ImGuiTexture(texture.handle),
				corner,
				ImVec2(corner.x + size.x, corner.y + size.y),
				thumbnailTop,
				thumbnailBottom);
		};

		#pragma endregion

		#pragma region Scanning

		// Folders then files, alphabetically; dot folders, build output and folders of other kinds are left out.
		bool Scan(
			const std::filesystem::path& folder,
			const std::filesystem::path& skip,
			AssetNode& node,
			std::vector<std::string>& scenes,
			std::vector<std::string>& files,
			std::vector<std::string>& folders)
		{
			std::error_code code;
			bool held = false;

			for (const auto& entry : std::filesystem::directory_iterator(folder, code))
			{
				const std::filesystem::path path = entry.path().lexically_normal();
				const std::string name = path.filename().string();

				if (name.empty() || name.front() == '.')
					continue;

				AssetNode child{ path.string(), name, entry.is_directory(code) };

				if (child.folder)
				{
					if (path == skip)
						continue;

					folders.push_back(child.path);

					const bool held_files = Scan(path, skip, child, scenes, files, folders);

					held = held || held_files;

					if (child.children.empty() && held_files)
						continue;
				}
				else
				{
					held = true;
					files.push_back(child.path);

					const bool scene = path.extension() == SceneSerializer::extension;

					if (!scene && path.extension() != Prefab::extension)
						continue;

					child.icon = IconOf(child.path);

					if (scene)
						scenes.push_back(child.path);
				};

				node.children.push_back(std::move(child));
			};

			std::sort(
				node.children.begin(),
				node.children.end(),
				[](const AssetNode& a, const AssetNode& b)
				{
					if (a.folder != b.folder)
						return a.folder;

					return _stricmp(a.name.c_str(), b.name.c_str()) < 0;
				});

			return held;
		};

		#pragma endregion

		#pragma region Shell

		void Shell(const char* file, const std::string& parameters)
		{
			const HINSTANCE result = ShellExecuteA(
				nullptr,
				"open",
				file,
				parameters.empty() ? nullptr : parameters.c_str(),
				nullptr,
				SW_SHOWNORMAL);

			// The one error code ShellExecute has: anything at or below 32.
			if ((INT_PTR)result <= 32)
				std::cerr
					<< "Could not open " << file << ' ' << parameters
					<< " (error " << (INT_PTR)result << ')' << std::endl;
		};

		void ShowInExplorer(const std::string& path)
		{
			Shell("explorer.exe", "/select,\"" + path + '"');
		};

		bool HasOpener(const std::string& extension)
		{
			char executable[MAX_PATH]{ };
			DWORD length = MAX_PATH;

			return SUCCEEDED(AssocQueryStringA(ASSOCF_NONE, ASSOCSTR_EXECUTABLE, extension.c_str(), "open", executable, &length));
		};

		// For a kind of file nothing claims, a .shader among them.
		void OpenAsText(const std::string& path)
		{
			SHELLEXECUTEINFOA info{ sizeof(info) };
			info.fMask = SEE_MASK_CLASSNAME;
			info.lpClass = ".txt";
			info.lpVerb = "open";
			info.lpFile = path.c_str();
			info.nShow = SW_SHOWNORMAL;

			if (!ShellExecuteExA(&info))
				std::cerr << "Could not open " << path << " (error " << GetLastError() << ')' << std::endl;
		};

		void OpenFile(const std::string& path)
		{
			if (HasOpener(std::filesystem::path(path).extension().string()))
				Shell(path.c_str(), "");
			else
				OpenAsText(path);
		};

		#pragma endregion

		#pragma region Visual Studio

		using Microsoft::WRL::ComPtr;

		// IDispatch takes the arguments last first.
		HRESULT Call(
			IDispatch* object,
			const wchar_t* member,
			WORD kind,
			VARIANT* result,
			std::span<VARIANT> arguments = { })
		{
			DISPID id = 0;
			LPOLESTR name = const_cast<LPOLESTR>(member);

			const HRESULT found = object->GetIDsOfNames(IID_NULL, &name, 1, LOCALE_USER_DEFAULT, &id);

			if (FAILED(found))
				return found;

			DISPPARAMS parameters{ arguments.data(), nullptr, (UINT)arguments.size(), 0 };

			return object->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, kind, &parameters, result, nullptr, nullptr);
		};

		ComPtr<IDispatch> Property(IDispatch* object, const wchar_t* member)
		{
			VARIANT value;
			VariantInit(&value);

			if (FAILED(Call(object, member, DISPATCH_PROPERTYGET, &value)) || value.vt != VT_DISPATCH)
			{
				VariantClear(&value);
				return nullptr;
			};

			ComPtr<IDispatch> result;
			result.Attach(value.pdispVal);

			return result;
		};

		VARIANT Integer(int value)
		{
			VARIANT integer;
			VariantInit(&integer);
			integer.vt = VT_I4;
			integer.lVal = value;

			return integer;
		};

		// The first column when none is given.
		HRESULT GoTo(IDispatch* window, int line, int column)
		{
			const ComPtr<IDispatch> document = Property(window, L"Document");
			const ComPtr<IDispatch> selection = document ? Property(document.Get(), L"Selection") : nullptr;

			if (!selection)
				return E_NOINTERFACE;

			VARIANT position[]{ Integer((std::max)(column, 1)), Integer(line) };

			return Call(selection.Get(), L"MoveToLineAndOffset", DISPATCH_METHOD, nullptr, position);
		};

		// What a Visual Studio in the middle of a build or a modal dialog answers.
		bool IsBusy(HRESULT result)
		{
			return result == RPC_E_CALL_REJECTED || result == RPC_E_SERVERCALL_RETRYLATER;
		};

		// Each Visual Studio registers as "!VisualStudio.DTE.<version>:<pid>"; busy when one will not answer.
		ComPtr<IDispatch> FindVisualStudioWith(const std::string& solution, DWORD& process, bool& busy)
		{
			ComPtr<IRunningObjectTable> table;
			ComPtr<IEnumMoniker> monikers;
			ComPtr<IBindCtx> context;

			if (FAILED(GetRunningObjectTable(0, &table)) ||
				FAILED(table->EnumRunning(&monikers)) ||
				FAILED(CreateBindCtx(0, &context)))
				return nullptr;

			const std::wstring prefix = L"!VisualStudio.DTE.";

			ComPtr<IMoniker> moniker;

			while (monikers->Next(1, moniker.ReleaseAndGetAddressOf(), nullptr) == S_OK)
			{
				LPOLESTR name = nullptr;

				if (FAILED(moniker->GetDisplayName(context.Get(), nullptr, &name)))
					continue;

				const std::wstring display = name;
				CoTaskMemFree(name);

				ComPtr<IUnknown> object;
				ComPtr<IDispatch> dte;

				if (display.rfind(prefix, 0) != 0 ||
					FAILED(table->GetObject(moniker.Get(), &object)) ||
					FAILED(object.As(&dte)))
					continue;

				VARIANT open;
				VARIANT path;
				VariantInit(&open);
				VariantInit(&path);

				HRESULT result = Call(dte.Get(), L"Solution", DISPATCH_PROPERTYGET, &open);

				if (SUCCEEDED(result) && open.vt == VT_DISPATCH)
					result = Call(open.pdispVal, L"FullName", DISPATCH_PROPERTYGET, &path);

				busy = busy || IsBusy(result);

				std::error_code code;

				const bool match =
					SUCCEEDED(result) &&
					path.vt == VT_BSTR &&
					SysStringLen(path.bstrVal) > 0 &&
					std::filesystem::equivalent(std::filesystem::path(path.bstrVal), solution, code);

				VariantClear(&path);
				VariantClear(&open);

				if (match)
				{
					process = wcstoul(display.substr(display.rfind(L':') + 1).c_str(), nullptr, 10);
					return dte;
				};
			};

			return nullptr;
		};

		enum struct Handover
		{
			Opened,
			NotRunning,

			Busy,

			Failed,
		};

		// A line of zero leaves the caret where Visual Studio puts it.
		Handover OpenThroughAutomation(const std::string& solution, const std::string& file, int line, int column)
		{
			DWORD process = 0;
			bool busy = false;

			const ComPtr<IDispatch> dte = FindVisualStudioWith(solution, process, busy);

			if (!dte)
				return busy ? Handover::Busy : Handover::NotRunning;

			const ComPtr<IDispatch> operations = Property(dte.Get(), L"ItemOperations");
			const ComPtr<IDispatch> window = Property(dte.Get(), L"MainWindow");

			VARIANT path;
			VariantInit(&path);
			path.vt = VT_BSTR;
			path.bstrVal = SysAllocString(std::filesystem::path(file).wstring().c_str());

			VARIANT opened;
			VariantInit(&opened);

			const HRESULT result = operations
				? Call(operations.Get(), L"OpenFile", DISPATCH_METHOD, &opened, { &path, 1 })
				: E_NOINTERFACE;

			const HRESULT moved = SUCCEEDED(result) && line > 0 && opened.vt == VT_DISPATCH
				? GoTo(opened.pdispVal, line, column)
				: S_OK;

			VariantClear(&opened);
			VariantClear(&path);

			if (IsBusy(result))
				return Handover::Busy;

			if (FAILED(result))
			{
				std::cerr
					<< "Visual Studio has " << solution << " open but would not open "
					<< file << " (error 0x" << std::hex << result << std::dec << ')' << std::endl;

				return Handover::Failed;
			};

			if (FAILED(moved))
				std::cerr
					<< "Visual Studio opened " << file << " but would not go to line " << line
					<< " (error 0x" << std::hex << moved << std::dec << ')' << std::endl;

			// Windows only lets another process take the foreground when the holder allows it.
			AllowSetForegroundWindow(process);

			if (window)
				Call(window.Get(), L"Activate", DISPATCH_METHOD, nullptr);

			return Handover::Opened;
		};

		// The editor's thread does not keep COM up, so the call brings it up.
		bool OpenInRunningVisualStudio(const std::string& solution, const std::string& file, int line, int column)
		{
			const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

			const Handover handover = OpenThroughAutomation(solution, file, line, column);

			// A busy one may have the solution, and starting another would open it twice.
			if (handover == Handover::Busy)
				std::cerr << "Visual Studio is busy; try again once it has finished." << std::endl;

			if (SUCCEEDED(com))
				CoUninitialize();

			return handover != Handover::NotRunning;
		};

		// Long enough for a cold start of Visual Studio on a large solution.
		constexpr std::chrono::seconds visualStudioStartTimeout{ 180 };
		constexpr std::chrono::milliseconds visualStudioPollInterval{ 500 };

		// A fresh Visual Studio answers nothing until loaded, and /Command File.OpenFile fails with dialogs.
		void OpenOnceLoaded(const std::string& solution, const std::string& file, int line, int column)
		{
			std::thread(
				[solution, file, line, column]()
				{
					const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

					const auto deadline = std::chrono::steady_clock::now() + visualStudioStartTimeout;

					Handover handover = Handover::NotRunning;

					while (std::chrono::steady_clock::now() < deadline)
					{
						handover = OpenThroughAutomation(solution, file, line, column);

						if (handover == Handover::Opened || handover == Handover::Failed)
							break;

						std::this_thread::sleep_for(visualStudioPollInterval);
					};

					if (handover == Handover::NotRunning || handover == Handover::Busy)
						std::cerr << "Visual Studio did not load " << solution << " in time to open " << file << std::endl;

					if (SUCCEEDED(com))
						CoUninitialize();
				}).detach();
		};

		#pragma endregion

		#pragma region Models

		// Empty when unreadable; imported into a scene of its own, deleted as a closed scene is.
		std::string ImportedPrefab(const std::string& path)
		{
			Scene* scene = new Scene(std::filesystem::path(path).stem().string());
			Scene::Unlist(scene);

			std::string text;

			if (GameObject* model = ImportModel(path, scene->GetRoot()))
			{
				// Children join their parents through the queue.
				Engine::DoTasks();

				Prefabs::ObjectNode prefab;

				if (Prefabs::Parse(SceneSerializer::Serialize(*model), prefab))
					text = Prefabs::Write(Prefabs::Isolated(prefab));
			};

			Engine::QueueTask([scene]() { delete scene; });

			return text;
		};

		#pragma endregion
	};

	#pragma region Project Panel

	void Editor::RefreshProjectAssets()
	{
		if (m_projectWatch.GetFolder() != m_projectPath)
			m_projectWatch.Watch(m_projectPath, false);

		m_projectScenes.clear();
		m_projectFiles.clear();
		m_projectFolders.clear();
		m_projectShaders.clear();
		m_projectDialogues.clear();
		m_noThumbnail.clear();

		for (FileTab& tab : m_fileTabs)
			tab.files.clear();

		m_assets = AssetNode{ m_projectPath, "", true };

		if (!m_projectPath.empty())
		{
			const std::filesystem::path build = m_scripts.HasProject()
				? std::filesystem::path(m_scripts.GetLibrary()).parent_path().lexically_normal()
				: std::filesystem::path();

			std::vector<std::string> files{ };
			std::vector<std::string> folders{ };

			Scan(m_projectPath, build, m_assets, m_projectScenes, files, folders);

			std::sort(m_projectScenes.begin(), m_projectScenes.end());

			for (const std::string& folder : folders)
				m_projectFolders.push_back(ProjectAssets::RelativeTo(folder, m_projectPath));

			std::sort(m_projectFolders.begin(), m_projectFolders.end());

			for (const std::string& file : files)
			{
				m_projectFiles.push_back(ProjectAssets::RelativeTo(file, m_projectPath));

				if (FileTab* tab = FileTabOf(file))
					tab->files.push_back(m_projectFiles.back());
				else if (ProjectAssets::IsShaderFile(file))
					m_projectShaders.push_back(file);
				else if (ProjectAssets::IsDialogueFile(file))
					m_projectDialogues.push_back(file);
			};
		};

		// After the scan, which finds the shaders and dialogues.
		RefreshScripts();
	};

	void Editor::DrawProject()
	{
		if (m_focusProject)
		{
			ImGui::SetNextWindowFocus();
			m_focusProject = false;
		};

		ImGui::SetNextWindowSizeConstraints(ImVec2(m_projectMinWidth, 0.0f), ImVec2(FLT_MAX, FLT_MAX));

		if (ImGui::Begin(EditorPanels::Project, &m_showProject))
		{
			if (m_projectPath.empty())
			{
				m_projectMinWidth = 0.0f;
				ImGui::TextDisabled("No scene folder chosen.");

				if (ImGui::Button("Choose Folder..."))
					m_dialogs->folder.Open();
			}
			else
			{
				// A change anywhere in the project rescans its shaders as well as the scripts.
				if (m_projectWatch.Changed())
					RefreshProjectAssets();
				else if (m_scriptWatch.Changed())
					RefreshScripts();

				const char* const change = "Change...";

				const float button_width =
					ImGui::CalcTextSize(change).x + ImGui::GetStyle().FramePadding.x * 2.0f;
				const float button_x = ImGui::GetContentRegionMax().x - button_width;

				ImGui::AlignTextToFramePadding();
				const std::string shown_path = FitPath(
					m_projectPath,
					button_x - ImGui::GetStyle().ItemSpacing.x - ImGui::GetCursorPosX());

				ImGui::TextUnformatted(shown_path.c_str());

				if (shown_path != m_projectPath)
					ImGui::SetItemTooltip("%s", m_projectPath.c_str());

				ImGui::SameLine(button_x);

				if (ImGui::Button(change))
					m_dialogs->folder.Open();

				// Tabs drop to icons once their names do not fit, and scroll on a screen too narrow for those.
				const ImGuiStyle& style = ImGui::GetStyle();
				float names_width = -style.ItemInnerSpacing.x;
				float icons_width = -style.ItemInnerSpacing.x;
				const auto add_width = [&names_width, &icons_width, &style](const char* label)
				{
					names_width += ImGui::TabItemCalcSize(label, false).x + style.ItemInnerSpacing.x;
					icons_width += ImGui::TabItemCalcSize(IconOfTab(label).c_str(), false).x + style.ItemInnerSpacing.x;
				};

				add_width(ProjectTabs::Scenes);
				add_width(ProjectTabs::Scripts);

				for (const FileTab& tab : m_fileTabs)
					add_width(tab.kind.title);

				const bool icons_only = names_width > ImGui::GetContentRegionAvail().x;
				m_projectMinWidth = icons_width + style.WindowPadding.x * 2.0f;

				if (ImGui::BeginTabBar("##project_tabs", ImGuiTabBarFlags_FittingPolicyScroll))
				{
					const auto begin_tab = [this, icons_only](const char* label)
					{
						const bool show = label == m_projectTabToShow;

						if (show)
							m_projectTabToShow = { };

						const std::string icon = IconOfTab(label);
						const std::string shown = (icons_only ? icon : std::string(label)) + "###" + label;
						ImGuiTabItemFlags flags = show ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;

						if (icons_only)
							flags |= ImGuiTabItemFlags_NoTooltip;

						const bool open = ImGui::BeginTabItem(shown.c_str(), nullptr, flags);

						if (icons_only)
							ImGui::SetItemTooltip("%s", label + icon.size() + 1);

						if (!open)
							return false;

						m_projectTab = label;

						return true;
					};

					if (begin_tab(ProjectTabs::Scenes))
					{
						DrawScenes();
						ImGui::EndTabItem();
					};

					if (begin_tab(ProjectTabs::Scripts))
					{
						DrawScripts();
						ImGui::EndTabItem();
					};

					for (FileTab& tab : m_fileTabs)
						if (begin_tab(tab.kind.title))
						{
							DrawFileTab(tab);
							ImGui::EndTabItem();
						};

					ImGui::EndTabBar();
				};
			};
		};

		ImGui::End();
	};

	void Editor::ShowProjectTab(std::string_view tab)
	{
		m_showProject = true;
		m_focusProject = true;
		m_projectTabToShow = tab;
	};

	#pragma endregion

	#pragma region Scenes Tab

	void Editor::DrawScenes()
	{
		if (m_revealSelectedAsset)
		{
			m_assetSearch.Clear();

			if (std::error_code code; !std::filesystem::is_directory(m_selectedAsset, code))
				m_projectFilter.Show(IconOf(m_selectedAsset), true);
		};

		m_assetSearch.Draw();

		ImGui::BeginChild("##assets", m_projectFilter.DrawUnder(projectKinds), ImGuiChildFlags_Borders);


		m_assetSearch.BeginResults();

		for (const AssetNode& node : m_assets.children)
			if (ShowsAsset(node))
				DrawAssetNode(node, 0, false);

		m_assetSearch.EndResults();

		// Dropped anywhere else in the list, it goes in the project folder.
		TakeImportFolder(m_projectPath, ImGui::IsWindowHovered());

		// So a row that turns a drop away does not pass it to the project folder.
		const ImGuiWindow* list = ImGui::GetCurrentWindow();
		const ImRect below(
			list->InnerRect.Min.x,
			(std::max)(ImGui::GetCursorScreenPos().y, list->InnerRect.Min.y),
			list->InnerRect.Max.x,
			list->InnerRect.Max.y);

		if (below.GetHeight() > 0.0f && ImGui::BeginDragDropTargetCustom(below, list->ID))
		{
			AcceptMove(m_projectPath);
			ImGui::EndDragDropTarget();
		};

		m_revealSelectedAsset = false;

		if (ImGui::BeginPopupContextWindow(
			"##project_create",
			ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
		{
			DrawCreateMenu(m_projectPath);
			ImGui::EndPopup();
		};

		ImGui::EndChild();

		if (!m_assetToMove.empty())
			MoveAsset(std::exchange(m_assetToMove, { }), m_assetMoveFolder);

		if (GameObject* dropped = LoomObject::GetByGuid<GameObject>(std::exchange(m_objectToSave, Guid())))
			SaveAsPrefab(dropped, m_assetMoveFolder);

		if (!m_assetToOpen.empty())
			OpenAsset(std::exchange(m_assetToOpen, { }));
	};

	void Editor::DrawAssetNode(const AssetNode& node, int depth, bool found)
	{
		const char* icon = node.folder ? EditorIcons::Folder : node.icon;

		if (!m_projectFilter.Shows(icon))
			return;

		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;

		if (node.path == m_selectedAsset)
			flags |= ImGuiTreeNodeFlags_Selected;

		if (node.folder)
		{
			flags |= ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;

			// The top level is only a handful of folders, and the scenes are in one of them.
			if (depth == 0)
				flags |= ImGuiTreeNodeFlags_DefaultOpen;

			m_assetSearch.OpenFound();

			if (m_revealSelectedAsset && ProjectAssets::IsUnder(m_selectedAsset, node.path))
				ImGui::SetNextItemOpen(true);
		}
		else
			flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

		const bool open = ImGui::TreeNodeEx(node.path.c_str(), flags, "%s %s", icon, node.name.c_str());

		if (m_revealSelectedAsset && node.path == m_selectedAsset)
			ImGui::ScrollToItem(ImGuiScrollFlags_KeepVisibleCenterY);

		if (node.path == m_pingedAsset)
		{
			const double pinged = ImGui::GetTime() - m_pingedSince;

			if (pinged < PING_SECONDS)
			{
				const double fade = 1.0 - pinged / PING_SECONDS;
				const double flash = (1.0 + std::cos(pinged / PING_SECONDS * PING_FLASHES * 2.0 * std::numbers::pi)) * 0.5;
				ImVec4 colour = EditorTheme::AccentText;
				ImDrawList* draw = ImGui::GetWindowDrawList();

				colour.w = (float)(flash * fade);
				draw->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::GetColorU32(colour), PING_ROUNDING, 0, PING_OUTLINE_THICKNESS);
				colour.w *= PING_FILL_ALPHA;
				draw->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::GetColorU32(colour), PING_ROUNDING);
			}
			else m_pingedAsset.clear();
		};

		if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
		{
			m_selectedAsset = node.path;

			// A model shows in the Inspector, where it can be imported.
			if (!node.folder && IsModelFile(node.path))
				SelectModel(node.path);
			else m_selectedModel.clear();
		};

		if (!node.folder &&
			ImGui::IsItemHovered() &&
			ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			m_assetToOpen = node.path;

		// Dropped on a file, it goes beside it.
		const std::string folder = node.folder ? node.path : std::filesystem::path(node.path).parent_path().string();

		TakeImportFolder(folder, ImGui::IsItemHovered());

		DragAsset(node);

		if (ImGui::BeginDragDropTarget())
		{
			AcceptMove(folder);
			ImGui::EndDragDropTarget();
		};

		ImGui::SetItemTooltip("%s", node.path.c_str());

		DrawAssetContextMenu(node);

		if (node.folder && open)
		{
			found = found || m_assetSearch.Matches(node.name);

			for (const AssetNode& child : node.children)
				if (found || ShowsAsset(child))
					DrawAssetNode(child, depth + 1, found);

			ImGui::TreePop();
		};
	};

	bool Editor::ShowsAsset(const AssetNode& node) const
	{
		if (!node.folder && !m_projectFilter.Shows(node.icon))
			return false;

		return m_assetSearch.Matches(node.name) ||
			std::ranges::any_of(node.children, [this](const AssetNode& child) { return ShowsAsset(child); });
	};

	#pragma endregion

	#pragma region Create Menus

	void Editor::DrawAssetContextMenu(const AssetNode& node)
	{
		if (!ImGui::BeginPopupContextItem())
			return;

		m_selectedAsset = node.path;

		if (!node.folder && ImGui::MenuItem("Open"))
			m_assetToOpen = node.path;

		if (std::filesystem::path(node.path).extension() == Prefab::extension &&
			ImGui::MenuItem((std::string(EditorIcons::Model) + " Edit Model").c_str()))
			OpenModel(node.path);

		if (ImGui::MenuItem("Show in Explorer"))
			ShowInExplorer(node.path);

		ImGui::Separator();

		DrawCreateMenu(
			node.folder
				? node.path
				: std::filesystem::path(node.path).parent_path().string());

		ImGui::EndPopup();
	};

	void Editor::DrawCreateMenu(const std::string& folder)
	{
		if (!ImGui::BeginMenu("Create"))
			return;

		for (const AssetKindInfo& info : assetKinds)
			if (!IsScriptsPanels(info.kind) && TabMaking(info.kind) == nullptr && ImGui::MenuItem((std::string(info.icon) + " " + info.label).c_str()))
				AskForAsset(info.kind, folder);

		ImGui::EndMenu();
	};

	void Editor::DrawScriptsCreateMenu(const std::string& folder)
	{
		if (!ImGui::BeginMenu("Create"))
			return;

		// Kept in the project file's .loomscripts, so not without one.
		if (ImGui::MenuItem((std::string(EditorIcons::Folder) + " Folder").c_str(), nullptr, false, !m_projectFile.empty()))
		{
			AddNewFolder(m_scriptFolders, m_scriptFolderTree, folder);
			SaveScriptFolders();
		};

		for (const AssetKindInfo& info : assetKinds)
			if (IsScriptsPanels(info.kind) && ImGui::MenuItem((std::string(info.icon) + " " + info.label).c_str()))
			{
				AskForAsset(info.kind, m_projectPath);
				m_newScriptFolder = folder;
			};

		ImGui::EndMenu();
	};

	#pragma endregion

	#pragma region Creating Assets

	void Editor::AskForAsset(AssetKind kind, const std::string& folder)
	{
		const AssetKindInfo& info = InfoOf(kind);

		m_newAssetKind = kind;
		m_newAssetFolder = folder;
		m_newScriptFolder.clear();
		m_newScriptFor = Guid();

		for (FileTab& tab : m_fileTabs)
			tab.newFolder.clear();

		m_askForAsset = true;
		m_focusAssetName = true;

		// NewShader, NewShader1, ...: the first name nothing is using.
		snprintf(m_newAssetName, sizeof(m_newAssetName), "%s", info.defaultName);

		m_newAssetProblem = NewAssetProblem();

		for (int suffix = 1; !m_newAssetProblem.empty() && suffix < MAX_DEFAULT_NAME_SUFFIX; suffix++)
		{
			snprintf(m_newAssetName, sizeof(m_newAssetName), "%s%d", info.defaultName, suffix);
			m_newAssetProblem = NewAssetProblem();
		};
	};

	std::string Editor::NewAssetFolder() const
	{
		if (!IsScript(m_newAssetKind))
			return m_newAssetFolder;

		const std::string scripts =
			std::filesystem::path(ScriptsProject()).parent_path().string();

		return ProjectAssets::IsUnder(m_newAssetFolder, scripts)
			? m_newAssetFolder
			: scripts;
	};

	std::string Editor::ScriptsProject() const
	{
		return m_scripts.HasProject()
			? m_scripts.GetProject()
			: ProjectTemplate::ScriptsProject(m_projectPath, m_projectName);
	};

	std::string Editor::NewAssetPath() const
	{
		const std::filesystem::path folder = NewAssetFolder();
		const std::string name = m_newAssetName;

		if (IsScript(m_newAssetKind))
			return (folder / (name + ".hpp")).string();

		switch (m_newAssetKind)
		{
		case AssetKind::Scene:		return (folder / (name + SceneSerializer::extension)).string();
		case AssetKind::Prefab:		return (folder / (name + Prefab::extension)).string();
		case AssetKind::Shader:		return (folder / (name + ProjectAssets::shaderExtension)).string();
		case AssetKind::Dialogue:	return (folder / (name + ProjectAssets::dialogueExtension)).string();
		case AssetKind::Texture:	return (folder / (name + ProjectAssets::textureExtension)).string();
		default:					return (folder / name).string();
		};
	};

	std::string Editor::NewAssetProblem() const
	{
		if (IsScript(m_newAssetKind))
			return ProjectAssets::ScriptNameProblem(ScriptsProject(), m_newAssetName);

		if (!ProjectAssets::IsValidName(m_newAssetName))
			return "Not a usable file name.";

		const std::string path = NewAssetPath();

		std::error_code code;

		return std::filesystem::exists(path, code)
			? path + " already exists."
			: "";
	};

	void Editor::CreateScriptAsset(AssetKind kind, const std::string& name)
	{
		m_newAssetKind = kind;
		m_newAssetFolder = m_projectPath;
		m_newScriptFolder.clear();
		m_newScriptFor = Guid();
		snprintf(m_newAssetName, sizeof(m_newAssetName), "%s", name.c_str());

		CreateAsset();
	};

	std::string Editor::CreateAsset()
	{
		const std::string folder = NewAssetFolder();
		const std::string name = m_newAssetName;

		std::string error;
		std::string path;

		switch (m_newAssetKind)
		{
		case AssetKind::Folder:
			path = ProjectAssets::CreateFolder(folder, name, &error);
			break;

		case AssetKind::Scene:
			if (Scene* scene = CreateScene(name); SaveScene(scene, NewAssetPath()))
				path = NewAssetPath();
			else
			{
				CloseScene(scene);
				error = "the scene could not be saved";
			};
			break;

		case AssetKind::Prefab:
		{
			// A GameObject of its own, so the file has every field; deleted once registered.
			Scene* blank = new Scene(name);
			Scene::Unlist(blank);
			blank->GetRoot().SetName(name);

			Prefabs::ObjectNode prefab;
			const bool parsed = Prefabs::Parse(SceneSerializer::Serialize(blank->GetRoot()), prefab);

			Engine::QueueTask([blank]() { delete blank; });

			if (parsed &&
				WritePrefab(ProjectAssets::RelativeTo(NewAssetPath(), m_projectPath), prefab))
				path = NewAssetPath();
			else error = "the prefab could not be written";
			break;
		};

		case AssetKind::Script:
		case AssetKind::State:
		case AssetKind::StateMachine:
		case AssetKind::DialogueManager:
		{
			if (!m_scripts.HasProject())
			{
				if (ProjectTemplate::AddScripts(m_projectPath, m_projectName, &error).empty())
					break;

				// The scripts project may already be under the folder, so it is loaded first.
				LoadProjectFile();
			};

			path = ProjectAssets::CreateScript(
				ScriptsProject(),
				NewAssetFolder(),
				name,
				InfoOf(m_newAssetKind).script,
				&error);
			break;
		};

		case AssetKind::Shader:
			path = ProjectAssets::CreateShader(folder, name, &error);
			break;

		case AssetKind::Dialogue:
			path = ProjectAssets::CreateDialogue(folder, name, &error);
			break;

		case AssetKind::Texture:
			path = ProjectAssets::CreateTexture(folder, name, &error);
			break;

		case AssetKind::Palette:
			path = ProjectAssets::CreatePalette(folder, name, &error);
			break;
		};

		if (path.empty())
		{
			std::cerr << "Could not create " << name << ": " << error << std::endl;
			return "";
		};

		m_askForAsset = false;

		if (IsScript(m_newAssetKind) && m_newScriptFor.IsValid())
		{
			m_attachAfterBuildTo = std::exchange(m_newScriptFor, Guid());
			m_attachAfterBuild = name;
			CompileScripts();
		}
		else if (const char* appearsIn = InfoOf(m_newAssetKind).appearsIn)
			std::cout << name << " shows up in " << appearsIn << " after the next compile." << std::endl;

		if (IsScriptsPanels(m_newAssetKind))
		{
			// A shader is listed by its path from the project.
			const std::string item = IsScript(m_newAssetKind) ? name : ProjectAssets::RelativeTo(path, m_projectPath);

			m_scriptFolders.Move(item, m_newScriptFolder);
			SaveScriptFolders();

			m_selectedScript = item;
			m_revealSelectedScript = true;
			ShowProjectTab(ProjectTabs::Scripts);
		}
		else if (FileTab* tab = FileTabOf(path))
		{
			tab->folders.Move(ProjectAssets::RelativeTo(path, m_projectPath), tab->newFolder);
			SaveFileFolders(*tab);

			RevealFile(path);
		}
		else
		{
			m_selectedAsset = std::filesystem::path(path).lexically_normal().string();
			m_revealSelectedAsset = true;
		};

		RefreshProjectAssets();

		return path;
	};

	void Editor::DrawCreateAssetPrompt()
	{
		if (!m_askForAsset)
			return;

		const AssetKindInfo& info = InfoOf(m_newAssetKind);
		const std::string title = std::string(info.icon) + " Create " + info.label + "###create_asset";

		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

		if (ImGui::Begin(
			title.c_str(),
			&m_askForAsset,
			ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoDocking |
			ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoSavedSettings))
		{
			if (m_focusAssetName)
			{
				ImGui::SetKeyboardFocusHere();
				m_focusAssetName = false;
			};

			ImGui::SetNextItemWidth(NAME_PROMPT_WIDTH);

			const bool entered = ImGui::InputText(
				"Name",
				m_newAssetName,
				sizeof(m_newAssetName),
				ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);

			// Checked on edit, since for a script it walks the scripts project.
			if (ImGui::IsItemEdited())
				m_newAssetProblem = NewAssetProblem();

			const bool usable = m_newAssetProblem.empty();

			ImGui::TextDisabled("%s", usable ? NewAssetPath().c_str() : m_newAssetProblem.c_str());

			ImGui::BeginDisabled(!usable);

			// Opened here rather than in CreateAsset, so --create-script leaves Visual Studio closed.
			if (ImGui::Button("Create", ImVec2(PROMPT_BUTTON_WIDTH, 0.0f)) || (entered && usable))
				if (const std::string path = CreateAsset(); !path.empty() && IsScript(m_newAssetKind))
					OpenAsset(path);

			ImGui::EndDisabled();

			ImGui::SameLine();

			const bool escaped =
				ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
				ImGui::IsKeyPressed(ImGuiKey_Escape);

			if (ImGui::Button("Cancel", ImVec2(PROMPT_BUTTON_WIDTH, 0.0f)) || escaped)
				m_askForAsset = false;
		};

		ImGui::End();
	};

	#pragma endregion

	#pragma region Visual Studio

	void Editor::OpenInVisualStudio(const std::string& path, int line, int column)
	{
		if (!opensVisualStudio)
			return;

		// A hand-written project need not have a solution beside it.
		const std::string solution = m_scripts.HasProject()
			? std::filesystem::path(m_scripts.GetProject()).replace_extension(".sln").string()
			: "";

		std::error_code code;
		const bool has_solution = !solution.empty() && std::filesystem::exists(solution, code);

		if (has_solution && OpenInRunningVisualStudio(solution, path, line, column))
			return;

		const std::string& devenv = ScriptLibrary::FindVisualStudio();

		if (devenv.empty())
			Shell((has_solution ? solution : path).c_str(), "");
		else if (has_solution)
		{
			Shell(devenv.c_str(), '"' + solution + '"');
			OpenOnceLoaded(solution, path, line, column);
		}
		else
			Shell(devenv.c_str(), "/Edit \"" + path + '"');
	};

	#pragma endregion

	#pragma region Opening Scenes

	void Editor::BeginOpeningScene(const std::string& path)
	{
		std::string error;

		m_opening = SceneSerializer::BeginLoadFromFile(path, &error);
		m_openingPath = std::filesystem::path(path).lexically_normal().string();

		if (!m_opening)
			std::cerr << "Could not open scene: " << error << std::endl;
	};

	void Editor::StepOpening()
	{
		if (!m_opening && m_scripts.IsIdle())
			m_projectLoad.reset();

		if (!m_opening || m_opening->Step(OPENING_SLICE_SECONDS))
			return;

		FinishOpening();

		if (std::exchange(m_playAfterCompile, false))
			BeginPlay();
	};

	void Editor::FinishOpening()
	{
		if (!m_opening)
			return;

		std::string error;
		Scene* scene = std::exchange(m_opening, nullptr)->Finish(&error);
		const std::string path = std::exchange(m_openingPath, { });

		if (scene == nullptr)
			std::cerr << "Could not open scene: " << error << std::endl;
		else AdoptScene(scene, path);
	};

	void Editor::DrawLoading()
	{
		if (!m_opening && (m_scripts.IsIdle() || m_activeScene != nullptr))
			return;

		// A prompt only comes to the front as it takes focus, so the bar would cover it.
		if (m_askForLayoutName || m_askForFolder || m_askForNewProject || m_askForAsset ||
			ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
			return;

		constexpr float half = 0.5f;
		constexpr float sides = 2.0f;

		constexpr ImGuiWindowFlags flags =
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
			ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing;

		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		const std::string step = m_opening
			? "Opening " + std::filesystem::path(m_openingPath).stem().string()
			: std::string("Compiling scripts");

		// A negative fraction is ImGui's endless sweeping bar.
		float fraction = m_opening
			? m_opening->Progress()
			: m_scripts.BuildProgress().value_or(-(float)ImGui::GetTime());

		if (m_projectLoad && fraction >= 0.0f)
			fraction = m_projectLoad->Fraction(m_opening != nullptr, fraction);

		const float width = (std::min)(
			viewport->WorkSize.x - ImGui::GetStyle().WindowPadding.x * sides,
			ImGui::GetFontSize() * OPENING_BAR_LINES);

		ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_Always, ImVec2(half, half));
		ImGui::SetNextWindowViewport(viewport->ID);

		if (ImGui::Begin("##Loading", nullptr, flags))
		{
			// Focusing a panel brings its dock host to the front, over the bar.
			ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

			// A project's name goes above its bar and the step below.
			if (m_projectLoad)
			{
				ImGui::Text("Loading %s", m_projectName.c_str());
				ImGui::ProgressBar(fraction, ImVec2(width, 0.0f));
				ImGui::TextDisabled("%s", step.c_str());
			}
			else
			{
				ImGui::TextUnformatted(step.c_str());
				ImGui::ProgressBar(fraction, ImVec2(width, 0.0f));
			};
		};

		ImGui::End();
	};

	void Editor::DrawNoScene()
	{
		// The bar over the whole editor stands in for what the view holds.
		if (m_opening || !m_scripts.IsIdle())
			return;

		constexpr float half = 0.5f;

		const ImGuiStyle& style = ImGui::GetStyle();
		const ImVec2 available = ImGui::GetContentRegionAvail();
		const ImVec2 corner = ImGui::GetCursorPos();

		// Centred in the view, or from its top where it does not fit.
		const auto centre =
			[&](float width, float height)
			{
				ImGui::SetCursorPos(ImVec2(
					corner.x + (std::max)(0.0f, (available.x - width) * half),
					corner.y + (std::max)(0.0f, (available.y - height) * half)));
			};

		if (m_projectPath.empty())
		{
			ImGui::TextDisabled("No project open.");
			return;
		};

		if (m_projectScenes.empty())
		{
			ImGui::TextDisabled("No scenes");
			return;
		};

		const char* const title = "Open a scene";

		std::vector<std::string> labels;
		float list_width = ImGui::CalcTextSize(title).x;

		for (const std::string& scene : m_projectScenes)
		{
			labels.push_back(std::string(EditorIcons::Scene) + ' ' + ProjectAssets::RelativeTo(scene, m_projectPath));
			list_width = (std::max)(list_width, ImGui::CalcTextSize(labels.back().c_str()).x);
		};

		list_width = (std::min)(available.x, list_width + style.WindowPadding.x * 2.0f);

		const float title_height = ImGui::GetTextLineHeightWithSpacing();
		const float list_height = (std::min)(
			available.y - title_height,
			(float)labels.size() * ImGui::GetTextLineHeightWithSpacing() + style.WindowPadding.y * 2.0f);

		centre(list_width, title_height + list_height);

		ImGui::BeginGroup();
		ImGui::TextDisabled("%s", title);
		ImGui::BeginChild("##scenes", ImVec2(list_width, list_height), ImGuiChildFlags_Borders);

		// Opened once the list is drawn: opening rescans the project's scenes.
		std::string chosen;

		for (size_t i = 0; i < labels.size(); i++)
			if (ImGui::Selectable(labels[i].c_str()))
				chosen = m_projectScenes[i];

		ImGui::EndChild();
		ImGui::EndGroup();

		if (!chosen.empty())
			OpenSceneAlone(chosen);
	};

	#pragma endregion

	#pragma region Opening Assets

	void Editor::OpenAsset(const std::string& path, bool preview)
	{
		if (IsModelFile(path))
		{
			OpenModel(path);
			return;
		};

		const std::string scripts_folder = m_scripts.HasProject()
			? std::filesystem::path(m_scripts.GetProject()).parent_path().string()
			: "";

		switch (ProjectAssets::OpenerOf(path, scripts_folder))
		{
		case ProjectAssets::Opener::Editor:
			OpenSceneAlone(path);
			break;

		case ProjectAssets::Opener::Prefab:
			OpenPrefab(std::filesystem::path(path).is_absolute()
				? ProjectAssets::RelativeTo(path, m_projectPath)
				: std::filesystem::path(path).generic_string());
			break;

		case ProjectAssets::Opener::Shaders:
			OpenShader(path);
			break;

		case ProjectAssets::Opener::Dialogues:
			OpenDialogue(path);
			break;

		case ProjectAssets::Opener::Sprites:
			OpenSprite(path, preview);
			break;

		case ProjectAssets::Opener::Palette:
			OpenPalette(path);
			break;

		case ProjectAssets::Opener::VisualStudio:
			OpenInVisualStudio(path);
			break;

		case ProjectAssets::Opener::Shell:
			OpenFile(path);
			break;
		};
	};

	void Editor::OpenShader(const std::string& path)
	{
		m_shaders.Open(path);
		m_showShaders = true;
	};

	void Editor::OpenDialogue(const std::string& path)
	{
		m_dialogues.Open(path);
		m_showDialogues = true;
	};

	void Editor::OpenSprite(const std::string& path, bool preview)
	{
		m_sprites.Open(path, preview);
		m_showSprites = true;
	};

	void Editor::OpenModel(const std::string& path)
	{
		const std::string file = std::filesystem::path(path).is_absolute() ? path : PrefabFile(path).string();
		const std::string prefab = std::filesystem::path(file).replace_extension(Prefab::extension).string();

		std::error_code code;

		if (!IsModelFile(file))
			m_models.Open(file);
		else if (std::filesystem::exists(prefab, code) || m_models.IsOpen(prefab))
			m_models.Open(prefab);
		else if (const std::string text = ImportedPrefab(file); !text.empty())
			m_models.Open(prefab, text);

		m_showModels = true;
	};

	void Editor::OpenPalette(const std::string& path)
	{
		m_sprites.OpenPalette(path);
		m_showPalette = true;
	};

	bool Editor::OpenInShell(ImGuiContext*, const char* path)
	{
		std::error_code code;

		if (std::filesystem::is_regular_file(path, code))
			static_cast<Editor*>(ImGui::GetPlatformIO().Platform_OpenInShellUserData)->OpenAsset(path);
		else
			Shell(path, "");

		return true;
	};

	#pragma endregion

	#pragma region Thumbnails

	const Texture* Editor::Thumbnail(const std::string& path)
	{
		if (!ProjectAssets::IsTextureFile(path) || m_noThumbnail.contains(path))
			return nullptr;

		const Texture* texture = Texture::Shared(path);

		if (texture != nullptr && texture->handle != 0)
			return texture;

		m_noThumbnail.insert(path);

		return nullptr;
	};

	#pragma endregion

	#pragma region Moving Assets

	void Editor::DragAsset(const AssetNode& node)
	{
		if (!ImGui::BeginDragDropSource())
			return;

		const std::string relative = ProjectAssets::RelativeTo(node.path, m_projectPath);

		ImGui::SetDragDropPayload(node.folder ? FOLDER_PAYLOAD : ASSET_PAYLOAD, relative.c_str(), relative.size() + 1);
		if (node.folder)
			ImGui::Text("%s %s", EditorIcons::Folder, relative.c_str());
		else if (const Texture* thumbnail = Thumbnail(node.path))
		{
			ImGui::Image(Renderer::Get()->ImGuiTexture(thumbnail->handle), ThumbnailSize(*thumbnail), thumbnailTop, thumbnailBottom);
			ImGui::SameLine();
			ImGui::TextUnformatted(relative.c_str());
		}
		else ImGui::Text("%s %s", IconOf(node.path), relative.c_str());

		ImGui::EndDragDropSource();
	};

	void Editor::AcceptMove(const std::string& folder)
	{
		const ImGuiPayload* dragged = ImGui::GetDragDropPayload();

		if (dragged && dragged->IsDataType(object_payload))
		{
			// The Prefab panel's copy is a prefab already.
			const bool shown = GetPrefabRoot() && GetPrefabRoot()->GetGuid() == *(const Guid*)dragged->Data;

			if (!shown && ImGui::AcceptDragDropPayload(object_payload))
			{
				m_objectToSave = *(const Guid*)dragged->Data;
				m_assetMoveFolder = std::filesystem::path(folder).lexically_normal().string();
			};

			return;
		};

		if (dragged == nullptr || (!dragged->IsDataType(ASSET_PAYLOAD) && !dragged->IsDataType(FOLDER_PAYLOAD)))
			return;

		const std::filesystem::path path = (std::filesystem::path(m_projectPath) / (const char*)dragged->Data).lexically_normal();
		const std::filesystem::path into = std::filesystem::path(folder).lexically_normal();

		// Where it already is, or inside itself, the folder does not light up.
		if (path.parent_path() == into || ProjectAssets::IsUnder(into.string(), path.string()))
			return;

		// The project file names the scripts project by where it is.
		if (m_scripts.HasProject() &&
			ProjectAssets::IsUnder(std::filesystem::path(m_scripts.GetProject()).lexically_normal().string(), path.string()))
			return;

		if (ImGui::AcceptDragDropPayload(dragged->DataType))
		{
			m_assetToMove = path.string();
			m_assetMoveFolder = into.string();
		};
	};

	void Editor::MoveAsset(const std::string& path, const std::string& folder)
	{
		// Found before the move, since a sprite says where its image is.
		const std::string companion = SpriteAsset::Companion(path);

		std::error_code code;

		// Neither moves unless both can, so an image never leaves its sprite behind.
		if (!companion.empty() && std::filesystem::path(path).parent_path().lexically_normal() != std::filesystem::path(folder).lexically_normal())
			for (const std::string& file : { path, companion })
				if (const std::filesystem::path target = std::filesystem::path(folder) / std::filesystem::path(file).filename();
					std::filesystem::exists(target, code))
				{
					std::cerr << "Project: " << target.string() << " is already there, and an image and its sprite move together" << std::endl;
					return;
				};

		std::string error;
		const std::string moved = ProjectAssets::MoveInto(path, folder, &error);

		if (moved.empty())
		{
			std::cerr << "Project: " << error << std::endl;
			return;
		};

		for (auto& [scene, scenePath] : m_scenePaths)
			if (!scenePath.empty())
				scenePath = ProjectAssets::Relocate(scenePath, path, moved);

		if (!m_startupScene.empty())
			m_startupScene = ProjectAssets::Relocate(m_startupScene, path, moved);

		if (!m_selectedModel.empty())
			m_selectedModel = ProjectAssets::Relocate(m_selectedModel, path, moved);

		m_shaders.Relocate(path, moved);
		m_dialogues.Relocate(path, moved);
		m_sprites.Relocate(path, moved);
		m_models.Relocate(path, moved);
		SpriteAsset::RelinkPalettes(m_projectPath, path, moved);

		Refile(path, moved);

		if (!companion.empty())
		{
			const std::string companion_moved = ProjectAssets::MoveInto(companion, folder, &error);

			if (companion_moved.empty())
				std::cerr << "Project: " << error << std::endl;
			else
			{
				m_sprites.Relocate(companion, companion_moved);
				Refile(companion, companion_moved);
			};
		};

		RefreshProjectAssets();

		if (RevealFile(moved))
			return;

		m_selectedAsset = std::filesystem::path(moved).lexically_normal().string();
		m_revealSelectedAsset = true;
	};

	void Editor::Refile(const std::string& from, const std::string& to)
	{
		for (FileTab& tab : m_fileTabs)
		{
			bool refiled = false;

			for (const std::string& file : tab.files)
			{
				const std::filesystem::path relocated = ProjectAssets::Relocate((std::filesystem::path(m_projectPath) / file).lexically_normal().string(), from, to);
				const std::string now = relocated.lexically_relative(m_projectPath).generic_string();

				if (now == file)
					continue;

				const std::string filed = tab.folders.FolderOf(file);

				tab.folders.Move(file, "");
				tab.folders.Move(now, filed);
				refiled = true;

				if (tab.selected == file)
					tab.selected = now;
			};

			if (refiled)
				SaveFileFolders(tab);
		};
	};

	#pragma endregion

	#pragma region Project Files

	std::string Editor::ScriptFoldersFile() const
	{
		return m_projectFile.empty()
			? ""
			: std::filesystem::path(m_projectFile).replace_extension(ScriptFolders::extension).string();
	};

	void Editor::SaveScriptFolders()
	{
		if (const std::string file = ScriptFoldersFile(); !m_scriptFolders.Save(file))
			std::cerr << "Scripts: could not write the folders to " << file << std::endl;
	};

	std::string Editor::InputActionsFile() const
	{
		return m_projectFile.empty()
			? ""
			: std::filesystem::path(m_projectFile).replace_extension(InputActions::extension).string();
	};

	bool Editor::SaveInputActions()
	{
		if (const std::string file = InputActionsFile(); !InputActions::Save(file))
		{
			std::cerr << "Key Bindings: could not write them to " << file << std::endl;
			return false;
		};

		m_keyBindings.Saved();

		return true;
	};

	std::string Editor::FileFoldersFile(const FileTab& tab) const
	{
		return m_projectFile.empty()
			? ""
			: std::filesystem::path(m_projectFile).replace_extension(tab.kind.extension).string();
	};

	void Editor::SaveFileFolders(const FileTab& tab)
	{
		if (const std::string file = FileFoldersFile(tab); !tab.Save(file))
			std::cerr << tab.kind.name << ": could not write the folders to " << file << std::endl;
	};

	#pragma endregion

	#pragma region Scripts Tab

	void Editor::RefreshScripts()
	{
		m_scriptEntries.clear();

		for (const std::string& shader : m_projectShaders)
			m_scriptEntries.try_emplace(
				ProjectAssets::RelativeTo(shader, m_projectPath),
				ScriptEntry{ shader, std::filesystem::path(shader).stem().string(), ScriptEntryKind::Shader, true });

		for (const std::string& dialogue : m_projectDialogues)
			m_scriptEntries.try_emplace(
				ProjectAssets::RelativeTo(dialogue, m_projectPath),
				ScriptEntry{ dialogue, std::filesystem::path(dialogue).stem().string(), ScriptEntryKind::Dialogue, true });

		if (!m_scripts.HasProject())
		{
			m_scriptWatch.Watch("", false);
			return;
		};

		const std::filesystem::path root = std::filesystem::path(m_scripts.GetProject()).parent_path().lexically_normal();

		if (m_scriptWatch.GetFolder() != root.string())
			m_scriptWatch.Watch(root.string(), true);

		// What the watch has seen so far, this scan takes in.
		m_scriptWatch.Changed();

		// For compiled scripts DeclaredScripts does not recognise.
		std::map<std::string, std::string> headers{ };

		std::error_code code;

		for (auto entry = std::filesystem::recursive_directory_iterator(root, code);
			entry != std::filesystem::recursive_directory_iterator();
			entry.increment(code))
		{
			const std::string name = entry->path().filename().string();

			if (entry->is_directory(code))
			{
				if (!name.empty() && name.front() == '.')
					entry.disable_recursion_pending();

				continue;
			};

			if (!ProjectAssets::IsScriptFile(name))
				continue;

			const std::string path = entry->path().string();
			const std::string text = ProjectAssets::ReadText(path);

			for (const std::string& type : ProjectAssets::DeclaredTypes(text))
				headers.try_emplace(type, path);

			for (const ProjectAssets::DeclaredScript& script : ProjectAssets::DeclaredScripts(text))
				m_scriptEntries.try_emplace(
					script.name,
					ScriptEntry{ path, script.name, script.state ? ScriptEntryKind::State : ScriptEntryKind::Script });
		};

		const auto compiled = [&](const std::string& name, ScriptEntryKind kind)
		{
			ScriptEntry& entry = m_scriptEntries[name];

			entry.label = name;
			entry.kind = kind;
			entry.compiled = true;

			if (const auto found = headers.find(name); entry.file.empty() && found != headers.end())
				entry.file = found->second;
		};

		for (const std::string& type : m_scripts.GetTypes())
			compiled(type, ScriptEntryKind::Script);

		for (const std::string& state : m_scripts.GetStates())
			compiled(state, ScriptEntryKind::State);
	};

	void Editor::DrawScripts()
	{
		if (m_revealSelectedScript)
		{
			m_scriptFolderTree.ClearSearch();

			if (const auto entry = m_scriptEntries.find(m_selectedScript); entry != m_scriptEntries.end())
				m_scriptFilter.Show(IconOf(entry->second.kind), true);
		};

		m_scriptFolderTree.DrawSearch();

		ImGui::BeginChild("##scripts", m_scriptFilter.DrawUnder(scriptKinds), ImGuiChildFlags_Borders);

		std::vector<std::string> names{ };

		for (const auto& [name, entry] : m_scriptEntries)
			if (m_scriptFilter.Shows(IconOf(entry.kind)))
				names.push_back(name);

		const VirtualFolders::Node root = ScriptsByLabel(names);

		if (root.children.empty() && root.items.empty() && !m_scriptEntries.empty())
			ImGui::TextDisabled("None of the kinds highlighted below.");
		else if (root.children.empty() && root.items.empty())
			ImGui::TextDisabled(m_scripts.HasProject()
				? "No scripts yet. Right-click to create one."
				: "No scripts project. Right-click to create a script.");

		const FolderTree::Hooks hooks = ScriptFolderHooks();

		m_scriptFolderTree.Draw(m_scriptFolders, root, hooks);

		m_revealSelectedScript = false;

		// Dropped anywhere else in the list, it goes to the top.
		if (ImGui::BeginDragDropTargetCustom(ImGui::GetCurrentWindow()->InnerRect, ImGui::GetID("##scripts_top")))
		{
			FolderTree::Drop(m_scriptFolders, "", hooks);
			ImGui::EndDragDropTarget();
		};

		if (ImGui::BeginPopupContextWindow(
			"##scripts_create",
			ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
		{
			DrawScriptsCreateMenu("");
			ImGui::EndPopup();
		};

		ImGui::EndChild();
	};

	VirtualFolders::Node Editor::ScriptsByLabel(const std::vector<std::string>& names) const
	{
		VirtualFolders::Node root = m_scriptFolders.Build(names);

		// By label, so a shader sits among the scripts by name rather than path.
		const auto by_label = [&](const auto& self, VirtualFolders::Node& node) -> void
		{
			std::stable_sort(
				node.items.begin(),
				node.items.end(),
				[&](const std::string& a, const std::string& b)
				{
					return ScriptFolders::Alphabetical(m_scriptEntries.at(a).label, m_scriptEntries.at(b).label);
				});

			for (VirtualFolders::Node& child : node.children)
				self(self, child);
		};

		by_label(by_label, root);

		return root;
	};

	void Editor::DrawScriptEntry(const std::string& name)
	{
		const ScriptEntry& entry = m_scriptEntries.at(name);
		const char* icon = IconOf(entry.kind);
		const std::string& label = entry.label;

		ImGuiTreeNodeFlags flags =
			ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;

		if (name == m_selectedScript)
			flags |= ImGuiTreeNodeFlags_Selected;

		if (!entry.compiled)
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

		ImGui::TreeNodeEx(("script:" + name).c_str(), flags, "%s %s", icon, label.c_str());

		if (!entry.compiled)
			ImGui::PopStyleColor();

		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
			m_selectedScript = name;

		if (!entry.file.empty() &&
			ImGui::IsItemHovered() &&
			ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			OpenAsset(entry.file);

		ImGui::SetItemTooltip(
			"%s%s",
			entry.file.empty() ? "Not in any of the scripts project's headers" : entry.file.c_str(),
			entry.compiled ? "" : "\nNot compiled yet");

		if (ImGui::BeginDragDropSource())
		{
			ImGui::SetDragDropPayload(script_payload, name.c_str(), name.size() + 1);
			ImGui::Text("%s %s", icon, label.c_str());
			ImGui::EndDragDropSource();
		};

		if (!ImGui::BeginPopupContextItem())
			return;

		m_selectedScript = name;

		const std::string folder = m_scriptFolders.FolderOf(name);

		if (ImGui::MenuItem("Open", nullptr, false, !entry.file.empty()))
			OpenAsset(entry.file);

		if (ImGui::MenuItem("Show in Explorer", nullptr, false, !entry.file.empty()))
			ShowInExplorer(entry.file);

		if (!folder.empty() && ImGui::MenuItem("Move to Top"))
		{
			m_scriptFolders.Move(name, "");
			SaveScriptFolders();
		};

		ImGui::Separator();

		DrawScriptsCreateMenu(folder);

		ImGui::EndPopup();
	};

	FolderTree::Hooks Editor::ScriptFolderHooks()
	{
		FolderTree::Hooks hooks;
		hooks.item_payload = script_payload;
		hooks.folder_payload = script_folder_payload;
		hooks.panel = "Scripts";
		hooks.reveal = [this](const std::string& folder) { return m_revealSelectedScript && VirtualFolders::IsAtOrUnder(m_scriptFolders.FolderOf(m_selectedScript), folder); };
		hooks.create = [this](const std::string& folder) { DrawScriptsCreateMenu(folder); };
		hooks.item = [this](const std::string& script) { DrawScriptEntry(script); };
		hooks.name = [this](const std::string& script) { return m_scriptEntries.at(script).label; };
		hooks.changed = [this]() { SaveScriptFolders(); };

		return hooks;
	};

	#pragma endregion

	#pragma region Revealing Files

	FileTab* Editor::FileTabOf(const std::string& path)
	{
		const FileTabKind* kind = TabHolding(path);

		for (FileTab& tab : m_fileTabs)
			if (&tab.kind == kind)
				return &tab;

		return nullptr;
	};

	bool Editor::RevealFile(const std::string& path)
	{
		FileTab* tab = FileTabOf(path);

		if (tab == nullptr)
			return false;

		tab->selected = std::filesystem::path(path).lexically_normal().lexically_relative(m_projectPath).generic_string();
		tab->reveal = true;
		ShowProjectTab(tab->kind.title);

		return true;
	};

	void Editor::PingAsset(const std::string& path)
	{
		m_selectedAsset = std::filesystem::path(path).lexically_normal().string();
		m_revealSelectedAsset = true;
		ShowProjectTab(ProjectTabs::Scenes);
		m_pingedAsset = m_selectedAsset;
		m_pingedSince = ImGui::GetTime();
	};

	#pragma endregion

	#pragma region Field Pickers

	void Editor::DrawPickerButton(const std::string& shown, const char* icon, const std::string& tip)
	{
		const float width = ImGui::CalcItemWidth();

		bool cut = false;
		const std::string label = FitPickerLabel(
			icon,
			shown,
			width - ImGui::GetStyle().FramePadding.x * 2.0f,
			cut);

		if (ImGui::Button((label + "###picker").c_str(), ImVec2(width, 0.0f)))
		{
			m_fieldPicker.ClearSearch();
			ImGui::OpenPopup(PICKER);
		};

		PickerTip(tip, shown, cut);
	};

	bool Editor::BeginPicker(bool* appearing)
	{
		const ImVec2 under(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y);
		const float width = (std::max)(ImGui::GetItemRectSize().x, PICKER_MIN_WIDTH);

		ImGui::SetNextWindowPos(under, ImGuiCond_Appearing);

		if (!ImGui::BeginPopup(PICKER))
			return false;

		const bool opened = ImGui::IsWindowAppearing();

		if (appearing)
			*appearing = opened;

		if (opened)
			ImGui::SetKeyboardFocusHere();

		m_fieldPicker.DrawSearch();

		ImGui::BeginChild("##choices", ImVec2(width, ImGui::GetFrameHeightWithSpacing() * PICKER_ROWS), ImGuiChildFlags_Borders);

		return true;
	};

	void Editor::EndPicker()
	{
		ImGui::EndChild();
		ImGui::EndPopup();
	};

	bool Editor::PickerRow(const std::string& id, const std::string& label, bool selected, const char* icon, const std::string& tip)
	{
		ImGuiTreeNodeFlags flags =
			ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;

		if (selected)
			flags |= ImGuiTreeNodeFlags_Selected;

		bool cut = false;
		const std::string shown = FitPickerLabel(
			icon,
			label,
			ImGui::GetContentRegionAvail().x - ImGui::GetTreeNodeToLabelSpacing(),
			cut);

		ImGui::TreeNodeEx(("pick:" + id).c_str(), flags, "%s", shown.c_str());

		PickerTip(tip, label, cut);

		if (!ImGui::IsItemClicked(ImGuiMouseButton_Left))
			return false;

		ImGui::CloseCurrentPopup();

		return true;
	};

	bool Editor::DrawAssetField(const SerializedField& field)
	{
		std::string& path = *(std::string*)field.data;

		const bool folder = field.extensions.empty();
		const auto icon_of = [&](const std::string& file)
		{
			if (file.empty())
				return (const char*)nullptr;

			return folder
				? EditorIcons::Folder
				: IconOf(file);
		};

		const std::filesystem::path named(path);

		DrawPickerButton(
			path.empty() ? std::string("None") : (folder ? named.filename() : named.stem()).string(),
			icon_of(path),
			path);

		// The files the Scripts tab lists rather than one of the FileTabs.
		const std::string_view extension = folder ? std::string_view() : field.extensions.front();
		const std::optional<ScriptEntryKind> listed = extension == ProjectAssets::shaderExtension
			? std::optional(ScriptEntryKind::Shader)
			: extension == ProjectAssets::dialogueExtension
				? std::optional(ScriptEntryKind::Dialogue)
				: std::nullopt;

		bool changed = false;

		if (ImGui::BeginDragDropTarget())
		{
			const ImGuiPayload* dragged = ImGui::GetDragDropPayload();
			const char* payload = folder ? FOLDER_PAYLOAD : ASSET_PAYLOAD;

			if (dragged != nullptr &&
				dragged->IsDataType(payload) &&
				(folder || field.AcceptsFile((const char*)dragged->Data)) &&
				ImGui::AcceptDragDropPayload(payload))
			{
				path = (const char*)dragged->Data;
				changed = true;
			};

			ImGui::EndDragDropTarget();
		};

		if (listed)
			changed |= AcceptScriptsDrop(path, *listed);

		bool appearing = false;

		if (!BeginPicker(&appearing))
			return changed;

		const auto row = [&](const std::string& file, const std::string& label)
		{
			const bool picked = PickerRow(file, label, file == path, icon_of(file), file);

			if (picked && file != path)
			{
				path = file;
				changed = true;
			};
		};

		row("", "None");

		if (listed)
		{
			std::vector<std::string> names{ };

			for (const auto& [name, entry] : m_scriptEntries)
				if (entry.kind == *listed)
					names.push_back(name);

			FolderTree::Hooks hooks = ScriptFolderHooks();
			hooks.reveal = [&](const std::string& folder) { return appearing && VirtualFolders::IsAtOrUnder(m_scriptFolders.FolderOf(path), folder); };
			hooks.arrange = false;
			hooks.create = nullptr;
			hooks.item = [&](const std::string& name) { row(name, m_scriptEntries.at(name).label); };

			m_fieldPicker.Draw(m_scriptFolders, ScriptsByLabel(names), hooks);
		}
		else if (FileTab* tab = folder ? nullptr : FileTabOf("file" + field.extensions.front()))
		{
			std::vector<std::string> files{ };

			for (const std::string& file : tab->files)
				if (field.AcceptsFile(file))
					files.push_back(file);

			FolderTree::Hooks hooks = FileFolderHooks(*tab);
			hooks.reveal = [&](const std::string& folder) { return appearing && VirtualFolders::IsAtOrUnder(tab->folders.FolderOf(path), folder); };
			hooks.arrange = false;
			hooks.create = nullptr;
			hooks.item = [&](const std::string& file) { row(file, std::filesystem::path(file).filename().string()); };

			m_fieldPicker.Draw(tab->folders, tab->folders.Build(files), hooks);
		}
		else
		{
			std::vector<std::string> files{ };

			if (folder)
				files = m_projectFolders;
			else
				for (const std::string& file : m_projectFiles)
					if (field.AcceptsFile(file))
						files.push_back(file);

			VirtualFolders flat{ };

			FolderTree::Hooks hooks{ };
			hooks.arrange = false;
			hooks.item = [&](const std::string& file) { row(file, file); };

			m_fieldPicker.Draw(flat, flat.Build(files), hooks);
		};

		EndPicker();

		return changed;
	};

	#pragma endregion

	#pragma region File Tabs

	void Editor::DrawFileTab(FileTab& tab)
	{
		if (tab.reveal)
		{
			tab.tree.ClearSearch();
			tab.filter.Show(tab.kind.filter.icon, true);
		};

		tab.tree.DrawSearch();

		ImGui::BeginChild("##files", tab.filter.DrawUnder({ &tab.kind.filter, 1 }), ImGuiChildFlags_Borders);

		// Every file here is of the one kind.
		static const std::vector<std::string> none{ };
		const VirtualFolders::Node root = tab.folders.Build(tab.filter.Shows(tab.kind.filter.icon) ? tab.files : none);

		if (root.children.empty() && root.items.empty() && !tab.files.empty())
			ImGui::TextDisabled("None of the kinds highlighted below.");
		else if (root.children.empty() && root.items.empty())
			ImGui::TextDisabled(
				std::ranges::any_of(assetKinds, [&tab](const AssetKindInfo& info) { return TabMaking(info.kind) == &tab.kind; })
					? "No %s yet. Right-click to create one."
					: "No %s yet.",
				tab.kind.files);

		const FolderTree::Hooks hooks = FileFolderHooks(tab);

		tab.tree.Draw(tab.folders, root, hooks);

		tab.reveal = false;

		// A file from outside goes in the project folder.
		TakeImportFolder(m_projectPath, ImGui::IsWindowHovered());

		// Dropped anywhere else in the list, it goes to the top.
		if (ImGui::BeginDragDropTargetCustom(ImGui::GetCurrentWindow()->InnerRect, ImGui::GetID("##files_top")))
		{
			FolderTree::Drop(tab.folders, "", hooks);
			ImGui::EndDragDropTarget();
		};

		if (ImGui::BeginPopupContextWindow(
			"##files_create",
			ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
		{
			DrawFileCreateMenu(tab, "");
			ImGui::EndPopup();
		};

		ImGui::EndChild();
	};

	void Editor::DrawFileEntry(FileTab& tab, const std::string& file)
	{
		const AssetNode node{
			(std::filesystem::path(m_projectPath) / file).lexically_normal().string(),
			std::filesystem::path(file).filename().string() };

		ImGuiTreeNodeFlags flags =
			ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;

		if (file == tab.selected)
			flags |= ImGuiTreeNodeFlags_Selected;

		const Texture* thumbnail = Thumbnail(node.path);
		const std::string icon = thumbnail != nullptr ? ThumbnailSpaces() : IconOf(node.path);

		ImGui::TreeNodeEx(("file:" + file).c_str(), flags, "%s %s", icon.c_str(), node.name.c_str());

		if (thumbnail != nullptr)
			DrawThumbnail(*thumbnail);

		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
		{
			tab.selected = file;

			// A model shows in the Inspector, where it can be imported.
			if (IsModelFile(node.path))
				SelectModel(node.path);
			else m_selectedModel.clear();
		};

		// Single click opens on release so a drag to a field leaves the panel shut.
		const ImGuiIO& io = ImGui::GetIO();
		const float drag_threshold = io.MouseDragThreshold * io.MouseDragThreshold;

		const bool released =
			file == tab.selected &&
			ImGui::IsItemHovered() &&
			ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
			io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] < drag_threshold;

		const bool double_clicked = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

		if (double_clicked)
			OpenAsset(node.path);
		else if (released && ProjectAssets::IsTwoDFile(node.path))
			OpenAsset(node.path, true);

		ImGui::SetItemTooltip("%s", file.c_str());

		DragAsset(node);

		if (!ImGui::BeginPopupContextItem())
			return;

		tab.selected = file;

		const std::string folder = tab.folders.FolderOf(file);

		if (ImGui::MenuItem("Open"))
			OpenAsset(node.path);

		if (ImGui::MenuItem("Show in Explorer"))
			ShowInExplorer(node.path);

		if (!folder.empty() && ImGui::MenuItem("Move to Top"))
		{
			tab.folders.Move(file, "");
			SaveFileFolders(tab);
		};

		ImGui::Separator();

		DrawFileCreateMenu(tab, folder);

		ImGui::EndPopup();
	};

	void Editor::DrawFileCreateMenu(FileTab& tab, const std::string& folder)
	{
		if (!ImGui::BeginMenu("Create"))
			return;

		// Kept beside the project file, so not without one.
		if (ImGui::MenuItem((std::string(EditorIcons::Folder) + " Folder").c_str(), nullptr, false, !m_projectFile.empty()))
		{
			AddNewFolder(tab.folders, tab.tree, folder);
			SaveFileFolders(tab);
		};

		for (const AssetKindInfo& info : assetKinds)
			if (TabMaking(info.kind) == &tab.kind && ImGui::MenuItem((std::string(info.icon) + " " + info.label).c_str()))
			{
				AskForAsset(info.kind, m_projectPath);
				tab.newFolder = folder;
			};

		ImGui::EndMenu();
	};

	FolderTree::Hooks Editor::FileFolderHooks(FileTab& tab)
	{
		FolderTree::Hooks hooks;
		hooks.item_payload = ASSET_PAYLOAD;
		hooks.folder_payload = tab.kind.folderPayload;
		hooks.takes = [&tab](const std::string& file) { return std::ranges::find(tab.files, file) != tab.files.end(); };
		hooks.panel = tab.kind.name;
		hooks.reveal = [&tab](const std::string& folder) { return tab.reveal && VirtualFolders::IsAtOrUnder(tab.folders.FolderOf(tab.selected), folder); };
		hooks.create = [this, &tab](const std::string& folder) { DrawFileCreateMenu(tab, folder); };
		hooks.item = [this, &tab](const std::string& file) { DrawFileEntry(tab, file); };
		hooks.name = [](const std::string& file) { return std::filesystem::path(file).filename().string(); };
		hooks.changed = [this, &tab]() { SaveFileFolders(tab); };

		return hooks;
	};

	#pragma endregion
};
