#pragma once

#include <atomic>
#include <string>
#include <unordered_map>


namespace Loom
{
	template <typename BASE, int _ID>
	struct DataPackage
	{
		virtual void Handle() = 0;

		virtual void* Serialize()
		{
			return (void*)this;
		};

		virtual void Deserialize(const void* data)
		{
			memcpy(this, data, sizeof(BASE));
		};

		const int ID = _ID;
	};
};
