#include "Editor.h"

#include "EditorFileDialogs.h"
#include "EditorGui.h"
#include "ModelImporter.h"
#include "ProjectAssets.h"
#include "ProjectTemplate.h"

#include "SceneSerializer.h"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>

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
		constexpr float namePromptWidth = 320.0f;
		constexpr float promptButtonWidth = 120.0f;

		// How far NewShader, NewShader1, ... counts before leaving the name to
		// the user to sort out.
		constexpr int maxDefaultNameSuffix = 1000;

		struct AssetKindInfo
		{
			AssetKind kind;
			const char* label;
			const char* defaultName;

			// Scripts only: the header's template, and where its type turns up
			// once it is compiled.
			ProjectAssets::ScriptKind script = ProjectAssets::ScriptKind::Component;
			const char* appearsIn = nullptr;
		};

		constexpr AssetKindInfo assetKinds[] =
		{
			{ AssetKind::Folder, "Folder", "New Folder" },
			{ AssetKind::Scene, "Scene", "New Scene" },
			{ AssetKind::Script, "Script", "NewScript", ProjectAssets::ScriptKind::Component, "Add Component" },
			{ AssetKind::State, "State", "NewState", ProjectAssets::ScriptKind::State, "a state machine's Start and Current" },
			{ AssetKind::StateMachine, "State Machine", "NewStateMachine", ProjectAssets::ScriptKind::StateMachine, "Add Component" },
			{ AssetKind::Shader, "Shader", "NewShader" },
			{ AssetKind::Texture, "Texture", "NewTexture" },
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

		// Folders first, then files, each alphabetically. Dot-prefixed entries
		// (.vs, .gitignore) and the build output are not assets.
		void Scan(
			const std::filesystem::path& folder,
			const std::filesystem::path& skip,
			AssetNode& node,
			std::vector<std::string>& scenes)
		{
			std::error_code code;

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

					Scan(path, skip, child, scenes);
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

		bool OpenThroughAutomation(const std::string& solution, const std::string& file)
		{
			DWORD process = 0;
			bool busy = false;

			const ComPtr<IDispatch> dte = FindVisualStudioWith(solution, process, busy);

			// A busy one may be the one with the solution, and starting another
			// would open it twice.
			if (!dte && busy)
				std::cerr << "Visual Studio is busy; try again once it has finished." << std::endl;

			if (!dte)
				return busy;

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

			if (FAILED(result))
			{
				std::cerr
					<< "Visual Studio has " << solution << " open but would not open "
					<< file << " (error 0x" << std::hex << result << std::dec << ')' << std::endl;

				return true;
			};

			// The editor has the foreground, having just been clicked, and Windows
			// only lets another process take it when the one holding it says so.
			AllowSetForegroundWindow(process);

			if (window)
				Call(window.Get(), L"Activate", DISPATCH_METHOD, nullptr);

			return true;
		};

		// False when no Visual Studio has the solution open. The editor's thread
		// does not keep COM up, so this brings it up for the length of the call.
		bool OpenInRunningVisualStudio(const std::string& solution, const std::string& file)
		{
			const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

			const bool handled = OpenThroughAutomation(solution, file);

			if (SUCCEEDED(com))
				CoUninitialize();

			return handled;
		};
	};

	void Editor::RefreshProjectAssets()
	{
		WatchProject();

		m_projectScenes.clear();
		m_assets = AssetNode{ m_projectPath, "", true };

		if (m_projectPath.empty())
			return;

		const std::filesystem::path build = m_scripts.HasProject()
			? std::filesystem::path(m_scripts.GetLibrary()).parent_path().lexically_normal()
			: std::filesystem::path();

		Scan(m_projectPath, build, m_assets, m_projectScenes);

		std::sort(m_projectScenes.begin(), m_projectScenes.end());
	};

	void Editor::WatchProject()
	{
		if (m_assetWatchPath == m_projectPath)
			return;

		if (m_assetWatch != nullptr)
			FindCloseChangeNotification(m_assetWatch);

		m_assetWatch = nullptr;
		m_assetWatchPath = m_projectPath;

		if (m_projectPath.empty())
			return;

		const HANDLE watch = FindFirstChangeNotificationA(
			m_projectPath.c_str(),
			TRUE,
			FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME);

		if (watch != INVALID_HANDLE_VALUE)
			m_assetWatch = watch;
	};

	bool Editor::ProjectChanged()
	{
		if (m_assetWatch == nullptr || WaitForSingleObject(m_assetWatch, 0) != WAIT_OBJECT_0)
			return false;

		FindNextChangeNotification(m_assetWatch);

		return true;
	};

	void Editor::DrawProject()
	{
		if (ImGui::Begin("Project", &m_showProject))
		{
			if (m_projectPath.empty())
			{
				ImGui::TextDisabled("No scene folder chosen.");

				if (ImGui::Button("Choose Folder..."))
					m_dialogs->folder.Open();
			}
			else
			{
				if (ProjectChanged())
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

				ImGui::BeginChild("##assets", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border);

				if (m_assets.children.empty())
					ImGui::TextDisabled("Nothing here yet. Right-click to create something.");

				for (const AssetNode& node : m_assets.children)
					DrawAssetNode(node, 0);

				m_revealSelectedAsset = false;

				if (ImGui::BeginPopupContextWindow(
					"##project_create",
					ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
				{
					DrawCreateMenu(m_projectPath);
					ImGui::EndPopup();
				};

				ImGui::EndChild();
			};
		};

		ImGui::End();
	};

	void Editor::DrawAssetNode(const AssetNode& node, int depth)
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

			if (m_revealSelectedAsset && ProjectAssets::IsUnder(m_selectedAsset, node.path))
				ImGui::SetNextItemOpen(true);
		}
		else
			flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

		const bool open = ImGui::TreeNodeEx(node.path.c_str(), flags, "%s", node.name.c_str());

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
			OpenAsset(node.path);

		DragAsset(node);

		ImGui::SetItemTooltip("%s", node.path.c_str());

		DrawAssetContextMenu(node);

		if (node.folder && open)
		{
			for (const AssetNode& child : node.children)
				DrawAssetNode(child, depth + 1);

			ImGui::TreePop();
		};
	};

	void Editor::DrawAssetContextMenu(const AssetNode& node)
	{
		if (!ImGui::BeginPopupContextItem())
			return;

		m_selectedAsset = node.path;

		if (!node.folder && ImGui::MenuItem("Open"))
			OpenAsset(node.path);

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
			if (ImGui::MenuItem(info.label))
				AskForAsset(info.kind, folder);

		ImGui::EndMenu();
	};

	void Editor::AskForAsset(AssetKind kind, const std::string& folder)
	{
		const AssetKindInfo& info = InfoOf(kind);

		m_newAssetKind = kind;
		m_newAssetFolder = folder;
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
		m_selectedAsset = std::filesystem::path(path).lexically_normal().string();
		m_revealSelectedAsset = true;

		RefreshProjectAssets();

		return path;
	};

	bool Editor::IsScriptFile(const std::string& path) const
	{
		if (!m_scripts.HasProject())
			return false;

		const std::filesystem::path extension = std::filesystem::path(path).extension();

		return
			(extension == ".hpp" || extension == ".h" || extension == ".cpp") &&
			ProjectAssets::IsUnder(path, std::filesystem::path(m_scripts.GetProject()).parent_path().string());
	};

	void Editor::OpenScript(const std::string& path)
	{
		// A hand-written project need not have a solution beside it, even though
		// every project the editor writes does.
		const std::string solution =
			std::filesystem::path(m_scripts.GetProject()).replace_extension(".sln").string();

		std::error_code code;
		const bool has_solution = std::filesystem::exists(solution, code);

		if (has_solution && OpenInRunningVisualStudio(solution, path))
			return;

		const std::string& devenv = ScriptLibrary::FindVisualStudio();

		if (devenv.empty())
			Shell((has_solution ? solution : path).c_str(), "");
		else if (has_solution)
			Shell(devenv.c_str(), '"' + solution + "\" /Command \"File.OpenFile \\\"" + path + "\\\"\"");
		else
			Shell(devenv.c_str(), "/Edit \"" + path + '"');
	};

	void Editor::OpenAsset(const std::string& path)
	{
		if (std::filesystem::path(path).extension() == SceneSerializer::extension)
			OpenScene(path);
		else if (IsScriptFile(path))
			OpenScript(path);
		else
			OpenFile(path);
	};

	bool Editor::OpenInShell(ImGuiContext*, const char* path)
	{
		std::error_code code;

		if (std::filesystem::is_regular_file(path, code))
			OpenFile(path);
		else
			Shell(path, "");

		return true;
	};

	void Editor::DragAsset(const AssetNode& node) const
	{
		if (node.folder || !ImGui::BeginDragDropSource())
			return;

		// Relative, the way components name the files they use.
		const std::string relative = std::filesystem::path(node.path).lexically_relative(m_projectPath).generic_string();

		ImGui::SetDragDropPayload(ASSET_PAYLOAD, relative.c_str(), relative.size() + 1);
		ImGui::TextUnformatted(relative.c_str());

		ImGui::EndDragDropSource();
	};

	void Editor::DrawCreateAssetPrompt()
	{
		if (!m_askForAsset)
			return;

		const AssetKindInfo& info = InfoOf(m_newAssetKind);
		const std::string title = std::string("Create ") + info.label + "###create_asset";

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
};
