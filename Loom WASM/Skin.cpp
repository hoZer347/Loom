#include "Skin.h"

#include "GameObject.h"
#include "Mesh.h"

#include "glm/gtc/type_ptr.hpp"

#include <algorithm>
#include <sstream>


namespace Loom
{
	namespace
	{
		constexpr size_t POSITION_COMPONENTS = 3;

		void CollectByName(GameObject& object, std::unordered_map<std::string, GameObject*>& found)
		{
			found.try_emplace(object.GetName(), &object);

			for (GameObject* child : object.GetChildren())
				CollectByName(*child, found);
		};
	};

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

	Skin::Skin()
	{ };

	std::vector<size_t> Skin::ShapeStarts() const
	{
		const std::vector<float>& offsets = *m_shapeOffsets;
		std::vector<size_t> starts;

		for (size_t at = 0; at < offsets.size(); at += 1 + (size_t)offsets[at] * FLOATS_PER_OFFSET)
			starts.push_back(at);

		return starts;
	};

	std::vector<glm::mat4> Skin::JointMatrices(const Mesh& mesh)
	{
		const std::vector<std::string> names = SplitLines(m_joints);
		GameObject* root = skeleton;

		if (root == nullptr || names.empty() || m_inverseBinds->size() != names.size() * MATRIX_FLOATS)
			return { };

		const std::unordered_map<std::string, GameObject*> joints = DescendantsByName(*root);
		const glm::mat4 toMesh = glm::inverse(mesh.GetGameObject()->WorldMatrix());

		std::vector<glm::mat4> matrices(names.size(), glm::mat4(1.0f));

		for (size_t i = 0; i < names.size(); i++)
		{
			const auto found = joints.find(names[i]);

			if (found != joints.end())
				matrices[i] = toMesh * found->second->WorldMatrix() * glm::make_mat4(m_inverseBinds->data() + i * MATRIX_FLOATS);
		};

		return matrices;
	};

	void Skin::Deform(Mesh& mesh)
	{
		std::vector<float>& bent = mesh.m_deformed;
		bent = *mesh.m_vertices;

		const std::vector<float>& offsets = *m_shapeOffsets;
		const std::vector<size_t> starts = ShapeStarts();

		for (size_t shape = 0; shape < starts.size() && shape < m_shapeWeights->size(); shape++)
		{
			const float weight = (*m_shapeWeights)[shape];

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

		const std::vector<glm::mat4> matrices = JointMatrices(mesh);
		const size_t vertices = bent.size() / POSITION_COMPONENTS;

		if (matrices.empty() || m_influences->size() != vertices * FLOATS_PER_INFLUENCE)
			return;

		for (size_t v = 0; v < vertices; v++)
		{
			const float* influence = m_influences->data() + v * FLOATS_PER_INFLUENCE;

			glm::mat4 blended(0.0f);
			float total = 0.0f;

			for (size_t k = 0; k < JOINTS_PER_VERTEX; k++)
			{
				const size_t joint = (size_t)influence[k];
				const float weight = influence[JOINTS_PER_VERTEX + k];

				if (weight <= 0.0f || joint >= matrices.size())
					continue;

				blended += matrices[joint] * weight;
				total += weight;
			};

			if (total <= 0.0f)
				continue;

			float* position = bent.data() + v * POSITION_COMPONENTS;
			const glm::vec3 moved = glm::vec3(blended * glm::vec4(position[0], position[1], position[2], 1.0f)) / total;

			std::copy_n(glm::value_ptr(moved), POSITION_COMPONENTS, position);
		};
	};

	void Skin::SetShapeWeight(const std::string& name, float weight)
	{
		const std::vector<std::string> names = SplitLines(m_shapes);
		const auto found = std::find(names.begin(), names.end(), name);

		if (found == names.end())
			return;

		m_shapeWeights->resize(names.size());
		(*m_shapeWeights)[found - names.begin()] = weight;
	};

	float Skin::GetShapeWeight(const std::string& name) const
	{
		const std::vector<std::string> names = SplitLines(m_shapes);
		const size_t index = std::find(names.begin(), names.end(), name) - names.begin();

		return index < m_shapeWeights->size() ? (*m_shapeWeights)[index] : 0.0f;
	};

	std::vector<std::string> Skin::GetShapeNames() const
	{
		return SplitLines(m_shapes);
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
};
