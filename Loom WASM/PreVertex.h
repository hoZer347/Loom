#pragma once

#include "Loom API.h"

#include "Asset.h"
#include "Component.h"
#include "Shaders.h"

#include <filesystem>
#include <mutex>
#include <string>
#include <vector>


namespace Loom
{
	/**
	* Loom::PreVertex
	* - Moves the vertices of everything drawn with the shaders it covers
	*   before their own vertex stages read them: every shader with
	*   everything on, or else the shaders listed
	* - Its .loomprevertex file is GLSL defining vec3 PreVertex(vec3 world),
	*   handed each vertex's world position and returning where it goes. It
	*   may declare uniforms, such as u_time, which every draw sets
	* - Put in front of the vertex stage of each shader it covers, and of that
	*   shader's shadow, instanced and skinned programs, as the shader is
	*   compiled. A stage with no aPos or no u_model is left as it is, and
	*   normals are not moved with the vertices
	* - The last one attached is the one in effect. Attaching, detaching or
	*   editing it, or writing its file, compiles again the shaders it covers
	*   and covered
	*/
	struct LOOM_API PreVertex final : Component<PreVertex, "Rendering">
	{
		static constexpr char extension[] = ".loomprevertex";

		#pragma region Lifetime

		// A scene unloading deletes its components without detaching them.
		~PreVertex();

		void OnAttach() override;
		void OnDetach() override;
		void OnFieldChanged(const SerializedField& field) override;

		#pragma endregion

		#pragma region Editor

		// The list only while everything is off.
		bool IsShown(const SerializedField& field) const override;

		#pragma endregion

		#pragma region Shaders

		// The code the shader at path runs in front of its vertex stage, or
		// nothing when no PreVertex covers it.
		static std::string CodeFor(const std::string& shader);

		// Compiles the shaders again once the file of the one in effect has
		// been written since it was read.
		static void ReloadIfChanged();

		#pragma endregion

		#pragma region Fields

		// Relative to the project folder, as a Material's shader is.
		Serial<Asset<extension>> file;

		// Off, only the shaders listed run it.
		Serial<bool> everything = true;

		Serial<AssetList<Shader::extension>> shaders;

		#pragma endregion

	private:
		#pragma region Cover

		// What the one in effect runs, and on which shaders.
		struct Cover
		{
			std::string file;
			std::filesystem::file_time_type written{ };
			std::string code;
			bool everything = false;
			std::vector<std::string> shaders{ };

			bool Covers(const std::string& shader) const;
			bool operator==(const Cover& other) const = default;
		};

		// Takes up what the last one attached says now, and compiles again
		// whatever that changes for.
		static void Refresh();

		static inline std::mutex s_mutex;
		static inline std::vector<PreVertex*> s_attached{ };
		static Cover s_cover;

		#pragma endregion
	};
};
