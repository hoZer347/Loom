#include "Editor.h"

#include "EditorFileDialogs.h"
#include "EditorGui.h"
#include "EditorIcons.h"
#include "ModelImporter.h"
#include "Prefab.h"
#include "ProjectAssets.h"
#include "ProjectTemplate.h"

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
		// Fields take files by ASSET_PAYLOAD, so a folder goes by its own.
		constexpr const char* FOLDER_PAYLOAD = "LoomFolder";

		constexpr float namePromptWidth = 320.0f;
		constexpr float promptButtonWidth = 120.0f;

		// How far NewShader, NewShader1, ... counts before leaving the name to
		// the user to sort out.
		constexpr int maxDefaultNameSuffix = 1000;

		struct AssetKindInfo
		{
			AssetKind kind;
			const char* icon;
			const char* label;
			const char* defaultName;

			// Scripts only: the header's template, and where its type turns up
			// once it is compiled.
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
			{ AssetKind::Shader, EditorIcons::Shader, "Shader", "NewShader" },
			{ AssetKind::Texture, EditorIcons::Texture, "Texture", "NewTexture" },
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

		// New Folder in parent, or the first of New Folder1, ... nothing there is
		// using, its name then typed in place.
		void AddNewFolder(VirtualFolders& folders, FolderTree& tree, const std::string& parent)
		{
			constexpr const char* newFolder = "New Folder";

			std::string path = folders.AddFolder(parent, newFolder);

			for (int suffix = 1; path.empty() && suffix < maxDefaultNameSuffix; suffix++)
				path = folders.AddFolder(parent, newFolder + std::to_string(suffix));

			tree.Rename(path);
		};

		// What the 2D panel's Create menu makes, rather than the Project panel's.
		bool IsTwoD(AssetKind kind)
		{
			return kind == AssetKind::Texture;
		};

		// Textures are uploaded bottom row first, so V is flipped.
		const ImVec2 thumbnailTop(0.0f, 1.0f);
		const ImVec2 thumbnailBottom(1.0f, 0.0f);

		const char* IconOf(const std::string& path)
		{
			return IsModelFile(path) ? EditorIcons::Model : EditorIcons::Of(path);
		};

		// As wide as the icon it stands in for, a line high and no wider,
		// keeping the picture's shape.
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

		// Folders first, then files, each alphabetically. Only files the game
		// uses are assets, and dot-prefixed folders (.vs) and the build output
		// are left out. So are the scripts, which are the Scripts panel's, and
		// the textures, which go in textures for the 2D panel, along with any
		// folder holding nothing else. Whether there were either under folder.
		bool Scan(
			const std::filesystem::path& folder,
			const std::filesystem::path& skip,
			AssetNode& node,
			std::vector<std::string>& scenes,
			std::vector<std::string>& textures)
		{
			std::error_code code;
			bool elsewhere = false;

			for (const auto& entry : std::filesystem::directory_iterator(folder, code))
			{
				const std::filesystem::path path = entry.path().lexically_normal();
				const std::string name = path.filename().string();

				if (name.empty() || name.front() == '.')
					continue;

				AssetNode child{ path.string(), name, entry.is_directory(code) };

				if (!child.folder && ProjectAssets::IsScriptFile(name))
					elsewhere = true;

				if (!child.folder && ProjectAssets::IsTextureFile(name))
				{
					textures.push_back(child.path);
					elsewhere = true;
					continue;
				};

				if (!child.folder && !ProjectAssets::IsGameFile(name) && !IsModelFile(name))
					continue;

				if (child.folder)
				{
					if (path == skip)
						continue;

					const bool held_elsewhere = Scan(path, skip, child, scenes, textures);

					elsewhere = elsewhere || held_elsewhere;

					if (child.children.empty() && held_elsewhere)
						continue;
				}
				else if (path.extension() == SceneSerializer::extension)
					scenes.push_back(child.path);

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

			return elsewhere;
		};

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

		// With whatever opens text files, for a kind of file nothing claims (a
		// .shader among them).
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

		using Microsoft::WRL::ComPtr;

		// Calls an automation member by name, with at most one argument.
		HRESULT Call(
			IDispatch* object,
			const wchar_t* member,
			WORD kind,
			VARIANT* result,
			VARIANT* argument = nullptr)
		{
			DISPID id = 0;
			LPOLESTR name = const_cast<LPOLESTR>(member);

			const HRESULT found = object->GetIDsOfNames(IID_NULL, &name, 1, LOCALE_USER_DEFAULT, &id);

			if (FAILED(found))
				return found;

			DISPPARAMS parameters{ argument, nullptr, argument != nullptr ? 1u : 0u, 0 };

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

		// What a Visual Studio in the middle of a build or a modal dialog answers.
		bool IsBusy(HRESULT result)
		{
			return result == RPC_E_CALL_REJECTED || result == RPC_E_SERVERCALL_RETRYLATER;
		};

		// Each running Visual Studio registers its automation object as
		// "!VisualStudio.DTE.<version>:<process id>". Sets busy when one would
		// not say which solution it has.
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

			// A Visual Studio would not answer, and may be the one with the
			// solution.
			Busy,

			Failed,
		};

		Handover OpenThroughAutomation(const std::string& solution, const std::string& file)
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
				? Call(operations.Get(), L"OpenFile", DISPATCH_METHOD, &opened, &path)
				: E_NOINTERFACE;

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

			// The editor has the foreground, having just been clicked, and Windows
			// only lets another process take it when the one holding it says so.
			AllowSetForegroundWindow(process);

			if (window)
				Call(window.Get(), L"Activate", DISPATCH_METHOD, nullptr);

			return Handover::Opened;
		};

		// False when no Visual Studio has the solution open. The editor's thread
		// does not keep COM up, so this brings it up for the length of the call.
		bool OpenInRunningVisualStudio(const std::string& solution, const std::string& file)
		{
			const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

			const Handover handover = OpenThroughAutomation(solution, file);

			// A busy one may be the one with the solution, and starting another
			// would open it twice.
			if (handover == Handover::Busy)
				std::cerr << "Visual Studio is busy; try again once it has finished." << std::endl;

			if (SUCCEEDED(com))
				CoUninitialize();

			return handover != Handover::NotRunning;
		};

		// Long enough for a cold start of Visual Studio on a large solution.
		constexpr std::chrono::seconds visualStudioStartTimeout{ 180 };
		constexpr std::chrono::milliseconds visualStudioPollInterval{ 500 };

		// A Visual Studio just started answers nothing until it has loaded the
		// solution, so the file is handed to it from a thread of its own once it
		// says it has. Its command line could open the file too, but File.OpenFile
		// through /Command fails with dialogs of its own.
		void OpenOnceLoaded(const std::string& solution, const std::string& file)
		{
			std::thread(
				[solution, file]()
				{
					const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

					const auto deadline = std::chrono::steady_clock::now() + visualStudioStartTimeout;

					Handover handover = Handover::NotRunning;

					while (std::chrono::steady_clock::now() < deadline)
					{
						handover = OpenThroughAutomation(solution, file);

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
	};

	void Editor::RefreshProjectAssets()
	{
		if (m_projectWatch.GetFolder() != m_projectPath)
			m_projectWatch.Watch(m_projectPath, false);

		RefreshScripts();

		m_projectScenes.clear();
		m_twoDFiles.clear();
		m_noThumbnail.clear();
		m_assets = AssetNode{ m_projectPath, "", true };

		if (m_projectPath.empty())
			return;

		const std::filesystem::path build = m_scripts.HasProject()
			? std::filesystem::path(m_scripts.GetLibrary()).parent_path().lexically_normal()
			: std::filesystem::path();

		std::vector<std::string> textures{ };

		Scan(m_projectPath, build, m_assets, m_projectScenes, textures);

		std::sort(m_projectScenes.begin(), m_projectScenes.end());

		for (const std::string& texture : textures)
			m_twoDFiles.push_back(std::filesystem::path(texture).lexically_relative(m_projectPath).generic_string());
	};

	void Editor::DrawProject()
	{
		if (ImGui::Begin(EditorPanels::Project, &m_showProject))
		{
			if (m_projectPath.empty())
			{
				ImGui::TextDisabled("No scene folder chosen.");

				if (ImGui::Button("Choose Folder..."))
					m_dialogs->folder.Open();
			}
			else
			{
				if (m_projectWatch.Changed())
					RefreshProjectAssets();

				const char* const change = "Change...";

				const float button_width =
					ImGui::CalcTextSize(change).x + ImGui::GetStyle().FramePadding.x * 2.0f;
				const float button_x = ImGui::GetContentRegionMax().x - button_width;

				ImGui::AlignTextToFramePadding();
				// Zero or less would mean no wrapping at all on a panel narrower than
				// the button.
				ImGui::PushTextWrapPos((std::max)(
					button_x - ImGui::GetStyle().ItemSpacing.x,
					ImGui::GetCursorPosX() + ImGui::GetFontSize()));
				ImGui::TextWrapped("%s", m_projectPath.c_str());
				ImGui::PopTextWrapPos();

				ImGui::SameLine(button_x);

				if (ImGui::Button(change))
					m_dialogs->folder.Open();

				// Something new is selected to be shown, which the search may hide.
				if (m_revealSelectedAsset)
					m_assetSearch.Clear();

				m_assetSearch.Draw();

				ImGui::BeginChild("##assets", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);

				if (m_assets.children.empty())
					ImGui::TextDisabled("Nothing here yet. Right-click to create something.");

				m_assetSearch.BeginResults();

				for (const AssetNode& node : m_assets.children)
					if (ShowsAsset(node))
						DrawAssetNode(node, 0, false);

				m_assetSearch.EndResults();

				// Dropped anywhere else in the list, it goes in the project folder.
				TakeImportFolder(m_projectPath, ImGui::IsWindowHovered());

				// The empty space under the rows, so a row that turns a drop away
				// does not pass it on to the project folder.
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
		};

		ImGui::End();
	};

	void Editor::DrawAssetNode(const AssetNode& node, int depth, bool found)
	{
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;

		if (node.path == m_selectedAsset)
			flags |= ImGuiTreeNodeFlags_Selected;

		if (node.folder)
		{
			flags |= ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;

			// The top level is only a handful of folders, and the scenes are in
			// one of them.
			if (depth == 0)
				flags |= ImGuiTreeNodeFlags_DefaultOpen;

			m_assetSearch.OpenFound();

			if (m_revealSelectedAsset && ProjectAssets::IsUnder(m_selectedAsset, node.path))
				ImGui::SetNextItemOpen(true);
		}
		else
			flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

		const bool open = ImGui::TreeNodeEx(node.path.c_str(), flags, "%s %s", node.folder ? EditorIcons::Folder : IconOf(node.path), node.name.c_str());

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
		return m_assetSearch.Matches(node.name) ||
			std::ranges::any_of(node.children, [this](const AssetNode& child) { return ShowsAsset(child); });
	};

	void Editor::DrawAssetContextMenu(const AssetNode& node)
	{
		if (!ImGui::BeginPopupContextItem())
			return;

		m_selectedAsset = node.path;

		if (!node.folder && ImGui::MenuItem("Open"))
			m_assetToOpen = node.path;

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
			if (!IsScript(info.kind) && !IsTwoD(info.kind) && ImGui::MenuItem((std::string(info.icon) + " " + info.label).c_str()))
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
			if (IsScript(info.kind) && ImGui::MenuItem((std::string(info.icon) + " " + info.label).c_str()))
			{
				AskForAsset(info.kind, m_projectPath);
				m_newScriptFolder = folder;
			};

		ImGui::EndMenu();
	};

	void Editor::AskForAsset(AssetKind kind, const std::string& folder)
	{
		const AssetKindInfo& info = InfoOf(kind);

		m_newAssetKind = kind;
		m_newAssetFolder = folder;
		m_newScriptFolder.clear();
		m_newTwoDFolder.clear();
		m_askForAsset = true;
		m_focusAssetName = true;

		// NewShader, NewShader1, ...: the first name nothing is using.
		snprintf(m_newAssetName, sizeof(m_newAssetName), "%s", info.defaultName);

		m_newAssetProblem = NewAssetProblem();

		for (int suffix = 1; !m_newAssetProblem.empty() && suffix < maxDefaultNameSuffix; suffix++)
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
			// A GameObject of its own, so the file has every field one writes.
			// Deleted once the queue has registered it, as a closed scene is.
			Scene* blank = new Scene(name);
			Scene::Unlist(blank);
			blank->GetRoot().SetName(name);

			Prefabs::ObjectNode prefab;
			const bool parsed = Prefabs::Parse(SceneSerializer::Serialize(blank->GetRoot()), prefab);

			Engine::QueueTask([blank]() { delete blank; });

			if (parsed &&
				WritePrefab(
					std::filesystem::path(NewAssetPath()).lexically_normal().lexically_relative(m_projectPath).generic_string(),
					prefab))
				path = NewAssetPath();
			else error = "the prefab could not be written";
			break;
		};

		case AssetKind::Script:
		case AssetKind::State:
		case AssetKind::StateMachine:
		{
			if (!m_scripts.HasProject())
			{
				if (ProjectTemplate::AddScripts(m_projectPath, m_projectName, &error).empty())
					break;

				// The project file names a scripts project now, which may be one
				// already under the folder rather than a new one, so it is loaded
				// before the script's place is worked out from it.
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

		case AssetKind::Texture:
			path = ProjectAssets::CreateTexture(folder, name, &error);
			break;
		};

		if (path.empty())
		{
			std::cerr << "Could not create " << name << ": " << error << std::endl;
			return "";
		};

		std::cout << "Created " << path << std::endl;

		if (const char* appearsIn = InfoOf(m_newAssetKind).appearsIn)
			std::cout << name << " shows up in " << appearsIn << " after the next compile." << std::endl;

		m_askForAsset = false;

		if (IsScript(m_newAssetKind))
		{
			m_scriptFolders.Move(name, m_newScriptFolder);
			SaveScriptFolders();

			m_selectedScript = name;
			m_revealSelectedScript = true;
			m_focusScripts = true;
		}
		else if (IsTwoD(m_newAssetKind))
		{
			m_twoDFolders.Move(std::filesystem::path(path).lexically_relative(m_projectPath).generic_string(), m_newTwoDFolder);
			SaveTwoDFolders();

			RevealTwoD(path);
		}
		else
		{
			m_selectedAsset = std::filesystem::path(path).lexically_normal().string();
			m_revealSelectedAsset = true;
		};

		RefreshProjectAssets();

		return path;
	};

	void Editor::OpenInVisualStudio(const std::string& path)
	{
		// A hand-written project need not have a solution beside it, even though
		// every project the editor writes does.
		const std::string solution = m_scripts.HasProject()
			? std::filesystem::path(m_scripts.GetProject()).replace_extension(".sln").string()
			: "";

		std::error_code code;
		const bool has_solution = !solution.empty() && std::filesystem::exists(solution, code);

		if (has_solution && OpenInRunningVisualStudio(solution, path))
			return;

		const std::string& devenv = ScriptLibrary::FindVisualStudio();

		if (devenv.empty())
			Shell((has_solution ? solution : path).c_str(), "");
		else if (has_solution)
		{
			Shell(devenv.c_str(), '"' + solution + '"');
			OpenOnceLoaded(solution, path);
		}
		else
			Shell(devenv.c_str(), "/Edit \"" + path + '"');
	};

	void Editor::OpenAsset(const std::string& path)
	{
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
				? std::filesystem::path(path).lexically_normal().lexically_relative(m_projectPath).generic_string()
				: std::filesystem::path(path).generic_string());
			break;

		case ProjectAssets::Opener::Shaders:
			OpenShader(path);
			break;

		case ProjectAssets::Opener::Sprites:
			OpenSprite(path);
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

	void Editor::OpenSprite(const std::string& path)
	{
		m_sprites.Open(path);
		m_showSprites = true;
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

	void Editor::DragAsset(const AssetNode& node)
	{
		if (!ImGui::BeginDragDropSource())
			return;

		// Relative, the way components name the files they use.
		const std::string relative = std::filesystem::path(node.path).lexically_relative(m_projectPath).generic_string();

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
		std::string error;
		const std::string moved = ProjectAssets::MoveInto(path, folder, &error);

		if (moved.empty())
		{
			std::cerr << "Project: " << error << std::endl;
			return;
		};

		std::cout << "Moved " << path << " to " << moved << std::endl;

		for (auto& [scene, scenePath] : m_scenePaths)
			if (!scenePath.empty())
				scenePath = ProjectAssets::Relocate(scenePath, path, moved);

		if (!m_startupScene.empty())
			m_startupScene = ProjectAssets::Relocate(m_startupScene, path, moved);

		if (!m_selectedModel.empty())
			m_selectedModel = ProjectAssets::Relocate(m_selectedModel, path, moved);

		m_shaders.Relocate(path, moved);
		m_sprites.Relocate(path, moved);

		// The 2D panel files its textures by their paths, so they stay filed
		// where they were.
		bool refiled = false;

		for (const std::string& file : m_twoDFiles)
		{
			const std::filesystem::path relocated = ProjectAssets::Relocate((std::filesystem::path(m_projectPath) / file).lexically_normal().string(), path, moved);
			const std::string to = relocated.lexically_relative(m_projectPath).generic_string();

			if (to == file)
				continue;

			const std::string filed = m_twoDFolders.FolderOf(file);

			m_twoDFolders.Move(file, "");
			m_twoDFolders.Move(to, filed);
			refiled = true;

			if (m_selectedTwoD == file)
				m_selectedTwoD = to;
		};

		if (refiled)
			SaveTwoDFolders();

		RefreshProjectAssets();

		if (ProjectAssets::IsTextureFile(moved))
			return RevealTwoD(moved);

		m_selectedAsset = std::filesystem::path(moved).lexically_normal().string();
		m_revealSelectedAsset = true;
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

			ImGui::SetNextItemWidth(namePromptWidth);

			const bool entered = ImGui::InputText(
				"Name",
				m_newAssetName,
				sizeof(m_newAssetName),
				ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);

			// Checked on edit rather than every frame: for a script it walks the
			// scripts project.
			if (ImGui::IsItemEdited())
				m_newAssetProblem = NewAssetProblem();

			const bool usable = m_newAssetProblem.empty();

			ImGui::TextDisabled("%s", usable ? NewAssetPath().c_str() : m_newAssetProblem.c_str());

			ImGui::BeginDisabled(!usable);

			// Opened from here rather than in CreateAsset, so --create-script
			// leaves Visual Studio closed.
			if (ImGui::Button("Create", ImVec2(promptButtonWidth, 0.0f)) || (entered && usable))
				if (const std::string path = CreateAsset(); !path.empty() && IsScript(m_newAssetKind))
					OpenAsset(path);

			ImGui::EndDisabled();

			ImGui::SameLine();

			const bool escaped =
				ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
				ImGui::IsKeyPressed(ImGuiKey_Escape);

			if (ImGui::Button("Cancel", ImVec2(promptButtonWidth, 0.0f)) || escaped)
				m_askForAsset = false;
		};

		ImGui::End();
	};

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

	void Editor::SaveInputActions()
	{
		if (const std::string file = InputActionsFile(); !InputActions::Save(file))
			std::cerr << "Key Bindings: could not write them to " << file << std::endl;
	};

	void Editor::RefreshScripts()
	{
		m_scriptEntries.clear();

		if (!m_scripts.HasProject())
		{
			m_scriptWatch.Watch("", false);
			return;
		};

		const std::filesystem::path root = std::filesystem::path(m_scripts.GetProject()).parent_path().lexically_normal();

		if (m_scriptWatch.GetFolder() != root.string())
			m_scriptWatch.Watch(root.string(), true);

		// Every type the headers declare, for the compiled scripts that do not
		// look like one to DeclaredScripts.
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
				m_scriptEntries.try_emplace(script.name, ScriptEntry{ path, script.state });
		};

		const auto compiled = [&](const std::string& name, bool state)
		{
			ScriptEntry& entry = m_scriptEntries[name];

			entry.state = state;
			entry.compiled = true;

			if (const auto found = headers.find(name); entry.header.empty() && found != headers.end())
				entry.header = found->second;
		};

		for (const std::string& type : m_scripts.GetTypes())
			compiled(type, false);

		for (const std::string& state : m_scripts.GetStates())
			compiled(state, true);
	};

	void Editor::DrawScripts()
	{
		if (m_focusScripts)
		{
			ImGui::SetNextWindowFocus();
			m_focusScripts = false;
		};

		if (ImGui::Begin(EditorPanels::Scripts, &m_showScripts))
		{
			if (m_scriptWatch.Changed())
				RefreshScripts();

			if (m_revealSelectedScript)
				m_scriptFolderTree.ClearSearch();

			m_scriptFolderTree.DrawSearch();

			ImGui::BeginChild("##scripts", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);

			std::vector<std::string> names{ };

			for (const auto& [name, entry] : m_scriptEntries)
				names.push_back(name);

			const ScriptFolders::Node root = m_scriptFolders.Build(names);

			if (m_projectPath.empty())
				ImGui::TextDisabled("No project open.");
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

			if (!m_projectPath.empty() &&
				ImGui::BeginPopupContextWindow(
					"##scripts_create",
					ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
			{
				DrawScriptsCreateMenu("");
				ImGui::EndPopup();
			};

			ImGui::EndChild();
		};

		ImGui::End();
	};

	void Editor::DrawScriptEntry(const std::string& name)
	{
		const ScriptEntry& entry = m_scriptEntries.at(name);
		const char* icon = entry.state ? EditorIcons::State : EditorIcons::Script;

		ImGuiTreeNodeFlags flags =
			ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;

		if (name == m_selectedScript)
			flags |= ImGuiTreeNodeFlags_Selected;

		if (!entry.compiled)
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

		ImGui::TreeNodeEx(("script:" + name).c_str(), flags, "%s %s", icon, name.c_str());

		if (!entry.compiled)
			ImGui::PopStyleColor();

		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
			m_selectedScript = name;

		if (!entry.header.empty() &&
			ImGui::IsItemHovered() &&
			ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			OpenAsset(entry.header);

		ImGui::SetItemTooltip(
			"%s%s",
			entry.header.empty() ? "Not in any of the scripts project's headers" : entry.header.c_str(),
			entry.compiled ? "" : "\nNot compiled yet");

		if (ImGui::BeginDragDropSource())
		{
			ImGui::SetDragDropPayload(script_payload, name.c_str(), name.size() + 1);
			ImGui::Text("%s %s", icon, name.c_str());
			ImGui::EndDragDropSource();
		};

		if (!ImGui::BeginPopupContextItem())
			return;

		m_selectedScript = name;

		const std::string folder = m_scriptFolders.FolderOf(name);

		if (ImGui::MenuItem("Open", nullptr, false, !entry.header.empty()))
			OpenAsset(entry.header);

		if (ImGui::MenuItem("Show in Explorer", nullptr, false, !entry.header.empty()))
			ShowInExplorer(entry.header);

		if (!folder.empty() && ImGui::MenuItem("Move to Top"))
		{
			m_scriptFolders.Move(name, "");
			SaveScriptFolders();
		};

		ImGui::Separator();

		DrawScriptsCreateMenu(folder);

		ImGui::EndPopup();
	};

	std::string Editor::TwoDFoldersFile() const
	{
		return m_projectFile.empty()
			? ""
			: std::filesystem::path(m_projectFile).replace_extension(TwoDFolders::extension).string();
	};

	void Editor::SaveTwoDFolders()
	{
		if (const std::string file = TwoDFoldersFile(); !m_twoDFolders.Save(file))
			std::cerr << "2D: could not write the folders to " << file << std::endl;
	};

	void Editor::RevealTwoD(const std::string& path)
	{
		m_selectedTwoD = std::filesystem::path(path).lexically_normal().lexically_relative(m_projectPath).generic_string();
		m_revealSelectedTwoD = true;
		m_showTwoD = true;
		m_focusTwoD = true;
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
		hooks.changed = [this]() { SaveScriptFolders(); };

		return hooks;
	};

	void Editor::DrawTwoD()
	{
		if (m_focusTwoD)
		{
			ImGui::SetNextWindowFocus();
			m_focusTwoD = false;
		};

		if (ImGui::Begin(EditorPanels::TwoD, &m_showTwoD))
		{
			if (m_projectWatch.Changed())
				RefreshProjectAssets();

			m_twoDFolderTree.DrawSearch();

			ImGui::BeginChild("##2d", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);

			const TwoDFolders::Node root = m_twoDFolders.Build(m_twoDFiles);

			if (m_projectPath.empty())
				ImGui::TextDisabled("No project open.");
			else if (root.children.empty() && root.items.empty())
				ImGui::TextDisabled("No textures yet. Right-click to create one.");

			const FolderTree::Hooks hooks = TwoDFolderHooks();

			m_twoDFolderTree.Draw(m_twoDFolders, root, hooks);

			m_revealSelectedTwoD = false;

			// A file from outside goes in the project folder.
			if (!m_projectPath.empty())
				TakeImportFolder(m_projectPath, ImGui::IsWindowHovered());

			// Dropped anywhere else in the list, it goes to the top.
			if (ImGui::BeginDragDropTargetCustom(ImGui::GetCurrentWindow()->InnerRect, ImGui::GetID("##2d_top")))
			{
				FolderTree::Drop(m_twoDFolders, "", hooks);
				ImGui::EndDragDropTarget();
			};

			if (!m_projectPath.empty() &&
				ImGui::BeginPopupContextWindow(
					"##2d_create",
					ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
			{
				DrawTwoDCreateMenu("");
				ImGui::EndPopup();
			};

			ImGui::EndChild();
		};

		ImGui::End();
	};

	void Editor::DrawTwoDEntry(const std::string& file)
	{
		const AssetNode node{
			(std::filesystem::path(m_projectPath) / file).lexically_normal().string(),
			std::filesystem::path(file).filename().string() };

		ImGuiTreeNodeFlags flags =
			ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;

		if (file == m_selectedTwoD)
			flags |= ImGuiTreeNodeFlags_Selected;

		const Texture* thumbnail = Thumbnail(node.path);
		const std::string icon = thumbnail != nullptr ? ThumbnailSpaces() : EditorIcons::Texture;

		ImGui::TreeNodeEx(("2d:" + file).c_str(), flags, "%s %s", icon.c_str(), node.name.c_str());

		if (thumbnail != nullptr)
			DrawThumbnail(*thumbnail);

		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
			m_selectedTwoD = file;

		// On the release, so dragging one out to a field leaves the panel shut.
		const ImGuiIO& io = ImGui::GetIO();
		const float drag_threshold = io.MouseDragThreshold * io.MouseDragThreshold;

		if (file == m_selectedTwoD &&
			ImGui::IsItemHovered() &&
			ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
			io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] < drag_threshold)
			OpenSprite(node.path);

		ImGui::SetItemTooltip("%s", file.c_str());

		DragAsset(node);

		if (!ImGui::BeginPopupContextItem())
			return;

		m_selectedTwoD = file;

		const std::string folder = m_twoDFolders.FolderOf(file);

		if (ImGui::MenuItem("Open"))
			OpenSprite(node.path);

		if (ImGui::MenuItem("Show in Explorer"))
			ShowInExplorer(node.path);

		if (!folder.empty() && ImGui::MenuItem("Move to Top"))
		{
			m_twoDFolders.Move(file, "");
			SaveTwoDFolders();
		};

		ImGui::Separator();

		DrawTwoDCreateMenu(folder);

		ImGui::EndPopup();
	};

	void Editor::DrawTwoDCreateMenu(const std::string& folder)
	{
		if (!ImGui::BeginMenu("Create"))
			return;

		// Kept in the project file's .loom2d, so not without one.
		if (ImGui::MenuItem((std::string(EditorIcons::Folder) + " Folder").c_str(), nullptr, false, !m_projectFile.empty()))
		{
			AddNewFolder(m_twoDFolders, m_twoDFolderTree, folder);
			SaveTwoDFolders();
		};

		for (const AssetKindInfo& info : assetKinds)
			if (IsTwoD(info.kind) && ImGui::MenuItem((std::string(info.icon) + " " + info.label).c_str()))
			{
				AskForAsset(info.kind, m_projectPath);
				m_newTwoDFolder = folder;
			};

		ImGui::EndMenu();
	};

	FolderTree::Hooks Editor::TwoDFolderHooks()
	{
		FolderTree::Hooks hooks;
		hooks.item_payload = ASSET_PAYLOAD;
		hooks.folder_payload = two_d_folder_payload;
		hooks.takes = [this](const std::string& file) { return std::ranges::find(m_twoDFiles, file) != m_twoDFiles.end(); };
		hooks.panel = "2D";
		hooks.reveal = [this](const std::string& folder) { return m_revealSelectedTwoD && VirtualFolders::IsAtOrUnder(m_twoDFolders.FolderOf(m_selectedTwoD), folder); };
		hooks.create = [this](const std::string& folder) { DrawTwoDCreateMenu(folder); };
		hooks.item = [this](const std::string& file) { DrawTwoDEntry(file); };
		hooks.name = [](const std::string& file) { return std::filesystem::path(file).filename().string(); };
		hooks.changed = [this]() { SaveTwoDFolders(); };

		return hooks;
	};
};
