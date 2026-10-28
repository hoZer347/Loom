#include "PlayHistory.h"

#include "Component.h"
#include "GameObject.h"
#include "Scene.h"
#include "SceneSerializer.h"

#include <algorithm>
#include <cstring>
#include <sstream>


namespace Loom
{
	namespace
	{
		#pragma region Scene Text

		struct Block
		{
			std::string line{ };
			Guid guid{ };
			std::vector<std::string> fields{ };
		};

		// The GameObject first, then each component in order.
		std::vector<Block> Blocks(const std::string& text)
		{
			const size_t keyword = strlen(SceneSerializer::component_keyword);

			std::vector<Block> blocks{ };
			std::stringstream lines(text);
			std::string line;

			while (std::getline(lines, line))
			{
				size_t start = 0;
				const int depth = SceneSerializer::DepthOf(line, start);
				const std::string content = line.substr(start);

				if (depth == 0 || content.rfind(SceneSerializer::component_keyword, 0) == 0)
				{
					Block& block = blocks.emplace_back();
					std::string type;

					block.line = content;

					if (depth != 0)
						SceneSerializer::SplitNameAndGuid(content.substr(keyword), type, block.guid);
				}
				else if (!blocks.empty())
					blocks.back().fields.push_back(content);
			};

			return blocks;
		};

		std::string Text(const std::vector<Block>& blocks)
		{
			std::string text;

			for (size_t i = 0; i < blocks.size(); i++)
			{
				const int depth = i == 0 ? 0 : 1;

				text += std::string(depth, '\t') + blocks[i].line + '\n';

				for (const std::string& field : blocks[i].fields)
					text += std::string(depth + 1, '\t') + field + '\n';
			};

			return text;
		};

		const Block* Find(const std::vector<Block>& blocks, const Guid& guid)
		{
			const auto found = std::find_if(
				blocks.begin() + 1,
				blocks.end(),
				[&guid](const Block& block) { return block.guid == guid; });

			return found == blocks.end() ? nullptr : &*found;
		};

		std::string KeyOf(const std::string& field)
		{
			return field.substr(0, field.find('='));
		};

		// Each field line of to that from read otherwise, written over now's.
		void Patch(std::vector<std::string>& now, const std::vector<std::string>& from, const std::vector<std::string>& to)
		{
			for (const std::string& wanted : to)
			{
				if (std::find(from.begin(), from.end(), wanted) != from.end())
					continue;

				const std::string key = KeyOf(wanted);
				const auto is = std::find_if(
					now.begin(),
					now.end(),
					[&key](const std::string& field) { return KeyOf(field) == key; });

				if (is == now.end())
					now.push_back(wanted);
				else *is = wanted;
			};
		};

		#pragma endregion

		#pragma region References

		GameObject* GameObjectOf(LoomObject* object)
		{
			if (ComponentBase* component = dynamic_cast<ComponentBase*>(object))
				return component->GetGameObject();

			return dynamic_cast<GameObject*>(object);
		};

		// object's Reference fields that point at one of gameObject's components.
		void Hold(const LoomObject& object, const GameObject& gameObject, std::vector<PlayHistory::Field>& held)
		{
			const std::vector<SerializedField>& fields = object.GetFields();

			for (size_t i = 0; i < fields.size(); i++)
			{
				if (fields[i].type != FieldType::Reference)
					continue;

				const ComponentBase* component = dynamic_cast<const ComponentBase*>(fields[i].GetReference());

				if (component == nullptr || component->GetGameObject() != &gameObject)
					continue;

				PlayHistory::Field& field = held.emplace_back();

				field.before = fields[i].Write();
				field.object = object.GetGuid();
				field.index = i;
			};
		};

		// gameObject's own text carries its own references.
		void HoldUnder(const GameObject& at, const GameObject& gameObject, std::vector<PlayHistory::Field>& held)
		{
			if (&at != &gameObject)
			{
				Hold(at, gameObject, held);

				for (const ComponentBase* component : at.GetComponents())
					Hold(*component, gameObject, held);
			};

			for (const GameObject* child : at.GetChildren())
				HoldUnder(*child, gameObject, held);
		};

		#pragma endregion
	};

	#pragma region Recording

	void PlayHistory::RecordFields(std::vector<Field> edits)
	{
		Step step{ };

		for (Field& edit : edits)
		{
			const LoomObject* object = LoomObject::GetByGuid<LoomObject>(edit.object);

			if (object == nullptr)
				continue;

			edit.after = object->GetFields()[edit.index].Write();

			if (edit.after != edit.before)
				step.push_back(Part{ std::move(edit) });
		};

		if (!step.empty())
			Push(std::move(step));
	};

	PlayHistory::Components PlayHistory::ReadComponents(GameObject& gameObject)
	{
		Components read{ gameObject.GetGuid(), SceneSerializer::SerializeOwn(gameObject) };

		for (const Scene* scene : Scene::GetScenes())
			HoldUnder(scene->GetRoot(), gameObject, read.held);

		return read;
	};

	void PlayHistory::RecordComponents(std::vector<Components> before)
	{
		Step step{ };

		for (Components& read : before)
		{
			GameObject* gameObject = LoomObject::GetByGuid<GameObject>(read.gameObject);

			if (gameObject == nullptr)
				continue;

			Part part{ };

			part.before = std::move(read.own);
			part.after = SceneSerializer::SerializeOwn(*gameObject);
			part.object = read.gameObject;
			part.index = components;

			for (Field& held : read.held)
			{
				LoomObject* holder = LoomObject::GetByGuid<LoomObject>(held.object);

				if (holder == nullptr)
					continue;

				held.after = holder->GetFields()[held.index].Write();

				if (held.after != held.before)
					part.held.push_back(std::move(held));
			};

			if (part.after != part.before || !part.held.empty())
				step.push_back(std::move(part));
		};

		if (!step.empty())
			Push(std::move(step));
	};

	#pragma endregion

	#pragma region Stepping

	std::vector<GameObject*> PlayHistory::Undo()
	{
		return Move(m_undo, m_redo, &Sides::after, &Sides::before);
	};

	std::vector<GameObject*> PlayHistory::Redo()
	{
		return Move(m_redo, m_undo, &Sides::before, &Sides::after);
	};

	void PlayHistory::Clear()
	{
		m_undo.clear();
		m_redo.clear();
	};

	void PlayHistory::Push(Step step)
	{
		m_redo.clear();

		if (m_undo.size() == max_steps)
			m_undo.erase(m_undo.begin());

		m_undo.push_back(std::move(step));
	};

	std::vector<GameObject*> PlayHistory::Move(std::vector<Step>& from, std::vector<Step>& to, std::string Sides::* was, std::string Sides::* wanted)
	{
		while (!from.empty())
		{
			Step step = std::move(from.back());
			std::vector<GameObject*> stepped{ };

			from.pop_back();

			for (const Part& part : step)
			{
				LoomObject* object = LoomObject::GetByGuid<LoomObject>(part.object);

				if (object == nullptr)
					continue;

				if (part.index == components)
				{
					PutComponents(static_cast<GameObject&>(*object), part.*was, part.*wanted);

					// A holder the run has pointed elsewhere since keeps what it holds.
					for (const Field& held : part.held)
					{
						LoomObject* holder = LoomObject::GetByGuid<LoomObject>(held.object);

						if (holder == nullptr)
							continue;

						const SerializedField& field = holder->GetFields()[held.index];

						if (field.Write() == held.*was && field.Read(held.*wanted))
							holder->OnFieldChanged(field);
					};
				}
				else
				{
					const SerializedField& field = object->GetFields()[part.index];

					if (field.Read(part.*wanted))
						object->OnFieldChanged(field);
				};

				if (GameObject* on = GameObjectOf(object); std::find(stepped.begin(), stepped.end(), on) == stepped.end())
					stepped.push_back(on);
			};

			if (stepped.empty())
				continue;

			to.push_back(std::move(step));

			return stepped;
		};

		return { };
	};

	void PlayHistory::PutComponents(GameObject& gameObject, const std::string& from, const std::string& to)
	{
		const std::vector<Block> had = Blocks(from);
		const std::vector<Block> wanted = Blocks(to);
		const std::vector<Block> now = Blocks(SceneSerializer::SerializeOwn(gameObject));

		if (had.empty() || wanted.empty() || now.empty())
			return;

		std::vector<Block> put{ now.front() };

		Patch(put.front().fields, had.front().fields, wanted.front().fields);

		for (auto block = wanted.begin() + 1; block != wanted.end(); block++)
		{
			const Block* was = Find(had, block->guid);

			if (const Block* is = Find(now, block->guid))
			{
				put.push_back(*is);

				if (was)
					Patch(put.back().fields, was->fields, block->fields);
			}
			// Taken off by the edit. One the run has taken off since stays off.
			else if (was == nullptr)
				put.push_back(*block);
		};

		// One the run has put on since stays, and one the edit put on goes.
		for (auto block = now.begin() + 1; block != now.end(); block++)
			if (Find(wanted, block->guid) == nullptr && Find(had, block->guid) == nullptr)
				put.push_back(*block);

		SceneSerializer::PendingReferences pending{ };

		if (SceneSerializer::ReadOwn(gameObject, Text(put), pending))
			SceneSerializer::ResolveReferences(pending);
	};

	#pragma endregion
};
