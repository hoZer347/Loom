#include "GameObject.h"

#include "ComponentRegistry.h"
#include "Engine.h"
#include "Scene.h"

#include "imgui.h"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/euler_angles.hpp"

#include <algorithm>
#include <thread>
#include <string>


namespace Loom
{
	glm::vec3 Transform::EulerDegrees(const glm::quat& rotation)
	{
		// Matrix turns about Z, then X, then Y, which is glm's YXZ.
		float y = 0.0f, x = 0.0f, z = 0.0f;
		glm::extractEulerAngleYXZ(glm::mat4_cast(glm::normalize(rotation)), y, x, z);

		return glm::degrees(glm::vec3(x, y, z));
	};

	glm::quat Transform::Rotation(const glm::vec3& degrees)
	{
		const glm::vec3 radians = glm::radians(degrees);

		return glm::quat_cast(glm::eulerAngleYXZ(radians.y, radians.x, radians.z));
	};

	GameObject::GameObject(
		GameObject* parent,
		const std::string& name,
		const int& thread_id) :
		parent(parent)
	{
		SetName(name);

		newName[0] = '\0';

		m_threadID = thread_id;

		size_t i = num_objects.fetch_add(1);
	};

	GameObject::~GameObject()
	{
		Task task =
			[this]()
			{
				for (auto& child : m_children)
					delete child;

				for (auto& component : m_components)
					delete component;

				size_t i = num_objects.fetch_add(-1) - 1;
			};

		if (Engine::isRunning) Engine::QueueTask(task);
		else task();
	};

	void GameObject::DetachComponent(ComponentBase* component)
	{
		if (component == nullptr)
			return;

		Engine::QueueTask(
			[this, component]()
			{
				if (std::find(
					m_components.begin(),
					m_components.end(),
					component) == m_components.end())
					return;

				for (std::vector<ComponentBase*>* list :
					{ &m_components, &m_updateables, &m_renderables, &m_physicsables })
					list->erase(
						std::remove(
							list->begin(),
							list->end(),
							component),
						list->end());

				component->OnDetach();

				delete component;
				hierarchy_changes++;
				Touch();
			});
	};

	void GameObject::RemoveChild(GameObject* child)
	{
		Engine::QueueTask(
			[this, child]()
			{
				const size_t at = child->GetSiblingIndex();

				if (at < m_children.size())
				{
					if (at > 0)
						m_children[at - 1]->Touch(true);

					m_children.erase(m_children.begin() + at);
				};

				delete child;
				hierarchy_changes++;
			});
	};

	void GameObject::SetThreadID(int thread_id)
	{
		this->m_threadID = thread_id;
		for (auto& child : m_children)
			if (child->m_inherit_thread_id)
				child->SetThreadID(thread_id);
	};

	void GameObject::SetInheritThreadID(bool inherit)
	{
		m_inherit_thread_id = inherit;

		if (inherit && parent)
			SetThreadID(parent->m_threadID);
	};

	void GameObject::Destroy()
	{
		if (parent == nullptr)
		{
			std::cerr << "Tried to destroy root GameObject" << std::endl;
			return;
		};

		Engine::QueueTask(
			[this]()
			{
				parent->RemoveChild(this);
			});
	};

	GameObject* GameObject::AddChild(const std::string& name)
	{
		GameObject* gameObject = new GameObject(this, name, m_threadID);

		Engine::QueueTask(
			[gameObject, this]()
			{
				if (!m_children.empty())
					m_children.back()->Touch(true);

				gameObject->m_siblingAt = m_children.size();
				m_children.emplace_back(gameObject);
				hierarchy_changes++;
				gameObject->Touch(true);
			});

		return gameObject;
	};

	void GameObject::SetParent(GameObject* new_parent, const GameObject* before)
	{
		Engine::QueueTask(
			[this, new_parent, before]()
			{
				if (parent == nullptr || new_parent == nullptr || before == this)
					return;

				for (const GameObject* above = new_parent; above != nullptr; above = above->parent)
					if (above == this)
						return;

				// Each place's sibling before it gets another after it.
				const size_t from = GetSiblingIndex();

				if (from < parent->m_children.size())
				{
					if (from > 0)
						parent->m_children[from - 1]->Touch(true);

					parent->m_children.erase(parent->m_children.begin() + from);
				};

				parent = new_parent;

				const size_t to = before && before->parent == parent ? before->GetSiblingIndex() : parent->m_children.size();

				if (to > 0)
					parent->m_children[to - 1]->Touch(true);

				m_siblingAt = to;
				parent->m_children.insert(parent->m_children.begin() + to, this);

				hierarchy_changes++;
				Touch(true);
			});
	};

	size_t GameObject::GetSiblingIndex() const
	{
		if (parent == nullptr)
			return 0;

		const std::vector<GameObject*>& siblings = parent->m_children;
		const size_t count = siblings.size();
		const size_t from = std::min(m_siblingAt, count == 0 ? 0 : count - 1);

		for (size_t distance = 0; distance < count; distance++)
		{
			if (from + distance < count && siblings[from + distance] == this)
				return m_siblingAt = from + distance;

			if (distance <= from && siblings[from - distance] == this)
				return m_siblingAt = from - distance;
		};

		return count;
	};

	void GameObject::OrderComponents(const std::vector<ComponentBase*>& order)
	{
		auto placed = m_components.begin();

		for (ComponentBase* component : order)
		{
			const auto found = std::find(placed, m_components.end(), component);

			if (found == m_components.end())
				continue;

			std::rotate(placed, found, found + 1);
			placed++;
		};

		hierarchy_changes++;
		Touch();
	};

	glm::mat4 GameObject::LocalMatrix() const
	{
		return transform.Matrix();
	};

	glm::mat4 GameObject::WorldMatrix() const
	{
		if (!parent)
			return LocalMatrix();

		// Under a parent that changes nothing, such as the scene's root.
		const glm::mat4 above = parent->WorldMatrix();

		return above == glm::mat4(1.0f) ? LocalMatrix() : above * LocalMatrix();
	};

	void GameObject::Gather(std::vector<std::vector<ComponentBase*>>& byLane, size_t lanes)
	{
		if (m_disabled)
			return;

		if (m_threadID > Unprocessed && !m_updateables.empty())
		{
			const size_t lane = m_threadID == 0 ? 0 : ((size_t)m_threadID - 1) % lanes + 1;

			if (lane >= byLane.size())
				byLane.resize(lane + 1);

			std::vector<ComponentBase*>& updates = byLane[lane];
			updates.insert(updates.end(), m_updateables.begin(), m_updateables.end());
		};

		for (GameObject* child : m_children)
			child->Gather(byLane, lanes);
	};

	void GameObject::Render()
	{
		if (m_disabled)
			return;

		if (m_threadID != Unprocessed)
			for (auto& renderable : m_renderables)
				renderable->OnRender();

		for (auto& child : m_children)
			child->Render();
	};

	void GameObject::Physics()
	{
		if (m_disabled)
			return;

		for (auto& physicsable : m_physicsables)
			physicsable->OnPhysics();
		for (auto& child : m_children)
			child->Physics();
	};

	void GameObject::Gui()
	{
		ImGui::PushID(this);

		if (ImGui::TreeNode((void*)this, "%s", m_name.c_str()))
		{
			ImGui::SameLine();
			if (ImGui::Button("Add Child"))
				AddChild();

			if (parent)
			{
				ImGui::SameLine();
				if (ImGui::Button("Delete"))
					Destroy();
			};

			ImGui::SameLine();

			if (!m_inherit_thread_id)
			{
				ImGui::PushItemWidth(200);
				ImGui::Text("Thread ID (-1 is not processed): ");
				ImGui::SameLine();
				if (ImGui::SliderInt(" ", &*m_threadID, -1, std::thread::hardware_concurrency()))
					SetThreadID(m_threadID);
				ImGui::PopItemWidth();
				ImGui::SameLine();
			}
			else ImGui::Text("Thread ID: %i", *m_threadID);

			ImGui::SameLine();

			if (ImGui::Checkbox("Inherit Host Thread", &*m_inherit_thread_id))
				SetInheritThreadID(m_inherit_thread_id);

			ImGui::Text("Name: ");
			ImGui::SameLine();

			if (ImGui::InputText(std::to_string(m_ID).c_str(), newName, 128))
				m_name = newName;

			// Adding components
			if (ImGui::TreeNode("Add Component: "))
			{
				for (auto& [name, factory] : ComponentRegistry::All())
					if (ImGui::Button(name.c_str()))
						factory(*this);

				ImGui::TreePop();
			};

			// Removing components, by instance rather than by type: a GameObject
			// is free to carry two of the same kind.
			if (ImGui::TreeNode("Remove Component: "))
			{
				for (ComponentBase* component : m_components)
				{
					ImGui::PushID(component);

					if (ImGui::Button(ComponentRegistry::NameOf(*component).c_str()))
						DetachComponent(component);

					ImGui::PopID();
				};

				ImGui::TreePop();
			};

			// A copy: a component's GUI can attach another one, and Attach now puts
			// it straight into m_components rather than on the queue.
			const std::vector<ComponentBase*> components = m_components;

			for (ComponentBase* component : components)
				component->Gui();
			for (GameObject* child : m_children)
				child->Gui();

			ImGui::TreePop();
		};

		ImGui::PopID();
	};
};
