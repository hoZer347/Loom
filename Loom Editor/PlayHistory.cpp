#include "PlayHistory.h"

#include "Component.h"
#include "GameObject.h"
#include "SceneSerializer.h"

#include <algorithm>
#include <cstring>
#include <sstream>


namespace Loom
{
	namespace
	{
		// A GameObject's own line, or one of its components', with the field
		// lines under it.
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

		GameObject* GameObjectOf(LoomObject* object)
		{
			if (ComponentBase* component = dynamic_cast<ComponentBase*>(object))
				return component->GetGameObject();

			return dynamic_cast<GameObject*>(object);
		};
	};

	void PlayHistory::RecordField(LoomObject& object, size_t index, const std::string& before)
	{
		std::string after = object.GetFields()[index].Write();

		if (after != before)
			Push(Step{ &object, object.m_ID, index, before, std::move(after) });
	};

	void PlayHistory::RecordComponents(GameObject& gameObject, const std::string& before)
	{
		std::string after = SceneSerializer::SerializeOwn(gameObject);

		if (after != before)
			Push(Step{ &gameObject, gameObject.m_ID, components, before, std::move(after) });
	};

	GameObject* PlayHistory::Undo()
	{
		return Move(m_undo, m_redo, &Step::after, &Step::before);
	};

	GameObject* PlayHistory::Redo()
	{
		return Move(m_redo, m_undo, &Step::before, &Step::after);
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

	GameObject* PlayHistory::Move(std::vector<Step>& from, std::vector<Step>& to, std::string Step::* was, std::string Step::* wanted)
	{
		while (!from.empty())
		{
			Step step = std::move(from.back());

			from.pop_back();

			// The address alone could be a new object made where an old one was.
			if (LoomObject::GetByID(step.id) != step.object)
				continue;

			if (step.index == components)
				PutComponents(static_cast<GameObject&>(*step.object), step.*was, step.*wanted);
			else
			{
				const SerializedField& field = step.object->GetFields()[step.index];

				if (field.Read(step.*wanted))
					step.object->OnFieldChanged(field);
			};

			GameObject* stepped = GameObjectOf(step.object);

			to.push_back(std::move(step));

			return stepped;
		};

		return nullptr;
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
};
