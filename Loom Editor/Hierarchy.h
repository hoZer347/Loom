#pragma once

#include <cstddef>
#include <unordered_map>
#include <utility>
#include <vector>


namespace Loom
{
	struct GameObject;

	/**
	* Loom::Hierarchy
	* - Which rows of the Hierarchy panel are drawn, worked out from what is
	*   open and where the view is, so a frame costs the rows on screen
	*   rather than the GameObjects the scenes hold
	* - Every row is the same height, so a run of closed rows, or a whole open
	*   subtree, is stepped over by its row count
	*/
	namespace Hierarchy
	{
		// The open nodes under one node, each with its sibling index.
		using OpenChildren = std::vector<std::pair<size_t, const GameObject*>>;
		using OpenUnder = std::unordered_map<const GameObject*, OpenChildren>;

		// The rows below each open node, its open descendants' included.
		using RowsBelow = std::unordered_map<const GameObject*, size_t>;

		// Where a list of rows starts, how tall each row is, and the band of
		// it the panel shows.
		struct View
		{
			float top = 0.0f;
			float row = 0.0f;
			float min = 0.0f;
			float max = 0.0f;
		};

		// One child to draw, after stepping over skip rows. The last step
		// has no child, and steps over the rows after the last one drawn.
		struct Step
		{
			size_t skip = 0;
			GameObject* child = nullptr;
		};

		// Which part of a row a drop lands in: above it, into it or below it.
		enum class Zone
		{
			Above,
			Into,
			Below,
		};

		// Under parent, ahead of before, or last when before is null.
		struct Placement
		{
			GameObject* parent = nullptr;
			const GameObject* before = nullptr;

			bool operator==(const Placement&) const = default;
		};

		// Whether target is root or under it, walking up from target, which
		// has to be alive.
		bool Contains(const GameObject& root, const GameObject* target);

		// The rows from first up to last of count rows that the view shows.
		std::pair<size_t, size_t> Visible(const View& view, size_t count);

		// Sorts each node's open children by sibling index, and counts the
		// rows below each of them.
		RowsBelow CountRowsBelow(OpenUnder& open_under);

		// The children of parent to draw, in order, taken before any is drawn
		// so that one moved by a drop while the rows draw is not drawn twice.
		// An open child is drawn whole if any of its rows is in view. kept is
		// drawn wherever it is, with whichever child it is under.
		std::vector<Step> Plan(
			const GameObject& parent,
			const OpenChildren* open,
			const RowsBelow& rows_below,
			const View& view,
			const GameObject* kept);

		// The part of the row from top to bottom that mouse is in. A root's
		// row has nothing to drop beside it, so all of it is into.
		Zone ZoneOf(float top, float bottom, float mouse, bool root);

		// Where a drop in zone of row puts what it carries, row drawn open or
		// not. moving is the GameObject the drop moves, null for one it makes,
		// and is stepped over when it is the next after row.
		Placement Place(GameObject& row, bool open, Zone zone, const GameObject* moving);
	};
};
