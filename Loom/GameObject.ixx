export module GameObject;

import DataTypes;

import std;
import Transform;


namespace Loom
{
	struct GameObject
	{
		GameObject()
		{

		};

		virtual ~GameObject()
		{

		};

		operator Transform& () { return transform; }

		ptr<Transform> transform;
	};
};
