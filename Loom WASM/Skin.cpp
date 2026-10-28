#include "Skin.h"

#include "GameObject.h"
#include "Jobs.h"
#include "Mesh.h"
#include "Profiler.h"

#include "glm/gtc/type_ptr.hpp"

#include <algorithm>
#include <bit>
#include <sstream>
#include <utility>


namespace Loom
{
	namespace
	{
		constexpr size_t POSITION_COMPONENTS = 3;

		// Enough vertices a job that handing them out costs little beside bending them.
		constexpr size_t VERTEX_GRAIN = 256;

		void CollectByName(GameObject& object, std::unordered_map<std::string, GameObject*>& found)
		{
			found.try_emplace(object.GetName(), &object);

			for (GameObject* child : object.GetChildren())
				CollectByName(*child, found);
		};
	};

	#pragma region Names

	std::unordered_map<std::string, GameObject*> DescendantsByName(GameObject& root)
	{
		std::unordered_map<std::string, GameObject*> found;
		CollectByName(root, found);

		return found;
	};

	std::vector<std::string> SplitLines(const std::string& text)
	{
		std::vector<std::string> lines;
		std::istringstream stream(text);

		for (std::string line; std::getline(stream, line);)
			lines.push_back(line);

		return lines;
	};

	const std::vector<GameObject*>& NamedObjects::Find(GameObject* root, const std::vector<std::string>& names)
	{
		bool gone = false;

		for (size_t i = 0; i < m_guids.size() && !gone; i++)
			gone = m_guids[i].IsValid() && LoomObject::GetByGuid<GameObject>(m_guids[i]) != m_found[i];

		if (root == m_root && names == m_names && !gone && GameObject::HierarchyChanges() == m_changes)
			return m_found;

		m_changes = GameObject::HierarchyChanges();
		m_root = root;
		m_names = names;
		m_guids.assign(names.size(), Guid());
		m_found.assign(names.size(), nullptr);

		if (root == nullptr)
			return m_found;

		const std::unordered_map<std::string, GameObject*> under = DescendantsByName(*root);

		for (size_t i = 0; i < names.size(); i++)
			if (const auto found = under.find(names[i]); found != under.end())
			{
				m_found[i] = found->second;
				m_guids[i] = found->second->GetGuid();
			};

		return m_found;
	};

	const std::vector<std::string>& NameList::Of(const std::string& text)
	{
		if (text != m_text)
		{
			m_text = text;
			m_names = SplitLines(text);
		};

		return m_names;
	};

	#pragma endregion

	#pragma region Skin

	Skin::Skin()
	{ };

	std::vector<size_t> Skin::ShapeStarts() const
	{
		const std::vector<float>& offsets = *std::as_const(m_shapeOffsets);
		std::vector<size_t> starts;

		for (size_t at = 0; at < offsets.size(); )
		{
			const float count = offsets[at];

			// Not a count, or more offsets than follow it, ends the shapes.
			if (!(count >= 0.0f && count <= (float)offsets.size()) || (size_t)count > (offsets.size() - at - 1) / FLOATS_PER_OFFSET)
				break;

			starts.push_back(at);
			at += 1 + (size_t)count * FLOATS_PER_OFFSET;
		};

		return starts;
	};

	std::vector<glm::mat4> Skin::JointMatrices(const Mesh& mesh)
	{
		const std::vector<std::string>& names = m_jointNames.Of(m_joints);

		const std::vector<float>& inverseBinds = *std::as_const(m_inverseBinds);

		if (skeleton == nullptr || names.empty() || inverseBinds.size() != names.size() * MATRIX_FLOATS)
			return { };

		const std::vector<GameObject*>& joints = m_jointObjects.Find(skeleton, names);
		const glm::mat4 toMesh = glm::inverse(mesh.GetGameObject()->WorldMatrix());

		std::vector<glm::mat4> matrices(names.size(), glm::mat4(1.0f));

		for (size_t i = 0; i < names.size(); i++)
			if (joints[i])
				matrices[i] = toMesh * joints[i]->WorldMatrix() * glm::make_mat4(inverseBinds.data() + i * MATRIX_FLOATS);

		return matrices;
	};

	bool Skin::Deform(Mesh& mesh)
	{
		return Bend(mesh, JointMatrices(mesh));
	};

	bool Skin::Shape(Mesh& mesh)
	{
		return Bend(mesh, { });
	};

	bool Skin::FitsGpu(const Mesh& mesh)
	{
		const size_t joints = m_jointNames.Of(m_joints).size();
		const size_t vertices = std::as_const(mesh.m_vertices)->size() / POSITION_COMPONENTS;

		return skeleton != nullptr &&
			joints > 0 &&
			joints <= MAX_GPU_JOINTS &&
			std::as_const(m_inverseBinds)->size() == joints * MATRIX_FLOATS &&
			std::as_const(m_influences)->size() == vertices * FLOATS_PER_INFLUENCE;
	};

	std::vector<uint32_t> Skin::GpuInfluences()
	{
		const std::vector<float>& influences = *std::as_const(m_influences);
		const size_t joints = m_jointNames.Of(m_joints).size();

		std::vector<uint32_t> words(influences.size());

		for (size_t at = 0; at + FLOATS_PER_INFLUENCE <= influences.size(); at += FLOATS_PER_INFLUENCE)
			for (size_t k = 0; k < JOINTS_PER_VERTEX; k++)
			{
				const float joint = influences[at + k];
				const float weight = influences[at + JOINTS_PER_VERTEX + k];

				if (weight <= 0.0f || !(joint >= 0.0f && joint < (float)joints))
					continue;

				words[at + k] = (uint32_t)joint;
				words[at + JOINTS_PER_VERTEX + k] = std::bit_cast<uint32_t>(weight);
			};

		return words;
	};

	bool Skin::Bend(Mesh& mesh, std::vector<glm::mat4> matrices)
	{
		LOOM_STAGE("Skin::Deform");

		// Read through const, so bending them does not count as changing them.
		const std::vector<float>& vertices = *std::as_const(mesh.m_vertices);
		const std::vector<float>& normals = *std::as_const(mesh.m_normals);
		const std::vector<float>& influences = *std::as_const(m_influences);
		const std::vector<float>& offsets = *std::as_const(m_shapeOffsets);
		const std::vector<float>& weights = *std::as_const(m_shapeWeights);

		Pose pose{
			&mesh,
			mesh.m_vertices.Version(),
			mesh.m_normals.Version(),
			m_influences.Version(),
			m_shapeOffsets.Version(),
			weights,
			std::move(matrices) };

		if (pose == m_bent && mesh.m_deformed.size() == vertices.size())
			return false;

		m_bent = std::move(pose);

		std::vector<float>& bent = mesh.m_deformed;
		bent = vertices;

		std::vector<float>& turned = mesh.m_deformedNormals;

		if (normals.size() == bent.size())
			turned = normals;
		else turned.clear();

		const std::vector<size_t> starts = ShapeStarts();

		for (size_t shape = 0; shape < starts.size() && shape < weights.size(); shape++)
		{
			const float weight = weights[shape];

			if (weight == 0.0f)
				continue;

			const size_t count = (size_t)offsets[starts[shape]];
			const float* offset = offsets.data() + starts[shape] + 1;

			for (size_t i = 0; i < count; i++, offset += FLOATS_PER_OFFSET)
			{
				const size_t vertex = (size_t)offset[0] * POSITION_COMPONENTS;

				if (vertex + POSITION_COMPONENTS > bent.size())
					continue;

				for (size_t axis = 0; axis < POSITION_COMPONENTS; axis++)
					bent[vertex + axis] += weight * offset[1 + axis];
			};
		};

		const std::vector<glm::mat4>& joints = m_bent.matrices;
		const size_t count = bent.size() / POSITION_COMPONENTS;

		if (joints.empty() || influences.size() != count * FLOATS_PER_INFLUENCE)
			return true;

		Jobs::For(count, VERTEX_GRAIN,
			[&](size_t, size_t begin, size_t end)
			{
				for (size_t v = begin; v < end; v++)
				{
					const float* influence = influences.data() + v * FLOATS_PER_INFLUENCE;

					glm::mat4 blended(0.0f);
					float total = 0.0f;

					for (size_t k = 0; k < JOINTS_PER_VERTEX; k++)
					{
						const size_t joint = (size_t)influence[k];
						const float weight = influence[JOINTS_PER_VERTEX + k];

						if (weight <= 0.0f || joint >= joints.size())
							continue;

						blended += joints[joint] * weight;
						total += weight;
					};

					if (total <= 0.0f)
						continue;

					float* position = bent.data() + v * POSITION_COMPONENTS;
					const glm::vec3 moved = glm::vec3(blended * glm::vec4(position[0], position[1], position[2], 1.0f)) / total;

					std::copy_n(glm::value_ptr(moved), POSITION_COMPONENTS, position);

					if (turned.empty())
						continue;

					// Assumes even joint scale; uneven stretch leaves normals off square.
					float* normal = turned.data() + v * POSITION_COMPONENTS;
					const glm::vec3 facing = glm::normalize(glm::mat3(blended) * glm::make_vec3(normal));

					std::copy_n(glm::value_ptr(facing), POSITION_COMPONENTS, normal);
				};
			});

		return true;
	};

	void Skin::SetShapeWeight(const std::string& name, float weight)
	{
		const std::vector<std::string>& names = m_shapeNames.Of(m_shapes);
		const auto found = std::find(names.begin(), names.end(), name);

		if (found == names.end())
			return;

		m_shapeWeights->resize(names.size());
		(*m_shapeWeights)[found - names.begin()] = weight;
	};

	float Skin::GetShapeWeight(const std::string& name)
	{
		const std::vector<std::string>& names = m_shapeNames.Of(m_shapes);
		const size_t index = std::find(names.begin(), names.end(), name) - names.begin();

		return index < m_shapeWeights->size() ? (*m_shapeWeights)[index] : 0.0f;
	};

	std::vector<std::string> Skin::GetShapeNames()
	{
		return m_shapeNames.Of(m_shapes);
	};

	void Skin::OnGui()
	{
		const std::vector<std::string> names = GetShapeNames();

		if (names.empty())
			return;

		ImGui::SeparatorText("Shape Keys");
		m_shapeWeights->resize(names.size());

		for (size_t i = 0; i < names.size(); i++)
			ImGui::SliderFloat(names[i].c_str(), &(*m_shapeWeights)[i], 0.0f, 1.0f);
	};

	#pragma endregion
};
