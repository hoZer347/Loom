#pragma once


namespace Loom
{
	struct AttributeBase;
	struct AttributeOwner;

	/// A row per attribute, showing the base it was given, the value the modifiers made
	/// of it, and the difference between the two, which is the part anybody is really
	/// reading.
	struct AttributeGui final
	{
		/// One row. The base is editable for a numeric attribute, so a value can be tried
		/// without a rebuild.
		static void Draw(const char* name, AttributeBase& attribute);

		/// Every attribute an owner declared, in declaration order.
		static void Draw(AttributeOwner& owner);

		AttributeGui() = delete;
	};
};
