#include "GameObject.h"

#include "Engine.h"
#include "Scene.h"

#include "imgui.h"

#include <thread>
#include <string>


namespace Loom
{
	GameObject::GameObject(
		GameObject* parent,
		const std::string& name,
		const int& thread_id) :
		parent(parent),
		m_threadID(thread_id)
	{
		SetName(name);

		newName[0] = '\0';

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

	void GameObject::Update(const int& thread)
	{
		for (ComponentBase* updateable : m_updateables)
			if (thread == m_threadID)
				updateable->OnRender();
		for (GameObject* child : m_children)
			child->Update(thread);
	};

	void GameObject::Render()
	{
		for (auto& renderable : m_renderables)
			if (m_threadID != -1)
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

		if (ImGui::TreeNode((void*)this, m_name.c_str()))
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
				if (ImGui::SliderInt(" ", &m_threadID, -1, std::thread::hardware_concurrency()))
					SetThreadID(m_threadID);
				ImGui::PopItemWidth();
				ImGui::SameLine();
			}
			else ImGui::Text("Thread ID: %i", m_threadID);

			ImGui::SameLine();

			if (ImGui::Checkbox("Inherit Host Thread", &m_inherit_thread_id))
				if (m_inherit_thread_id)
					SetThreadID(m_threadID);

			ImGui::Text("Name: ");
			ImGui::SameLine();

			if (ImGui::InputText(std::to_string(m_ID).c_str(), newName, 128))
				m_name = newName;

			// Adding components
			if (ImGui::TreeNode("Add Component: "))
			{
				for (auto& [name, func] : reg_component_attachers)
				{
					for (auto& component : m_components)
						if (component->GetClassName() == name)
							continue;

					if (ImGui::Button(name.c_str()))
						func(this);
				};

				ImGui::TreePop();
			};

			// TODO: Add a way to detach by component / name rather than by component type

			// Removing components
			if (ImGui::TreeNode("Remove Component: "))
			{
				for (auto& [name, func] : reg_component_detachers)
					if (ImGui::Button(name.c_str()))
					{
						reg_component_detachers[name](this);
						break;
					};

				ImGui::TreePop();
			};

			for (ComponentBase* component : m_components)
				component->Gui();
			for (GameObject* child : m_children)
				child->Gui();

			ImGui::TreePop();
		};

		ImGui::PopID();
	};
};
