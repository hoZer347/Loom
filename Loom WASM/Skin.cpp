#include "Skin.h"

#include "GameObject.h"
#include "Mesh.h"

#include "glm/gtc/matrix_transform.hpp"
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

	Skin::Skin()
	{ };

	std::vector<size_t> Skin::ShapeStarts() const
	{
		const std::vector<float>& offsets = *m_shapeOffsets;
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

		if (skeleton == nullptr || names.empty() || m_inverseBinds->size() != names.size() * MATRIX_FLOATS)
			return { };

		const std::vector<GameObject*>& joints = m_jointObjects.Find(skeleton, names);
		const glm::mat4 toMesh = glm::inverse(mesh.GetGameObject()->WorldMatrix());

		std::vector<glm::mat4> matrices(names.size(), glm::mat4(1.0f));

		for (size_t i = 0; i < names.size(); i++)
			if (joints[i])
				matrices[i] = toMesh * joints[i]->WorldMatrix() * glm::make_mat4(m_inverseBinds->data() + i * MATRIX_FLOATS);

		return matrices;
	};

	void Skin::Deform(Mesh& mesh)
	{
		std::vector<float>& bent = mesh.m_deformed;
		bent = *mesh.m_vertices;

		std::vector<float>& turned = mesh.m_deformedNormals;

		if (mesh.m_normals->size() == bent.size())
			turned = *mesh.m_normals;
		else turned.clear();

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
			const std::optional<glm::mat4> blended = Blend(m_influences->data() + v * FLOATS_PER_INFLUENCE, matrices);

			if (!blended)
				continue;

			float* position = bent.data() + v * POSITION_COMPONENTS;
			const glm::vec3 moved = glm::vec3(*blended * glm::vec4(position[0], position[1], position[2], 1.0f));

			std::copy_n(glm::value_ptr(moved), POSITION_COMPONENTS, position);

			if (turned.empty())
				continue;

			// Assumes the joints scale evenly, if at all, so the blend turns a
			// normal as it does a position and normalizing undoes the scale. A
			// joint stretched unevenly leaves its normals off square to the
			// surface.
			float* normal = turned.data() + v * POSITION_COMPONENTS;
			const glm::vec3 facing = glm::normalize(glm::mat3(*blended) * glm::make_vec3(normal));

			std::copy_n(glm::value_ptr(facing), POSITION_COMPONENTS, normal);
		};
	};

	std::optional<glm::mat4> Skin::Blend(const float* influence, const std::vector<glm::mat4>& matrices)
	{
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
			return std::nullopt;

		return blended / total;
	};

	size_t Skin::BindingFloats() const
	{
		return FLOATS_PER_INFLUENCE + ShapeStarts().size() * POSITION_COMPONENTS;
	};

	std::vector<float> Skin::Bindings(size_t vertices) const
	{
		const size_t stride = BindingFloats();
		std::vector<float> bindings(vertices * stride, 0.0f);

		if (m_influences->size() == vertices * FLOATS_PER_INFLUENCE)
			for (size_t v = 0; v < vertices; v++)
				std::copy_n(m_influences->data() + v * FLOATS_PER_INFLUENCE, FLOATS_PER_INFLUENCE, bindings.data() + v * stride);

		const std::vector<float>& offsets = *m_shapeOffsets;
		const std::vector<size_t> starts = ShapeStarts();

		for (size_t shape = 0; shape < starts.size(); shape++)
		{
			const size_t count = (size_t)offsets[starts[shape]];
			const float* offset = offsets.data() + starts[shape] + 1;

			for (size_t i = 0; i < count; i++, offset += FLOATS_PER_OFFSET)
			{
				const size_t vertex = (size_t)offset[0];

				if (vertex >= vertices)
					continue;

				float* into = bindings.data() + vertex * stride + FLOATS_PER_INFLUENCE + shape * POSITION_COMPONENTS;

				for (size_t axis = 0; axis < POSITION_COMPONENTS; axis++)
					into[axis] += offset[1 + axis];
			};
		};

		return bindings;
	};

	void Skin::SetBindings(const std::vector<float>& bindings)
	{
		const size_t stride = BindingFloats();
		const size_t shapes = ShapeStarts().size();
		const size_t vertices = bindings.size() / stride;

		std::vector<float> influences(vertices * FLOATS_PER_INFLUENCE);
		std::vector<float> offsets{ };

		for (size_t v = 0; v < vertices; v++)
			std::copy_n(bindings.data() + v * stride, FLOATS_PER_INFLUENCE, influences.data() + v * FLOATS_PER_INFLUENCE);

		// Only the vertices a key moves are kept, after the count of them.
		for (size_t shape = 0; shape < shapes; shape++)
		{
			const size_t start = offsets.size();
			offsets.push_back(0.0f);

			for (size_t v = 0; v < vertices; v++)
			{
				const float* offset = bindings.data() + v * stride + FLOATS_PER_INFLUENCE + shape * POSITION_COMPONENTS;

				if (std::all_of(offset, offset + POSITION_COMPONENTS, [](float axis) { return axis == 0.0f; }))
					continue;

				offsets.push_back((float)v);
				offsets.insert(offsets.end(), offset, offset + POSITION_COMPONENTS);
				offsets[start]++;
			};
		};

		*m_influences = std::move(influences);
		*m_shapeOffsets = std::move(offsets);
	};

	std::vector<glm::mat4> Skin::Bends(const Mesh& mesh, const std::vector<float>& bindings)
	{
		const size_t stride = BindingFloats();
		const size_t shapes = ShapeStarts().size();
		const size_t vertices = bindings.size() / stride;
		const std::vector<glm::mat4> matrices = JointMatrices(mesh);

		std::vector<glm::mat4> bends(vertices, glm::mat4(1.0f));

		for (size_t v = 0; v < vertices; v++)
		{
			const float* binding = bindings.data() + v * stride;
			glm::vec3 shift(0.0f);

			for (size_t shape = 0; shape < shapes && shape < m_shapeWeights->size(); shape++)
				shift += (*m_shapeWeights)[shape] * glm::make_vec3(binding + FLOATS_PER_INFLUENCE + shape * POSITION_COMPONENTS);

			const std::optional<glm::mat4> blended = matrices.empty() ? std::nullopt : Blend(binding, matrices);

			bends[v] = blended.value_or(glm::mat4(1.0f)) * glm::translate(glm::mat4(1.0f), shift);
		};

		return bends;
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
};
