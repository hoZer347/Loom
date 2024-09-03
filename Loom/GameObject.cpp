import GameObject;

import Engine;
import Scene;

#include <thread>
#include <string>
#include "imgui.h"


namespace Loom
{
	GameObject::GameObject(
		Engine* engine,
		GameObject* parent,
		const char* name,
		const int& thread_id) :
		engine(engine),
		parent(parent),
		name(name),
		m_thread_id(thread_id)
	{
		newName[0] = '\0';

		size_t i = num_objects.fetch_add(1);
		std::cout << "GameObject Made:	#" << i << std::endl;
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
				std::cout << "GameObject Deleted:	#" << i << std::endl;
			};

		if (engine->isRunning) engine->QueueTask(task);
		else task();
	};

	void GameObject::RemoveChild(GameObject* child)
	{
		engine->QueueTask(
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
		this->m_thread_id = thread_id;
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

		engine->QueueTask(
			[this]()
			{
				parent->RemoveChild(this);
			});
	};

	GameObject* GameObject::AddChild(const char* name = "New GameObject")
	{
		GameObject* gameObject = new GameObject(engine, this, name, m_thread_id);

		engine->QueueTask(
			[gameObject, this]()
			{
				m_children.emplace_back(gameObject);
			});

		return gameObject;
	};

	void GameObject::Update(const int& thread)
	{
		for (ComponentBase* updateable : m_updateables)
			if (thread == m_thread_id)
				updateable->OnUpdate();
		for (GameObject* child : m_children)
			child->Update(thread);
	};

	void GameObject::Render()
	{
		for (auto& renderable : m_renderables)
			renderable->OnRender();
		for (auto& child : m_children)
			child->Render();
	};

	void GameObject::Gui()
	{
		ImGui::PushID(this);

		if (ImGui::TreeNode((void*)this, (std::string(name) + " (" + std::to_string(m_id) + ")").c_str()))
		{
			ImGui::SameLine();
			if (ImGui::Button("Add Child"))
				AddChild("New GameObject");

			if (parent)
			{
				ImGui::SameLine();
				if (ImGui::Button("Delete"))
					Destroy();
			};

			if (!m_inherit_thread_id)
			{
				ImGui::PushItemWidth(200);
				ImGui::Text("Thread ID (-1 is not processed): ");
				ImGui::SameLine();
				if (ImGui::SliderInt(" ", &m_thread_id, -1, std::thread::hardware_concurrency()))
					SetThreadID(m_thread_id);
				ImGui::PopItemWidth();
				ImGui::SameLine();
			}
			else ImGui::Text("Thread ID: %i", m_thread_id);

			ImGui::SameLine();
			
			if (ImGui::Checkbox("Inherit Host Thread", &m_inherit_thread_id))
				if (m_inherit_thread_id)
					SetThreadID(m_thread_id);

			ImGui::Text("Name: ");
			ImGui::SameLine();

			if (ImGui::InputText(std::to_string(m_id).c_str(), newName, 128))
				name = newName;

			for (ComponentBase* component : m_components)
				component->Gui();
			for (GameObject* child : m_children)
				child->Gui();

			ImGui::TreePop();
		};

		ImGui::PopID();
	};
};
