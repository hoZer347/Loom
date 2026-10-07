#include "SpriteManager.h"

#include "Clock.h"

#include "Engine.h"
#include "Shaders.h"

#include "OpenGL.h"

#include "imgui.h"

#include "glm/gtc/type_ptr.hpp"

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

		glUseProgram(m_program);

		float quad[4];
		QuadVector(quad);

		const float sheetWidth = float(std::max(1, sheetSize.x));
		const float sheetHeight = float(std::max(1, sheetSize.y));

		glUniformMatrix4fv(glGetUniformLocation(m_program, "mvp"), 1, GL_FALSE, viewProjection);
		glUniform3f(glGetUniformLocation(m_program, "_Position"), position.x, position.y, position.z);
		glUniform4f(glGetUniformLocation(m_program, "_Quad"), quad[0], quad[1], quad[2], quad[3]);
		glUniform4f(glGetUniformLocation(m_program, "_SheetSize"), sheetWidth, sheetHeight, 0.0f, 0.0f);
		glUniform4f(glGetUniformLocation(m_program, "_CellSize"), float(spriteSize.x), float(spriteSize.y), 0.0f, 0.0f);
		glUniform4f(glGetUniformLocation(m_program, "_Anim"), float(clip), float(std::max(1, animationLength)), animationSpeed, 0.0f);
		glUniform4f(glGetUniformLocation(m_program, "_Play"), loop ? 1.0f : 0.0f, startTime, 0.0f, 0.0f);
		glUniform4f(glGetUniformLocation(m_program, "_Flip"), flipX ? 1.0f : 0.0f, flipY ? 1.0f : 0.0f, 0.0f, 0.0f);
		glUniform1f(glGetUniformLocation(m_program, "_Spin"), spin * 0.01745329252f);
		glUniform4fv(glGetUniformLocation(m_program, "_Color"), 1, color);
		glUniform4fv(glGetUniformLocation(m_program, "_Outline"), 1, outline);
		glUniform1f(glGetUniformLocation(m_program, "_OutlineWidth"), std::max(0.0f, outlineWidth));
		glUniform1f(glGetUniformLocation(m_program, "_Time"), Time::SinceStart());

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, texture);
		glUniform1i(glGetUniformLocation(m_program, "_MainTex"), 0);

		glBindBuffer(GL_ARRAY_BUFFER, Engine::VBO);
		glBufferData(GL_ARRAY_BUFFER, sizeof(s_quad), s_quad, GL_DYNAMIC_DRAW);

		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
		glEnableVertexAttribArray(0);

		glDrawArrays(GL_TRIANGLES, 0, 6);

		glDisableVertexAttribArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
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
		ImGui::DragFloat2("Pivot", &pivot.x, 0.01f);
		ImGui::DragFloat3("Position", glm::value_ptr(position), 0.05f);

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
