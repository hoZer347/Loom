#include "SpriteManager.h"

#include "AxisGui.h"
#include "Clock.h"

#include "Engine.h"
#include "Shaders.h"

#include "OpenGL.h"
#include "Renderer.h"

#include "glm/gtc/type_ptr.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <iostream>


namespace Loom
{
	// One quad, shared by every sprite: two triangles of a unit square, which the vertex
	// shader stretches into whatever the sprite's own rect says. Loom's Mesh only feeds
	// attribute 0, so the cell coordinates are derived from the corner rather than sent.
	static const float s_quad[] =
	{
		-0.5f, -0.5f, 0.0f,
		 0.5f, -0.5f, 0.0f,
		 0.5f,  0.5f, 0.0f,

		-0.5f, -0.5f, 0.0f,
		 0.5f,  0.5f, 0.0f,
		-0.5f,  0.5f, 0.0f,
	};

	SpriteManager::SpriteManager()
	{
		startTime = Time::SinceStart();
	};

	SpriteManager::~SpriteManager()
	{ };

	#pragma region Sheet

	void SpriteManager::SetSpriteSize(const Int2& size)
	{
		spriteSize = Int2{ std::max(1, size.x), std::max(1, size.y) };

		Section();
	};

	#pragma endregion

	#pragma region Animation

	void SpriteManager::SetClip(int value)
	{
		if (clip == value)
			return;

		clip = value;

		AdoptLength();
		Restart();
	};

	void SpriteManager::Play(int newClip)
	{
		clip = newClip;

		AdoptLength();
		Restart();
	};

	void SpriteManager::Restart()
	{
		startTime = Time::SinceStart();
	};

	bool SpriteManager::Finished() const
	{
		return !loop
			&& animationSpeed > 0.0f
			&& (Time::SinceStart() - startTime) * animationSpeed >= float(std::max(1, animationLength));
	};

	void SpriteManager::AdoptLength()
	{
		const int measured = MeasuredLengthOf(clip);

		if (measured > 0)
			animationLength = measured;
	};

	#pragma endregion

	#pragma region Sectioning

	int SpriteManager::MeasuredLengthOf(int index) const
	{
		return index >= 0 && index < int(clipLengths.size()) ? clipLengths[size_t(index)] : 0;
	};

	void SpriteManager::SetClipLengths(std::vector<int> lengths)
	{
		clipLengths = std::move(lengths);

		AdoptLength();
	};

	bool SpriteManager::Section()
	{
		if (!sheetPixels.IsReadable() || spriteSize.x <= 0 || spriteSize.y <= 0)
			return false;

		std::vector<int> lengths =
			SpriteSheetScanner::Measure(sheetPixels, spriteSize, emptyAlphaThreshold);

		if (lengths.empty())
			return false;

		clipLengths = std::move(lengths);

		AdoptLength();

		return true;
	};

	#pragma endregion

	#pragma region Draw

	Float2 SpriteManager::WorldSize() const
	{
		const float ppu = std::max(0.0001f, pixelsPerUnit);

		return Float2{
			float(spriteSize.x) / ppu * scale,
			float(spriteSize.y) / ppu * scale };
	};

	void SpriteManager::QuadVector(float out[4]) const
	{
		const Float2 size = WorldSize();

		out[0] = size.x * (0.5f - pivot.x);
		out[1] = size.y * (0.5f - pivot.y);
		out[2] = size.x;
		out[3] = size.y;
	};

	void SpriteManager::OnRender()
	{
		if (m_program == 0)
		{
			// Compiled on the first draw rather than in the constructor: a component can
			// be attached before there is a GL context to compile against, and Loom's
			// Shader caches by path, so every sprite shares the one program.
			try
			{
				static Shader shader(shaderPath);

				m_program = shader.id;
			}
			catch (const std::exception& error)
			{
				std::cerr << "[SpriteManager] " << error.what() << std::endl;

				// Left at zero so the failure is reported once per sprite rather than
				// once per frame for the life of the program.
				m_program = ~0u;

				return;
			};
		};

		if (m_program == ~0u || texture == 0)
			return;

		Renderer* renderer = Renderer::Get();

		if (!renderer)
			return;

		float quad[4];
		QuadVector(quad);

		const float sheetWidth = float(std::max(1, sheetSize.x));
		const float sheetHeight = float(std::max(1, sheetSize.y));

		renderer->SetUniform(m_program, "mvp", glm::make_mat4(viewProjection));
		renderer->SetUniform(m_program, "_Position", position);
		renderer->SetUniform(m_program, "_Quad", glm::make_vec4(quad));
		renderer->SetUniform(m_program, "_SheetSize", glm::vec4(sheetWidth, sheetHeight, 0.0f, 0.0f));
		renderer->SetUniform(m_program, "_CellSize", glm::vec4(float(spriteSize.x), float(spriteSize.y), 0.0f, 0.0f));
		renderer->SetUniform(m_program, "_Anim", glm::vec4(float(clip), float(std::max(1, animationLength)), animationSpeed, 0.0f));
		renderer->SetUniform(m_program, "_Play", glm::vec4(loop ? 1.0f : 0.0f, startTime, 0.0f, 0.0f));
		renderer->SetUniform(m_program, "_Flip", glm::vec4(flipX ? 1.0f : 0.0f, flipY ? 1.0f : 0.0f, 0.0f, 0.0f));
		renderer->SetUniform(m_program, "_Spin", glm::radians(spin));
		renderer->SetUniform(m_program, "_Color", glm::make_vec4(color));
		renderer->SetUniform(m_program, "_Outline", glm::make_vec4(outline));
		renderer->SetUniform(m_program, "_OutlineWidth", std::max(0.0f, outlineWidth));
		renderer->SetUniform(m_program, "_Time", Time::SinceStart());
		renderer->SetTexture(m_program, "_MainTex", texture);

		constexpr size_t QUAD_VERTICES = 6;
		renderer->Draw(m_program, GL_TRIANGLES, s_quad, QUAD_VERTICES, GL_DYNAMIC_DRAW);
	};

	void SpriteManager::OnGui()
	{
		ImGui::Text("Sheet: %u (%d x %d)", texture, sheetSize.x, sheetSize.y);
		ImGui::Text("Cell:  %d x %d", spriteSize.x, spriteSize.y);

		int editedClip = clip;

		if (ImGui::InputInt("Clip", &editedClip))
			SetClip(std::max(0, editedClip));

		ImGui::InputInt("Length", &animationLength);
		ImGui::DragFloat("Speed", &animationSpeed, 0.5f, 0.0f, 120.0f);
		ImGui::Checkbox("Loop", &loop);

		ImGui::Checkbox("Flip X", &flipX);
		ImGui::SameLine();
		ImGui::Checkbox("Flip Y", &flipY);

		ImGui::DragFloat("Spin", &spin, 1.0f);
		ImGui::DragFloat("Scale", &scale, 0.05f, 0.0001f, 100.0f);
		AxisGui::DragFloatN("Pivot", &pivot.x, 2, 0.01f);
		AxisGui::DragFloatN("Position", glm::value_ptr(position), 3, 0.05f);

		ImGui::ColorEdit4("Color", color);
		ImGui::ColorEdit4("Outline", outline);
		ImGui::DragFloat("Outline Width", &outlineWidth, 0.1f, 0.0f, 32.0f);

		if (ImGui::Button("Section"))
			if (!Section())
				std::cerr << "[SpriteManager] Nothing to measure -- no sheet pixels." << std::endl;

		ImGui::SameLine();

		if (ImGui::Button("Restart"))
			Restart();

		if (!clipLengths.empty() && ImGui::TreeNode("Clip lengths"))
		{
			for (size_t i = 0; i < clipLengths.size(); i++)
				ImGui::Text("%zu: %d", i, clipLengths[i]);

			ImGui::TreePop();
		};
	};

	#pragma endregion
};
