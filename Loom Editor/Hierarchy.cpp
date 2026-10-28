#include "Hierarchy.h"

#include "GameObject.h"

#include <algorithm>
#include <cmath>


namespace Loom::Hierarchy
{
	namespace
	{
		// How much of a row, top and bottom, drops beside it rather than into it.
		constexpr float SIBLING_EDGE = 0.25f;
	};

	#pragma region Rows

	bool Contains(const GameObject& root, const GameObject* target)
	{
		for (const GameObject* at = target; at; at = at->GetParent())
			if (at == &root)
				return true;

		return false;
	};

	std::pair<size_t, size_t> Visible(const View& view, size_t count)
	{
		const float rows = (float)count;

		return {
			(size_t)std::clamp(std::floor((view.min - view.top) / view.row), 0.0f, rows),
			(size_t)std::clamp(std::ceil((view.max - view.top) / view.row), 0.0f, rows) };
	};

	RowsBelow CountRowsBelow(OpenUnder& open_under)
	{
		RowsBelow rows_below{ };

		const auto count =
			[&](const auto& self, const GameObject& node) -> size_t
			{
				if (const auto found = rows_below.find(&node); found != rows_below.end())
					return found->second;

				size_t rows = node.GetChildren().size();

				if (const auto under = open_under.find(&node); under != open_under.end())
					for (const auto& [index, child] : under->second)
						rows += self(self, *child);

				return rows_below[&node] = rows;
			};

		for (auto& [parent, children] : open_under)
		{
			std::sort(children.begin(), children.end());

			for (const auto& [index, child] : children)
				count(count, *child);
		};

		return rows_below;
	};

	std::vector<Step> Plan(
		const GameObject& parent,
		const OpenChildren* open,
		const RowsBelow& rows_below,
		const View& view,
		const GameObject* kept)
	{
		const std::vector<GameObject*>& children = parent.GetChildren();

		size_t kept_at = children.size();

		for (const GameObject* at = kept; at; at = at->GetParent())
			if (at->GetParent() == &parent)
			{
				kept_at = at->GetSiblingIndex();
				break;
			};

		std::vector<Step> steps{ };
		size_t skip = 0;
		size_t rows = 0;

		const auto take =
			[&](size_t index)
			{
				steps.push_back({ skip, children[index] });
				skip = 0;
			};

		// The closed children from up to to, one row each.
		const auto closed =
			[&](size_t from, size_t to)
			{
				View run = view;
				run.top += rows * view.row;

				const auto [first, last] = Visible(run, to - from);
				size_t at = from;

				const auto draw =
					[&](size_t index)
					{
						skip += index - at;
						take(index);
						at = index + 1;
					};

				if (kept_at >= from && kept_at < from + first)
					draw(kept_at);

				for (size_t index = (std::max)(from + first, at); index < from + last; index++)
					draw(index);

				if (kept_at >= (std::max)(from + last, at) && kept_at < to)
					draw(kept_at);

				skip += to - at;
				rows += to - from;
			};

		size_t from = 0;

		if (open)
			for (const auto& [index, child] : *open)
			{
				// Gathered at the top of the frame, so one moved since is left alone.
				if (index < from || index >= children.size() || children[index] != child)
					continue;

				closed(from, index);
				from = index + 1;

				const size_t block = 1 + rows_below.at(child);
				const float top = view.top + rows * view.row;

				if (index == kept_at || (top < view.max && top + block * view.row > view.min))
					take(index);
				else
					skip += block;

				rows += block;
			};

		closed(from, children.size());
		steps.push_back({ skip, nullptr });

		return steps;
	};

	#pragma endregion

	#pragma region Dropping

	Zone ZoneOf(float top, float bottom, float mouse, bool root)
	{
		const float edge = (bottom - top) * SIBLING_EDGE;

		if (root || (mouse > top + edge && mouse < bottom - edge))
			return Zone::Into;

		return mouse <= top + edge ? Zone::Above : Zone::Below;
	};

	Placement Place(GameObject& row, bool open, Zone zone, const GameObject* moving)
	{
		if (zone == Zone::Into)
			return { &row, nullptr };

		if (zone == Zone::Above)
			return { row.GetParent(), &row };

		const std::vector<GameObject*>& children = row.GetChildren();

		// Below an open node is where its first child is drawn.
		if (open && !children.empty())
			return { &row, children.front() };

		GameObject* parent = row.GetParent();

		const std::vector<GameObject*>& siblings = parent->GetChildren();
		auto next = std::find(siblings.begin(), siblings.end(), &row) + 1;

		if (next != siblings.end() && *next == moving)
			next++;

		return { parent, next == siblings.end() ? nullptr : *next };
	};

	#pragma endregion
};
