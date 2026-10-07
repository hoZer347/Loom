#include "GameObject.h"

#include "ComponentRegistry.h"
#include "Engine.h"
#include "RenderMath.h"
#include "Scene.h"

#include "glm/gtc/matrix_transform.hpp"

#include "imgui.h"

#include <algorithm>
#include <thread>
#include <string>


namespace Loom
{
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
			});
	};

	void GameObject::RemoveChild(GameObject* child)
	{
		Engine::QueueTask(
			[this, child]()
			{
				m_children.erase(
					std::remove(
						m_children.begin(),
						m_children.end(),
						child));

				delete child;
			});
	};

	void GameObject::SetThreadID(int thread_id)
	{
		this->m_threadID = thread_id;
		for (auto& child : m_children)
			if (child->m_inherit_thread_id)
				child->SetThreadID(thread_id);
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
				m_children.emplace_back(gameObject);
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

				std::erase(parent->m_children, this);

				parent = new_parent;

				parent->m_children.insert(
					std::find(
						parent->m_children.begin(),
						parent->m_children.end(),
						before),
					this);
			});
	};

	glm::mat4 GameObject::LocalMatrix() const
	{
		const glm::vec3 degrees = ToGlm(rotation);

		glm::mat4 matrix = glm::translate(glm::mat4(1.0f), ToGlm(position));
		matrix = glm::rotate(matrix, glm::radians(degrees.y), Y_AXIS);
		matrix = glm::rotate(matrix, glm::radians(degrees.x), X_AXIS);
		matrix = glm::rotate(matrix, glm::radians(degrees.z), Z_AXIS);

		return glm::scale(matrix, ToGlm(scale));
	};

	glm::mat4 GameObject::WorldMatrix() const
	{
		return parent
			? parent->WorldMatrix() * LocalMatrix()
			: LocalMatrix();
	};

	void GameObject::Update(const int& thread)
	{
		for (ComponentBase* updateable : m_updateables)
			//if (thread == m_threadID)
				updateable->OnUpdate();
		for (GameObject* child : m_children)
			child->Update(thread);
	};

	void GameObject::Render()
	{
		for (auto& renderable : m_renderables)
			//if (m_threadID != -1)
				renderable->OnRender();
		for (auto& child : m_children)
			child->Render();
	};

	void GameObject::Physics()
	{
		for (auto& renderable : m_physicsables)
			if (m_threadID != -1)
				renderable->OnPhysics();
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
				if (m_inherit_thread_id)
					SetThreadID(m_threadID);

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
