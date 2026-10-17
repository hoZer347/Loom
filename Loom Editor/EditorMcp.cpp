// Before anything that brings in windows.h, which would otherwise pull in
// the old winsock.h and clash with this one.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>

#include "EditorMcp.h"

#include "Editor.h"
#include "EditCommands.h"
#include "EditorLog.h"
#include "FieldNames.h"
#include "UiScript.h"

#include "Component.h"
#include "ComponentRegistry.h"
#include "Engine.h"
#include "GameObject.h"
#include "Renderer.h"
#include "Scene.h"

#include "stb_image_write.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

using json = nlohmann::json;


namespace Loom
{
	struct EditorMcp::Call
	{
		std::string tool;
		json arguments;
		json result;
		bool done = false;
	};

	namespace
	{
		constexpr const char* endpoint = "/mcp";
		constexpr const char* server_name = "loom-editor";
		constexpr const char* server_version = "1.0";

		// Newest first; a client asking for one not here is offered the newest.
		const char* const protocol_versions[] = { "2025-06-18", "2025-03-26", "2024-11-05" };

		constexpr size_t max_request_bytes = 1 << 20;
		constexpr DWORD receive_timeout_ms = 5000;
		constexpr int listen_backlog = 8;
		constexpr std::chrono::minutes call_timeout{ 5 };

		// A capture comes back once the frame it was asked for in is drawn;
		// one that has not by then is not coming, as when the window has no size.
		constexpr int capture_frames = 60;

		constexpr size_t default_console_lines = 100;

		// JSON-RPC's own error codes.
		constexpr int parse_error = -32700;
		constexpr int invalid_request = -32600;
		constexpr int method_not_found = -32601;
		constexpr int invalid_params = -32602;

		constexpr const char* instructions =
			"The Loom editor, live, as the person at it is using it. "
			"Objects are named by guid or by name; hierarchy lists both. "
			"inspect shows each field as the scene file writes it, and set_field takes values in that form. "
			"Edits land in the editor's undo history. "
			"The ui tool drives the panels with in-process input; ui_items says what can be aimed at.";

		struct ToolError : std::runtime_error
		{
			using std::runtime_error::runtime_error;
		};

		struct Context
		{
			Editor& editor;
			UiScript& script;
			EditorMcp::Call& call;
			EditorMcp::Poll& poll;
		};

		json Text(const std::string& text)
		{
			return { { "content", json::array({ { { "type", "text" }, { "text", text } } }) } };
		};

		json Failure(const std::string& why)
		{
			json result = Text(why);
			result["isError"] = true;

			return result;
		};

		std::string Argument(const json& arguments, const char* name)
		{
			if (!arguments.contains(name) || !arguments[name].is_string())
				throw ToolError(std::string("needs ") + name);

			return arguments[name].get<std::string>();
		};

		std::string OptionalArgument(const json& arguments, const char* name)
		{
			return arguments.contains(name) && arguments[name].is_string() ? arguments[name].get<std::string>() : "";
		};

		bool SameText(const std::string& a, const std::string& b)
		{
			return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
				[](char x, char y) { return tolower((unsigned char)x) == tolower((unsigned char)y); });
		};

		GameObject* FindNamed(GameObject& parent, const std::string& name)
		{
			for (GameObject* child : parent.GetChildren())
			{
				if (child->GetName() == name)
					return child;

				if (GameObject* found = FindNamed(*child, name))
					return found;
			};

			return nullptr;
		};

		Scene* FindScene(const std::string& text)
		{
			Guid guid{ };
			const bool is_guid = Guid::TryParse(text, guid);

			for (Scene* scene : Scene::GetScenes())
				if ((is_guid && scene->GetGuid() == guid) || scene->GetName() == text)
					return scene;

			return nullptr;
		};

		// A GameObject by guid or name, or a scene's root by the scene's.
		GameObject& FindObject(const std::string& text)
		{
			Guid guid{ };

			if (Guid::TryParse(text, guid))
				if (GameObject* gameObject = LoomObject::GetByGuid<GameObject>(guid))
					return *gameObject;

			for (Scene* scene : Scene::GetScenes())
				if (GameObject* found = FindNamed(scene->GetRoot(), text))
					return *found;

			if (Scene* scene = FindScene(text))
				return scene->GetRoot();

			throw ToolError("nothing is called " + text);
		};

		std::string Named(LoomObject& object)
		{
			return object.GetName() + " (" + object.GetGuid().ToString() + ")";
		};

		// Components go by their type, having no names of their own.
		std::string Named(ComponentBase& component)
		{
			return ComponentRegistry::NameOf(component) + " (" + component.GetGuid().ToString() + ")";
		};

		void WriteTree(std::ostream& out, GameObject& gameObject, int depth)
		{
			out << std::string(depth, '\t') << Named(gameObject);

			if (gameObject.IsDisabled())
				out << " disabled";

			const char* separator = ": ";

			for (ComponentBase* component : gameObject.GetComponents())
			{
				out << separator << ComponentRegistry::NameOf(*component);
				separator = ", ";
			};

			out << '\n';

			for (GameObject* child : gameObject.GetChildren())
				WriteTree(out, *child, depth + 1);
		};

		void WriteFields(std::ostream& out, LoomObject& object)
		{
			const std::vector<std::string> labels = FieldNames::Of(object);
			const auto enumerators = FieldNames::EnumeratorsOf(object);

			for (size_t i = 0; i < object.GetFields().size(); i++)
			{
				const SerializedField& field = object.GetFields()[i];

				if (field.type == FieldType::Uniforms)
					continue;

				out << '\t' << labels[i] << " = " << field.Write();

				if (enumerators[i])
				{
					const char* separator = "  (one of ";

					for (const FieldNames::Enumerator& enumerator : *enumerators[i])
					{
						out << separator << enumerator.label << " = " << enumerator.value;
						separator = ", ";
					};

					out << ')';
				};

				out << '\n';
			};
		};

		// The component set_field writes into, by type or guid. Null for the
		// GameObject itself, whose fields hold its Transform.
		ComponentBase* FindComponent(GameObject& gameObject, const std::string& component)
		{
			if (component.empty() || SameText(component, "GameObject"))
				return nullptr;

			for (ComponentBase* part : gameObject.GetComponents())
				if (SameText(ComponentRegistry::NameOf(*part), component) || part->GetGuid().ToString() == component)
					return part;

			throw ToolError(gameObject.GetName() + " has no " + component);
		};

		std::string StateOf(Editor& editor)
		{
			std::ostringstream out;

			const ScriptLibrary& scripts = editor.GetScripts();

			out << "Project: " << (editor.GetProjectPath().empty() ? "none" : editor.GetProjectPath()) << '\n';

			out << "Play: " <<
				(!editor.IsPlaying() ? "stopped" :
					Engine::updateScenes ? "running" :
					scripts.IsBuilding() ? "starting once the scripts are built" : "paused") << '\n';

			out << "Scripts: " <<
				(!scripts.HasProject() ? "none" :
					scripts.IsBuilding() ? "building" :
					scripts.IsStale() ? "changed since the last build" :
					scripts.LastBuildSucceeded() ? "built" : "last build failed") << '\n';

			out << "Selected: " << (editor.GetSelected() ? Named(*editor.GetSelected()) : "nothing") << '\n';

			out << "Scenes:\n";

			for (Scene* scene : Scene::GetScenes())
			{
				out << '\t' << Named(*scene);

				if (scene == editor.GetActiveScene())
					out << " active";

				if (editor.IsOwned(scene))
				{
					const std::string path = editor.ScenePathOf(scene);
					out << (path.empty() ? " never saved" : " " + path);
				};

				out << '\n';
			};

			return out.str();
		};

		// The id the console's next line will have.
		size_t NextLogLine()
		{
			size_t next = 0;

			EditorLog::Get().ForEach([&next](size_t id, const std::string&, bool) { next = id + 1; });

			return next;
		};

		std::string LogFrom(size_t first_id, bool errors_only, size_t limit)
		{
			std::vector<std::string> lines{ };

			EditorLog::Get().ForEach(
				[&](size_t id, const std::string& text, bool is_error)
				{
					if (id >= first_id && (is_error || !errors_only))
						lines.push_back(std::to_string(id) + (is_error ? "\terror\t" : "\t\t") + text);
				});

			const size_t first = lines.size() > limit ? lines.size() - limit : 0;

			std::string out{ };

			for (size_t i = first; i < lines.size(); i++)
				out += lines[i] + '\n';

			return out;
		};

		std::string Base64(const std::vector<unsigned char>& bytes)
		{
			constexpr const char* digits = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
			constexpr int group_bytes = 3;
			constexpr int digit_bits = 6;
			constexpr unsigned digit_mask = 0x3F;
			constexpr int byte_bits = 8;

			std::string out{ };
			out.reserve((bytes.size() + group_bytes - 1) / group_bytes * (group_bytes + 1));

			for (size_t i = 0; i < bytes.size(); i += group_bytes)
			{
				const size_t count = std::min<size_t>(group_bytes, bytes.size() - i);
				unsigned group = 0;

				for (int b = 0; b < group_bytes; b++)
					group = (group << byte_bits) | (b < (int)count ? bytes[i + b] : 0);

				for (int d = 0; d <= group_bytes; d++)
					out += d <= (int)count ? digits[(group >> (digit_bits * (group_bytes - d))) & digit_mask] : '=';
			};

			return out;
		};

		// The capture comes bottom row first, as a bitmap stores it.
		std::vector<unsigned char> Png(int width, int height, const std::vector<uint8_t>& rgb)
		{
			constexpr int channels = 3;
			const size_t row = (size_t)width * channels;

			std::vector<uint8_t> flipped(rgb.size());

			for (int y = 0; y < height; y++)
				std::copy_n(rgb.begin() + (height - 1 - y) * row, row, flipped.begin() + y * row);

			std::vector<unsigned char> png{ };

			stbi_write_png_to_func(
				[](void* context, void* data, int size)
				{
					auto* out = (std::vector<unsigned char>*)context;
					out->insert(out->end(), (unsigned char*)data, (unsigned char*)data + size);
				},
				&png, width, height, channels, flipped.data(), (int)row);

			return png;
		};

		json State(Context& context, const json&)
		{
			return Text(StateOf(context.editor));
		};

		json Hierarchy(Context& context, const json& arguments)
		{
			const std::string name = OptionalArgument(arguments, "scene");
			const Scene* only = name.empty() ? nullptr : FindScene(name);

			std::ostringstream out;

			for (Scene* scene : Scene::GetScenes())
			{
				if (!name.empty() && scene != only)
					continue;

				out << "Scene " << Named(*scene) << '\n';

				for (GameObject* child : scene->GetRoot().GetChildren())
					WriteTree(out, *child, 1);
			};

			if (out.tellp() == 0)
				throw ToolError(name.empty() ? "no scenes are open" : "no scene is called " + name);

			return Text(out.str());
		};

		json Inspect(Context& context, const json& arguments)
		{
			GameObject& gameObject = FindObject(Argument(arguments, "object"));

			std::ostringstream out;

			out << "GameObject " << Named(gameObject) << '\n';

			if (Scene* scene = EditCommands::SceneOf(gameObject))
				out << "\tScene = " << scene->GetName() << '\n';

			if (gameObject.GetParent())
				out << "\tParent = " << Named(*gameObject.GetParent()) << '\n';

			out << "\tName = " << gameObject.GetName() << '\n';
			WriteFields(out, gameObject);

			for (ComponentBase* component : gameObject.GetComponents())
			{
				out << "Component " << Named(*component) << '\n';
				WriteFields(out, *component);
			};

			return Text(out.str());
		};

		json SetField(Context& context, const json& arguments)
		{
			GameObject& gameObject = FindObject(Argument(arguments, "object"));
			ComponentBase* component = FindComponent(gameObject, OptionalArgument(arguments, "component"));
			LoomObject& object = component ? *(LoomObject*)component : gameObject;
			const std::string label = Argument(arguments, "field");
			const std::string value = Argument(arguments, "value");

			if (component == nullptr && SameText(label, "Name"))
			{
				gameObject.SetName(value);
				context.editor.MarkEdited();

				return Text("Name = " + value);
			};

			const std::vector<std::string> labels = FieldNames::Of(object);
			const auto enumerators = FieldNames::EnumeratorsOf(object);

			for (size_t i = 0; i < labels.size(); i++)
			{
				if (!SameText(labels[i], label))
					continue;

				const SerializedField& field = object.GetFields()[i];

				if (field.type == FieldType::Uniforms)
					throw ToolError(label + " is a shader's variables, which only its component edits");

				std::string text = value;

				if (enumerators[i])
					for (const FieldNames::Enumerator& enumerator : *enumerators[i])
						if (SameText(enumerator.label, value))
							text = std::to_string(enumerator.value);

				if (!field.Read(text))
					throw ToolError(value + " is not a value " + labels[i] + " can hold, which inspect shows the form of");

				object.OnFieldChanged(field);
				context.editor.MarkEdited();

				return Text(labels[i] + " = " + field.Write());
			};

			throw ToolError((component ? Named(*component) : Named(gameObject)) + " has no field " + label);
		};

		json Select(Context& context, const json& arguments)
		{
			const std::string name = OptionalArgument(arguments, "object");

			context.editor.Select(name.empty() ? nullptr : &FindObject(name));

			return Text(StateOf(context.editor));
		};

		json AddObject(Context& context, const json& arguments)
		{
			const std::string parent_name = OptionalArgument(arguments, "parent");
			const std::string name = OptionalArgument(arguments, "name");

			GameObject* parent = nullptr;

			if (!parent_name.empty())
				parent = &FindObject(parent_name);
			else if (Scene* scene = context.editor.GetActiveScene())
				parent = &scene->GetRoot();
			else
				throw ToolError("no scene is active to put it in");

			GameObject* gameObject = name.empty() ? parent->AddChild() : parent->AddChild(name);

			context.editor.Select(gameObject);
			context.editor.MarkEdited();

			return Text("Added " + Named(*gameObject) + " under " + Named(*parent));
		};

		json AddComponent(Context& context, const json& arguments)
		{
			GameObject& gameObject = FindObject(Argument(arguments, "object"));
			const std::string type = Argument(arguments, "type");

			ComponentBase* component = ComponentRegistry::Create(type, gameObject);

			if (component == nullptr)
			{
				std::string known{ };

				for (const auto& [name, factory] : ComponentRegistry::All())
					known += (known.empty() ? "" : ", ") + name;

				throw ToolError(type + " is not a component this build has. It has " + known);
			};

			context.editor.MarkEdited();

			return Text("Added " + Named(*component) + " to " + Named(gameObject));
		};

		json DeleteObject(Context& context, const json& arguments)
		{
			GameObject& gameObject = FindObject(Argument(arguments, "object"));

			if (!EditCommands::CanTake(&gameObject))
				throw ToolError("a scene's root cannot be deleted");

			const std::string name = Named(gameObject);

			context.editor.Select(&gameObject);
			context.editor.DeleteSelected();

			return Text("Deleted " + name);
		};

		json Command(Context& context, const json& arguments)
		{
			Editor& editor = context.editor;
			const std::string command = Argument(arguments, "command");

			if (command == "play")
			{
				if (!Engine::updateScenes)
					editor.TogglePlay();
			}
			else if (command == "pause")
			{
				if (Engine::updateScenes)
					editor.TogglePlay();
			}
			else if (command == "stop")
				editor.Stop();
			else if (command == "compile")
				editor.CompileScripts();
			else if (command == "save")
				editor.SaveActiveScene();
			else if (command == "undo")
				editor.Undo();
			else if (command == "redo")
				editor.Redo();
			else
				throw ToolError(command + " is not a command");

			return Text(StateOf(editor));
		};

		json OpenScene(Context& context, const json& arguments)
		{
			std::filesystem::path path = Argument(arguments, "path");

			if (path.is_relative() && !context.editor.GetProjectPath().empty())
				path = (context.editor.GetProjectPath() / path).lexically_normal();

			Scene* scene = context.editor.OpenScene(path.string());

			if (scene == nullptr)
				throw ToolError("could not open " + path.string() + "; the console says why");

			return Text("Opened " + Named(*scene));
		};

		json Console(Context&, const json& arguments)
		{
			const size_t first_id = arguments.contains("after") ? arguments["after"].get<size_t>() + 1 : 0;
			const bool errors_only = arguments.value("errors_only", false);
			const size_t limit = arguments.value("limit", default_console_lines);

			return Text(LogFrom(first_id, errors_only, limit));
		};

		json Screenshot(Context& context, const json&)
		{
			Renderer* renderer = Renderer::Get();

			if (renderer == nullptr)
				throw ToolError("there is no renderer to capture from");

			struct Capture
			{
				json result;
				bool taken = false;
				int frames = 0;
			};

			auto capture = std::make_shared<Capture>();

			renderer->Capture(
				[capture](int width, int height, const std::vector<uint8_t>& rgb)
				{
					capture->taken = true;

					if (width <= 0 || height <= 0)
					{
						capture->result = Failure("the window has no size");
						return;
					};

					capture->result = { { "content", json::array({ {
						{ "type", "image" },
						{ "mimeType", "image/png" },
						{ "data", Base64(Png(width, height, rgb)) } } }) } };
				});

			EditorMcp::Call& call = context.call;

			context.poll =
				[capture, &call]()
				{
					if (!capture->taken && ++capture->frames < capture_frames)
						return false;

					call.result = capture->taken ? capture->result : Failure("the frame was never drawn");
					return true;
				};

			return { };
		};

		json Ui(Context& context, const json& arguments)
		{
			const std::string script = Argument(arguments, "script");
			const size_t first_id = NextLogLine();

			context.script.Run(script);

			UiScript& running = context.script;
			EditorMcp::Call& call = context.call;

			context.poll =
				[&running, &call, first_id]()
				{
					if (!running.Finished())
						return false;

					// A failure is logged too, last.
					const std::string logged = LogFrom(first_id, false, SIZE_MAX);

					call.result = running.Failed() ? Failure(logged) : Text("Done\n" + logged);

					return true;
				};

			return { };
		};

		json UiItems(Context& context, const json& arguments)
		{
			std::ostringstream out;

			out << "window\tlabel\tx\ty\twidth\theight\n";
			context.script.WriteItems(out, OptionalArgument(arguments, "window"));

			return Text(out.str());
		};

		struct Tool
		{
			const char* name;
			const char* description;
			const char* schema;
			json (*run)(Context&, const json&);
		};

		constexpr const char* no_arguments = R"({ "type": "object", "properties": { } })";

		constexpr const char* object_argument = R"({
			"type": "object",
			"properties": { "object": { "type": "string", "description": "A GameObject's guid or name, or a scene's for its root" } },
			"required": [ "object" ] })";

		const Tool tools[] =
		{
			{
				"state",
				"The open project, whether the scene is playing, the scripts' build, the selection, and the open scenes with their files.",
				no_arguments,
				State,
			},
			{
				"hierarchy",
				"Every GameObject in the open scenes, or in one, a line each: name (guid), disabled if it is, then its components.",
				R"({ "type": "object", "properties": { "scene": { "type": "string", "description": "A scene's name or guid; all of them when left out" } } })",
				Hierarchy,
			},
			{
				"inspect",
				"A GameObject's fields, its Transform among them, and every component's, as label = value in the scene file's text.",
				object_argument,
				Inspect,
			},
			{
				"set_field",
				"Writes one field, by the label inspect shows, as the Inspector would. Values take the form inspect writes them in; an enum takes an enumerator's name too.",
				R"({
					"type": "object",
					"properties": {
						"object": { "type": "string", "description": "A GameObject's guid or name" },
						"component": { "type": "string", "description": "A component's type or guid; the GameObject itself, Name and Transform included, when left out" },
						"field": { "type": "string", "description": "The field's label, as inspect shows it" },
						"value": { "type": "string" } },
					"required": [ "object", "field", "value" ] })",
				SetField,
			},
			{
				"select",
				"Selects a GameObject, showing it in the Inspector, or nothing when object is left out.",
				R"({ "type": "object", "properties": { "object": { "type": "string", "description": "A GameObject's guid or name" } } })",
				Select,
			},
			{
				"add_object",
				"Adds a GameObject and selects it.",
				R"({
					"type": "object",
					"properties": {
						"name": { "type": "string" },
						"parent": { "type": "string", "description": "A GameObject's or a scene's guid or name; the active scene when left out" } } })",
				AddObject,
			},
			{
				"add_component",
				"Attaches a component to a GameObject by its registered name, a script's among them once compiled.",
				R"({
					"type": "object",
					"properties": {
						"object": { "type": "string", "description": "A GameObject's guid or name" },
						"type": { "type": "string", "description": "The component's name, as Add Component lists it" } },
					"required": [ "object", "type" ] })",
				AddComponent,
			},
			{
				"delete_object",
				"Deletes a GameObject and everything under it.",
				object_argument,
				DeleteObject,
			},
			{
				"command",
				"One of the editor's commands: play, pause, stop, compile (the scripts), save (the active scene), undo or redo. Answers with the state after.",
				R"({
					"type": "object",
					"properties": { "command": { "type": "string", "enum": [ "play", "pause", "stop", "compile", "save", "undo", "redo" ] } },
					"required": [ "command" ] })",
				Command,
			},
			{
				"open_scene",
				"Opens a scene file beside those already open and makes it active.",
				R"({
					"type": "object",
					"properties": { "path": { "type": "string", "description": "A .loomscene, from the project folder unless absolute" } },
					"required": [ "path" ] })",
				OpenScene,
			},
			{
				"console",
				"The Console panel's lines, newest last, each with its id, which after takes to read only what came since.",
				R"({
					"type": "object",
					"properties": {
						"after": { "type": "integer", "description": "Only lines with a greater id" },
						"errors_only": { "type": "boolean" },
						"limit": { "type": "integer", "description": "The newest this many; 100 when left out" } } })",
				Console,
			},
			{
				"screenshot",
				"The whole editor window as a PNG, as the next frame draws it.",
				no_arguments,
				Screenshot,
			},
			{
				"ui",
				"Drives the panels with input injected in-process, one UiScript command a line, and answers once they have run with what they logged. "
				"Commands: wait <frames>; move, click, rightclick or doubleclick <target>; drag <target> to <target>; "
				"key <chord> (ImGui key names joined by +, as ctrl+z); type <text>; screenshot <file>; items. "
				"A target is x y in pixels from the window's top left, or \"window\" \"label\" [left|right] [dx dy], "
				"which waits for the item to be drawn. The mouse is the person's too, so they may notice it move.",
				R"({
					"type": "object",
					"properties": { "script": { "type": "string", "description": "UiScript commands, one a line" } },
					"required": [ "script" ] })",
				Ui,
			},
			{
				"ui_items",
				"Every labelled item drawn last frame: window, label, x, y, width and height, which is what ui aims at.",
				R"({ "type": "object", "properties": { "window": { "type": "string", "description": "Only windows whose names contain this" } } })",
				UiItems,
			},
		};

		const Tool* ToolNamed(const std::string& name)
		{
			for (const Tool& tool : tools)
				if (name == tool.name)
					return &tool;

			return nullptr;
		};

		json ToolList()
		{
			json list = json::array();

			for (const Tool& tool : tools)
				list.push_back({
					{ "name", tool.name },
					{ "description", tool.description },
					{ "inputSchema", json::parse(tool.schema) } });

			return list;
		};

		json Error(const json& id, int code, const std::string& message)
		{
			return { { "jsonrpc", "2.0" }, { "id", id }, { "error", { { "code", code }, { "message", message } } } };
		};

		json Result(const json& id, const json& result)
		{
			return { { "jsonrpc", "2.0" }, { "id", id }, { "result", result } };
		};

		std::string Lower(std::string text)
		{
			std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return (char)tolower(c); });
			return text;
		};

		// A page in a browser can reach a server on this machine; one that
		// sends no Origin, or this machine's, is not a page from elsewhere.
		bool LocalOrigin(const std::string& origin)
		{
			for (const char* local : { "http://127.0.0.1", "http://localhost", "http://[::1]" })
			{
				const size_t length = strlen(local);

				if (origin.compare(0, length, local) == 0 && (origin.size() == length || origin[length] == ':'))
					return true;
			};

			return false;
		};

		void Send(SOCKET client, const char* status, const std::string& body, const char* extra_headers = "")
		{
			std::string response =
				std::string("HTTP/1.1 ") + status + "\r\n"
				"Content-Type: application/json\r\n"
				"Content-Length: " + std::to_string(body.size()) + "\r\n"
				"Connection: close\r\n" + extra_headers + "\r\n" + body;

			size_t sent = 0;

			while (sent < response.size())
			{
				const int count = send(client, response.data() + sent, (int)(response.size() - sent), 0);

				if (count <= 0)
					return;

				sent += count;
			};
		};
	};

	EditorMcp::EditorMcp(Editor& editor, UiScript& script) :
		m_editor(editor),
		m_script(script),
		m_listener(INVALID_SOCKET)
	{
	};

	EditorMcp::~EditorMcp()
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_stopping = true;
		};

		m_answered.notify_all();

		if (m_listener != INVALID_SOCKET)
			closesocket(m_listener);

		if (m_thread.joinable())
			m_thread.join();

		if (m_listener != INVALID_SOCKET)
			WSACleanup();
	};

	bool EditorMcp::Listen(unsigned short port)
	{
		WSADATA data{ };

		if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
		{
			std::cerr << "The MCP server could not start Windows Sockets" << std::endl;
			return false;
		};

		const SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

		sockaddr_in address{ };
		address.sin_family = AF_INET;
		address.sin_port = htons(port);
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

		if (listener == INVALID_SOCKET ||
			bind(listener, (const sockaddr*)&address, sizeof(address)) != 0 ||
			listen(listener, listen_backlog) != 0)
		{
			std::cerr << "The MCP server could not listen on port " << port << ", which another editor may have" << std::endl;

			if (listener != INVALID_SOCKET)
				closesocket(listener);

			WSACleanup();
			return false;
		};

		m_listener = listener;
		m_thread = std::thread(&EditorMcp::Serve, this);

		std::cout << "MCP server at http://127.0.0.1:" << port << endpoint << std::endl;

		return true;
	};

	void EditorMcp::Serve()
	{
		while (true)
		{
			const SOCKET client = accept(m_listener, nullptr, nullptr);

			// Closing the listener is how the editor says to stop.
			if (client == INVALID_SOCKET)
				return;

			Answer(client);
			closesocket(client);
		};
	};

	void EditorMcp::Answer(uintptr_t connection)
	{
		const SOCKET client = connection;

		setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char*)&receive_timeout_ms, sizeof(receive_timeout_ms));

		std::string request{ };
		size_t header_end = std::string::npos;
		char buffer[4096];

		while ((header_end = request.find("\r\n\r\n")) == std::string::npos)
		{
			const int count = recv(client, buffer, sizeof(buffer), 0);

			if (count <= 0)
				return;

			request.append(buffer, count);

			if (request.size() > max_request_bytes)
				return Send(client, "413 Content Too Large", "");
		};

		std::istringstream head(request.substr(0, header_end));
		std::string method, target, line;
		head >> method >> target;
		std::getline(head, line);

		size_t content_length = 0;
		std::string origin{ };

		while (std::getline(head, line))
		{
			const size_t colon = line.find(':');

			if (colon == std::string::npos)
				continue;

			const std::string name = Lower(line.substr(0, colon));
			std::string value = line.substr(colon + 1);

			value.erase(0, value.find_first_not_of(' '));

			if (!value.empty() && value.back() == '\r')
				value.pop_back();

			if (name == "content-length")
				content_length = strtoull(value.c_str(), nullptr, 10);
			else if (name == "origin")
				origin = value;
		};

		if (target.substr(0, target.find('?')) != endpoint)
			return Send(client, "404 Not Found", "");

		if (!origin.empty() && !LocalOrigin(origin))
			return Send(client, "403 Forbidden", "");

		if (method != "POST")
			return Send(client, "405 Method Not Allowed", "", "Allow: POST\r\n");

		if (content_length > max_request_bytes)
			return Send(client, "413 Content Too Large", "");

		std::string body = request.substr(header_end + 4);

		while (body.size() < content_length)
		{
			const int count = recv(client, buffer, sizeof(buffer), 0);

			if (count <= 0)
				return;

			body.append(buffer, count);
		};

		body.resize(content_length);

		const std::string response = Respond(body);

		if (response.empty())
			Send(client, "202 Accepted", "");
		else
			Send(client, "200 OK", response);
	};

	std::string EditorMcp::Respond(const std::string& body)
	{
		const json message = json::parse(body, nullptr, false);

		if (message.is_discarded())
			return Error(nullptr, parse_error, "not JSON").dump();

		if (!message.is_object())
			return Error(nullptr, invalid_request, "not a request").dump();

		// A notification, or a client's answer to a request this server never
		// makes, neither of which is answered.
		if (!message.contains("method") || !message.contains("id"))
			return "";

		const json& id = message["id"];
		const std::string method = message.value("method", "");
		const json params = message.value("params", json::object());

		if (method == "initialize")
		{
			const std::string asked = params.value("protocolVersion", "");
			std::string version = protocol_versions[0];

			for (const char* supported : protocol_versions)
				if (asked == supported)
					version = asked;

			return Result(id, {
				{ "protocolVersion", version },
				{ "capabilities", { { "tools", json::object() } } },
				{ "serverInfo", { { "name", server_name }, { "version", server_version } } },
				{ "instructions", instructions } }).dump();
		};

		if (method == "ping")
			return Result(id, json::object()).dump();

		if (method == "tools/list")
			return Result(id, { { "tools", ToolList() } }).dump();

		if (method != "tools/call")
			return Error(id, method_not_found, method + " is not a method this server has").dump();

		auto call = std::make_shared<Call>();
		call->tool = params.value("name", "");
		call->arguments = params.value("arguments", json::object());

		if (!ToolNamed(call->tool))
			return Error(id, invalid_params, call->tool + " is not a tool this server has").dump();

		Wait(call);

		return Result(id, call->result).dump();
	};

	void EditorMcp::Wait(const std::shared_ptr<Call>& call)
	{
		std::unique_lock<std::mutex> lock(m_mutex);

		m_calls.push_back(call);

		const bool answered = m_answered.wait_for(lock, call_timeout, [&]() { return call->done || m_stopping; });

		if (!call->done)
			call->result = Failure(answered ? "the editor is closing" : "the editor did not answer in time");
	};

	void EditorMcp::Finish(const std::shared_ptr<Call>& call)
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			call->done = true;
		};

		m_answered.notify_all();
	};

	void EditorMcp::Step()
	{
		if (m_running)
		{
			if (!m_poll())
				return;

			Finish(m_running);
			m_running.reset();
			m_poll = nullptr;
		};

		while (true)
		{
			std::shared_ptr<Call> call{ };

			{
				std::lock_guard<std::mutex> lock(m_mutex);

				if (m_calls.empty())
					return;

				call = m_calls.front();
				m_calls.pop_front();
			};

			Context context{ m_editor, m_script, *call, m_poll };

			try
			{
				call->result = ToolNamed(call->tool)->run(context, call->arguments);
			}
			catch (const ToolError& error)
			{
				call->result = Failure(error.what());
			}
			catch (const std::exception& error)
			{
				call->result = Failure(error.what());
			};

			if (m_poll)
			{
				m_running = call;
				return;
			};

			Finish(call);
		};
	};
};
