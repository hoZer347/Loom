#include "EditorMcp.h"

#include "Editor.h"
#include "EditCommands.h"
#include "EditorLog.h"
#include "FieldNames.h"
#include "McpProtocol.h"
#include "McpRelay.h"
#include "SpriteAsset.h"
#include "UiScript.h"

#include "Component.h"
#include "ComponentRegistry.h"
#include "Engine.h"
#include "GameObject.h"
#include "Renderer.h"
#include "Scene.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <vector>

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
		#pragma region Constants

		// How long a request waits for the main thread, and a tool to finish.
		constexpr std::chrono::minutes call_timeout{ 5 };

		// A capture not back by then is not coming, as when the window has no size.
		constexpr int capture_frames = 60;

		constexpr size_t default_console_lines = 100;

		constexpr const char* instructions =
			"The Loom editor, live, as the person at it is using it. "
			"Objects are named by guid or by name; hierarchy lists both. "
			"inspect shows each field as the scene file writes it, and set_field takes values in that form. "
			"Edits land in the editor's undo history. "
			"The ui tool drives the panels with in-process input; ui_items says what can be aimed at.";

		#pragma endregion

		#pragma region Arguments

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

		using McpProtocol::Text;
		using McpProtocol::Failure;

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

		#pragma endregion

		#pragma region Lookup

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

		// Null for the GameObject itself, whose fields hold its Transform.
		ComponentBase* FindComponent(GameObject& gameObject, const std::string& component)
		{
			if (component.empty() || SameText(component, "GameObject"))
				return nullptr;

			for (ComponentBase* part : gameObject.GetComponents())
				if (SameText(ComponentRegistry::NameOf(*part), component) || part->GetGuid().ToString() == component)
					return part;

			throw ToolError(gameObject.GetName() + " has no " + component);
		};

		#pragma endregion

		#pragma region Writing

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

		// Categories in front tell apart two fields of one name.
		std::string CategorisedLabel(const SerializedField& field, const std::string& label)
		{
			return field.Category().empty() ? label : std::string(field.Category()) + '/' + label;
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

				out << '\t' << CategorisedLabel(field, labels[i]) << " = " << field.Write();

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

			for (const LoomObject::FieldButton& button : object.GetButtons())
				out << '\t' << button.name << " (a button, press_button presses it)\n";
		};

		#pragma endregion

		#pragma region State

		std::string StateOf(Editor& editor)
		{
			std::ostringstream out;

			const ScriptLibrary& scripts = editor.GetScripts();

			out << "Project: " << (editor.GetProjectPath().empty() ? "none" : editor.GetProjectPath()) << '\n';

			out << "Play: " <<
				(!editor.IsPlaying() ? "stopped" :
					Engine::updateScenes ? "running" :
					scripts.IsBuilding() ? "starting once the scripts are built" : "paused") << '\n';

			// The Scripts tab's own words for it.
			out << "Scripts: " << (scripts.HasProject() ? scripts.GetStatus() : "none");

			if (scripts.HasProject() && !scripts.IsBuilding() && scripts.IsStale())
				out << " The sources have changed since.";

			out << '\n';

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

		#pragma endregion

		#pragma region Console Log

		// The id the console's next line will have.
		size_t NextLogLine()
		{
			size_t next = 0;

			EditorLog::Get().ForEach([&next](size_t id, const EditorLog::Line&) { next = id + 1; });

			return next;
		};

		std::string LogFrom(size_t first_id, bool errors_only, size_t limit)
		{
			std::vector<std::string> lines{ };

			EditorLog::Get().ForEach(
				[&](size_t id, const EditorLog::Line& line)
				{
					if (id >= first_id && (line.is_error || !errors_only))
						lines.push_back(std::to_string(id) + (line.is_error ? "\terror\t" : "\t\t") + line.text);
				});

			const size_t first = lines.size() > limit ? lines.size() - limit : 0;

			std::string out{ };

			for (size_t i = first; i < lines.size(); i++)
				out += lines[i] + '\n';

			return out;
		};

		#pragma endregion

		#pragma region Capture

		// The capture is RGB from the bottom row up, as a bitmap stores it.
		SpriteAsset::Image Captured(int width, int height, const std::vector<uint8_t>& rgb)
		{
			constexpr int captured_channels = 3;
			constexpr int image_channels = 4;
			constexpr uint8_t opaque = 0xFF;

			SpriteAsset::Image image{ width, height, std::vector<uint8_t>((size_t)width * height * image_channels) };

			for (int y = 0; y < height; y++)
				for (int x = 0; x < width; x++)
				{
					const uint8_t* from = rgb.data() + ((size_t)(height - 1 - y) * width + x) * captured_channels;
					uint8_t* to = image.pixels.data() + ((size_t)y * width + x) * image_channels;

					std::copy_n(from, captured_channels, to);
					to[captured_channels] = opaque;
				};

			return image;
		};

		#pragma endregion

		#pragma region Tool Calls

		#pragma region Inspection

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

		#pragma endregion

		#pragma region Fields and Buttons

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
			const std::vector<SerializedField>& fields = object.GetFields();

			std::vector<size_t> matches;

			for (size_t i = 0; i < labels.size() && matches.empty(); i++)
				if (SameText(CategorisedLabel(fields[i], labels[i]), label))
					matches.push_back(i);

			if (matches.empty())
				for (size_t i = 0; i < labels.size(); i++)
					if (SameText(labels[i], label))
						matches.push_back(i);

			if (matches.empty())
				throw ToolError((component ? Named(*component) : Named(gameObject)) + " has no field " + label);

			if (matches.size() > 1)
			{
				std::string named;

				for (size_t i : matches)
					named += (named.empty() ? "" : ", ") + CategorisedLabel(fields[i], labels[i]);

				throw ToolError(label + " names more than one field, so give its categories too: " + named);
			};

			const size_t i = matches.front();
			const SerializedField& field = fields[i];
			const std::string shown = CategorisedLabel(field, labels[i]);

			if (field.type == FieldType::Uniforms)
				throw ToolError(label + " is a shader's variables, which only its component edits");

			std::string text = value;

			if (enumerators[i])
				for (const FieldNames::Enumerator& enumerator : *enumerators[i])
					if (SameText(enumerator.label, value))
						text = std::to_string(enumerator.value);

			if (!field.Read(text))
				throw ToolError(value + " is not a value " + shown + " can hold, which inspect shows the form of");

			object.OnFieldChanged(field);
			context.editor.MarkEdited();

			return Text(shown + " = " + field.Write());
		};

		json PressButton(Context& context, const json& arguments)
		{
			GameObject& gameObject = FindObject(Argument(arguments, "object"));
			ComponentBase* component = FindComponent(gameObject, OptionalArgument(arguments, "component"));
			LoomObject& object = component ? *(LoomObject*)component : gameObject;
			const std::string label = Argument(arguments, "button");
			const std::vector<LoomObject::FieldButton>& buttons = object.GetButtons();

			std::vector<const LoomObject::FieldButton*> matches;

			for (const LoomObject::FieldButton& button : buttons)
				if (matches.empty() && SameText(button.name, label))
					matches.push_back(&button);

			if (matches.empty())
				for (const LoomObject::FieldButton& button : buttons)
					if (SameText(std::string(NameDisplay(button.name)), label))
						matches.push_back(&button);

			if (matches.empty())
				throw ToolError((component ? Named(*component) : Named(gameObject)) + " has no button " + label);

			if (matches.size() > 1)
			{
				std::string named;

				for (const LoomObject::FieldButton* button : matches)
					named += (named.empty() ? "" : ", ") + button->name;

				throw ToolError(label + " names more than one button, so give its categories too: " + named);
			};

			if (matches.front()->press && *matches.front()->press)
				(*matches.front()->press)();

			context.editor.MarkEdited();

			return Text("Pressed " + matches.front()->name);
		};

		#pragma endregion

		#pragma region Objects

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

		#pragma endregion

		#pragma region Commands

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
			else if (command == "refresh")
				editor.FullRefresh();
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

		#pragma endregion

		#pragma region Screen

		json Screenshot(Context& context, const json& arguments)
		{
			Renderer* renderer = Renderer::Get();

			if (renderer == nullptr)
				throw ToolError("there is no renderer to capture from");

			const std::string window = OptionalArgument(arguments, "window");
			const std::optional<Engine::Region> region = window.empty() ? std::nullopt : UiScript::WindowRegion(window);

			if (!window.empty() && !region)
				throw ToolError("no window whose name contains \"" + window + "\" is showing");

			struct Capture
			{
				json result;
				bool taken = false;
				int frames = 0;
			};

			auto capture = std::make_shared<Capture>();

			renderer->Capture(
				[capture, region](int width, int height, const std::vector<uint8_t>& captured)
				{
					capture->taken = true;

					const std::vector<uint8_t> cut = region ? Engine::Crop(*region, width, height, captured) : std::vector<uint8_t>{ };
					const std::vector<uint8_t>& rgb = region ? cut : captured;

					if (width <= 0 || height <= 0)
					{
						capture->result = Failure("the window has no size");
						return;
					};

					capture->result = { { "content", json::array({ {
						{ "type", "image" },
						{ "mimeType", "image/png" },
						{ "data", McpProtocol::Base64(SpriteAsset::Encode(".png", Captured(width, height, rgb))) } } }) } };
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

			context.script.Listen(true);
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

		// From the next frame, the first the script has listened to.
		json UiItems(Context& context, const json& arguments)
		{
			const std::string window = OptionalArgument(arguments, "window");

			context.script.Listen(true);

			UiScript& script = context.script;
			EditorMcp::Call& call = context.call;

			context.poll =
				[&script, &call, window]()
				{
					std::ostringstream out;

					out << "window\tlabel\tx\ty\twidth\theight\n";
					script.WriteItems(out, window);

					call.result = Text(out.str());
					return true;
				};

			return { };
		};

		#pragma endregion

		#pragma endregion

		#pragma region Tool Table

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
						"field": { "type": "string", "description": "The field's label, as inspect shows it, its categories in front for a field named under some; without them, if no other field has that label" },
						"value": { "type": "string" } },
					"required": [ "object", "field", "value" ] })",
				SetField,
			},
			{
				"press_button",
				"Presses one of an object's buttons, by the name inspect shows, as clicking it in the Inspector would.",
				R"({
					"type": "object",
					"properties": {
						"object": { "type": "string", "description": "A GameObject's guid or name" },
						"component": { "type": "string", "description": "A component's type or guid; the GameObject itself when left out" },
						"button": { "type": "string", "description": "The button's name, as inspect shows it, its categories in front; without them, if no other button has that label" } },
					"required": [ "object", "button" ] })",
				PressButton,
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
				"One of the editor's commands: play, pause, stop, compile (the scripts), save (the active scene), undo, redo or refresh "
				"(Ctrl+Shift+R: rebuild the editor from the engine's sources, save the project and restart on it, after which this server "
				"is back on the same port; a failed build says so in the console and the editor carries on). Answers with the state after.",
				R"({
					"type": "object",
					"properties": { "command": { "type": "string", "enum": [ "play", "pause", "stop", "compile", "save", "undo", "redo", "refresh" ] } },
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
				"The whole editor window as a PNG, as the next frame draws it, or only one panel in it.",
				R"({ "type": "object", "properties": { "window": { "type": "string", "description": "Only the first shown window whose name contains this, such as Inspector" } } })",
				Screenshot,
			},
			{
				"ui",
				"Drives the panels with input injected in-process, one UiScript command a line, and answers once they have run with what they logged. "
				"Commands: wait <frames>; move, click, rightclick or doubleclick <target>; drag <target> to <target>; "
				"key <chord> (ImGui key names joined by +, as ctrl+z); hold or release <chord>, to keep it down through a click; hold or release mouse, to keep the left button down through moves; "
				"scroll <notches> (the wheel where the mouse is, away from you positive); type <text>; screenshot <file>; items; resize <width> <height> (the window, in pixels). "
				"A target is x y in pixels from the window's top left, or \"window\" \"label\" [left|right] [dx dy], "
				"which waits for the item to be drawn. ImGui takes the injected pointer in place of the person's until they next move the mouse.",
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

		#pragma endregion
	};

	#pragma region Lifetime

	EditorMcp::EditorMcp(Editor& editor, UiScript& script) :
		m_editor(editor),
		m_script(script),
		m_scriptListened(script.Listening())
	{
	};

	EditorMcp::~EditorMcp()
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_stopping = true;
		};

		m_answered.notify_all();

		m_relay.reset();
	};

	#pragma endregion

	#pragma region Server

	bool EditorMcp::Listen(unsigned short port)
	{
		m_relay = std::make_unique<McpRelay>(ToolList(), instructions, "state",
			[this](const std::string& name, const json& arguments) -> std::optional<json>
			{
				if (!ToolNamed(name))
					return std::nullopt;

				auto call = std::make_shared<Call>();
				call->tool = name;
				call->arguments = arguments;

				return Wait(call);
			});

		if (m_relay->Listen(port))
			return true;

		m_relay.reset();

		return false;
	};

	#pragma endregion

	#pragma region Call Queue

	json EditorMcp::Wait(const std::shared_ptr<Call>& call)
	{
		std::unique_lock<std::mutex> lock(m_mutex);

		m_calls.push_back(call);

		m_answered.wait_for(lock, call_timeout, [&]() { return call->done || m_stopping; });

		if (call->done)
			return call->result;

		// One still queued is dropped; one running finishes for nobody.
		std::erase(m_calls, call);

		return Failure(m_stopping ? "the editor is closing" : "the editor did not answer in time");
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
			{
				if (std::chrono::steady_clock::now() - m_runningSince < call_timeout)
					return;

				// Only a ui script runs this long, and what it had left is dropped.
				m_running->result = Failure("did not finish in time");
				m_script.Run("");
			};

			m_script.Listen(m_scriptListened);

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
			catch (const std::exception& error)
			{
				call->result = Failure(error.what());
			};

			if (m_poll)
			{
				m_running = call;
				m_runningSince = std::chrono::steady_clock::now();
				return;
			};

			Finish(call);
		};
	};

	#pragma endregion
};
