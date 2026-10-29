#pragma once

#include "Prefab.h"
#include "ProjectAssets.h"
#include "SceneSerializer.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>


namespace Loom
{
	// What the editor's Create prompt makes.
	enum class AssetKind
	{
		Folder,
		Scene,
		Prefab,
		Script,
		State,
		StateMachine,
		DialogueManager,
		Test,
		Shader,
		PixelShader,
		Dialogue,
		Texture,
		Palette,
		PreVertex,
	};

	namespace AssetKinds
	{
		// The kinds written as one file of their own extension.
		constexpr std::pair<AssetKind, const char*> extensions[] =
		{
			{ AssetKind::Scene, SceneSerializer::extension },
			{ AssetKind::Prefab, Prefab::extension },
			{ AssetKind::Shader, ProjectAssets::shaderExtension },
			{ AssetKind::PixelShader, ProjectAssets::pixelShaderExtension },
			{ AssetKind::Dialogue, ProjectAssets::dialogueExtension },
			{ AssetKind::PreVertex, ProjectAssets::preVertexExtension },
			{ AssetKind::Texture, ProjectAssets::textureExtension },
		};

		inline const char* ExtensionOf(AssetKind kind)
		{
			for (const auto& [made, extension] : extensions)
				if (made == kind)
					return extension;

			return "";
		};

		// What New in an asset field taking those extensions makes.
		inline std::optional<AssetKind> Making(const std::vector<std::string>& accepted)
		{
			for (const auto& [kind, extension] : extensions)
				if (std::find(accepted.begin(), accepted.end(), extension) != accepted.end())
					return kind;

			return std::nullopt;
		};
	};
};
