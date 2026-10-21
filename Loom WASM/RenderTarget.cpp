#include "RenderTarget.h"

#include "Light.h"
#include "Material.h"
#include "Mesh.h"
#include "RenderMath.h"
#include "Shaders.h"

#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"

#include <algorithm>


namespace Loom
{
	namespace
	{
		// Every target attached, which is every one a material can name.
		std::vector<RenderTarget*>& Attached()
		{
			static std::vector<RenderTarget*> attached{ };

			return attached;
		};
	};

	RenderTarget::~RenderTarget()
	{
		std::erase(Attached(), this);
		Release();
	};

	void RenderTarget::OnAttach()
	{
		Attached().push_back(this);
	};

	void RenderTarget::OnGui()
	{
		Renderer* renderer = Renderer::Get();

		if (!renderer || !Texture())
			return;

		void* texture = renderer->ImGuiTexture(m_texture);

		if (!texture)
			return;

		// As wide as the panel at most, and no taller than that.
		const float scale = ImGui::GetContentRegionAvail().x / std::max(m_made.width, m_made.height);

		// Bottom row first, so V is flipped.
		ImGui::Image(texture, ImVec2(m_made.width * scale, m_made.height * scale), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
	};

	void RenderTarget::Clear()
	{
		Renderer* renderer = Renderer::Get();

		if (!renderer || !Allocate())
			return;

		renderer->PushTarget(m_texture);
		renderer->Clear(glm::value_ptr(*clearColor), m_made.depth);
		renderer->PopTarget();
	};

	void RenderTarget::Draw(Mesh& mesh, const glm::mat4& viewProjection, Blend blend)
	{
		if (!mesh.IsDrawable())
			return;

		Material* own = mesh.material;
		const glm::mat4 model = mesh.GetGameObject()->WorldMatrix();

		DrawEach(mesh, *own, &model, 1, viewProjection, blend);
	};

	void RenderTarget::Draw(Mesh& mesh, const std::vector<glm::mat4>& models, const glm::mat4& viewProjection, Blend blend)
	{
		if (!mesh.IsDrawable())
			return;

		Material* own = mesh.material;

		DrawEach(mesh, *own, models.data(), models.size(), viewProjection, blend);
	};

	void RenderTarget::Draw(Mesh& mesh, Material& material, const std::vector<glm::mat4>& models, const glm::mat4& viewProjection, Blend blend)
	{
		DrawEach(mesh, material, models.data(), models.size(), viewProjection, blend);
	};

	void RenderTarget::DrawEach(Mesh& mesh, Material& material, const glm::mat4* models, size_t count, const glm::mat4& viewProjection, Blend blend)
	{
		Renderer* renderer = Renderer::Get();

		material.PickUpShader();

		if (!renderer || !material.shader || count == 0 || !Allocate())
			return;

		const uint32_t program = material.shader->id;

		renderer->PushTarget(m_texture);
		renderer->SetBlend(blend);
		m_drawing = true;

		material.Apply(program, &*mesh.variables);
		renderer->SetUniform(program, "u_viewProjection", viewProjection);
		Light::ApplyNone(program);

		for (size_t i = 0; i < count; i++)
			mesh.Draw(program, models[i]);

		m_drawing = false;
		renderer->SetBlend(Blend::Alpha);
		renderer->PopTarget();
	};

	uint32_t RenderTarget::Texture()
	{
		return Allocate() ? m_texture : 0;
	};

	RenderTarget* RenderTarget::Named(const std::string& name)
	{
		for (RenderTarget* target : Attached())
			if (target->m_gameObject->GetName() == name)
				return target;

		return nullptr;
	};

	glm::mat4 RenderTarget::Overhead(const glm::vec2& min, const glm::vec2& max)
	{
		// Looking down, with -Z up the view: a turn, not a mirror, so faces
		// keep the winding they have seen from above.
		const glm::mat4 fromAbove = glm::lookAt(glm::vec3(0.0f), -Y_AXIS, -Z_AXIS);

		return glm::ortho(min.x, max.x, -max.y, -min.y, -overheadHeight, overheadHeight) * fromAbove;
	};

	bool RenderTarget::Allocate()
	{
		Renderer* renderer = Renderer::Get();

		if (!renderer)
			return false;

		const int largest = std::max(1, renderer->MaxTextureSize());
		const Made wanted = { std::clamp(*width, 1, largest), std::clamp(*height, 1, largest), *depth, *pixelated };

		if (wanted == m_made)
			return m_texture != 0;

		Release();

		m_made = wanted;
		m_texture = renderer->CreateColorTarget(wanted.width, wanted.height, wanted.depth, wanted.pixelated);

		if (!m_texture)
			return false;

		renderer->PushTarget(m_texture);
		renderer->Clear(glm::value_ptr(*clearColor), wanted.depth);
		renderer->PopTarget();

		return true;
	};

	void RenderTarget::Release()
	{
		Renderer* renderer = Renderer::Get();

		if (renderer && m_texture)
			renderer->DestroyTexture(m_texture);

		m_texture = 0;
	};
};
