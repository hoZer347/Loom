#include "StateMachineMonitor.h"

#include "StateMachine.h"

#include "EditorGui.h"
#include "imgui.h"

#include <filesystem>
#include <iostream>


namespace Loom
{
	void StateMachineMonitor::DrawInline(StateMachineBase& machine)
	{
		const State* current = machine.Current();

		ImGui::Text("Elapsed:  %.3f", machine.ElapsedTime());

		if (machine.IsDisabled())
		{
			ImGui::SameLine();
			ImGui::TextUnformatted("(disabled)");
		};

		// A parallel state is several states at once, and a monitor that names only the
		// wrapper says nothing about what is actually running.
		if (const auto* subStates = StateMachineBase::SubStatesOf(current))
			for (const auto& state : *subStates)
				ImGui::BulletText("%s", state ? state->Name().c_str() : "<null>");

		if (ImGui::TreeNode("queued", "Queued (%zu)", machine.Queued().size()))
		{
			for (const auto& state : machine.Queued())
				ImGui::BulletText("%s", state ? state->Name().c_str() : "<null>");

			ImGui::TreePop();
		};

		ImGui::InputInt("History depth", &machine.maxSavedStates);

		if (machine.maxSavedStates < 0)
			machine.maxSavedStates = 0;

		if (ImGui::TreeNode("history", "History (%zu)", machine.History().size()))
		{
			for (const auto& entry : machine.History())
				ImGui::BulletText(
					"[%.3f] %s",
					entry.first,
					entry.second ? entry.second->Name().c_str() : "<null>");

			ImGui::TreePop();
		};

		if (ImGui::Button("Flush State History"))
			std::cout << machine.FlushStateHistory() << std::flush;

		ImGui::SameLine();

		if (ImGui::Button("Proceed"))
			machine.Proceed();

		ImGui::SameLine();

		if (ImGui::Button(machine.IsDisabled() ? "Enable" : "Disable"))
		{
			if (machine.IsDisabled())
				machine.Enable();
			else machine.Disable();
		};
	};

	bool StateMachineMonitor::DrawStateField(StateReference& reference)
	{
		constexpr const char* none = "None";

		const std::string& name = reference.StateType();

		// A name nothing is registered under is kept rather than dropped, since
		// the scripts that register it may just not be built yet.
		const std::string shown =
			name.empty() ? none :
			reference.IsSet() ? name :
			name + " (missing)";

		std::string picked = name;

		if (ImGui::BeginCombo("##state", shown.c_str()))
		{
			if (ImGui::Selectable(none, name.empty()))
				picked.clear();

			for (const std::string& registered : StateRegistry::Names())
				if (ImGui::Selectable(registered.c_str(), registered == name))
					picked = registered;

			ImGui::EndCombo();
		};

		// A state's header is named after it.
		if (std::string dropped; AcceptAssetDrop(dropped))
		{
			const std::string stem = std::filesystem::path(dropped).stem().string();

			if (StateRegistry::Contains(stem))
				picked = stem;
		};

		if (picked == name)
			return false;

		reference = picked.empty()
			? StateReference{ }
			: StateReference::Named(picked);

		return true;
	};

	void StateMachineMonitor::Draw(bool* open)
	{
		if (!ImGui::Begin("State Machines", open))
		{
			ImGui::End();

			return;
		};

		const std::vector<StateMachineBase*> machines = StateMachineBase::All();

		ImGui::Text("%zu machine(s)", machines.size());
		ImGui::Separator();

		for (StateMachineBase* machine : machines)
		{
			if (machine == nullptr)
				continue;

			ImGui::PushID(machine);

			if (ImGui::TreeNode(machine, "%s", machine->MachineName().c_str()))
			{
				const State* current = machine->Current();

				ImGui::Text("Current:  %s", current ? current->Name().c_str() : "<none>");

				DrawInline(*machine);

				ImGui::TreePop();
			};

			ImGui::PopID();
		};

		ImGui::End();
	};
};
