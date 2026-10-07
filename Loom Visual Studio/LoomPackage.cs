using Microsoft.VisualStudio;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Shell.Interop;
using System;
using System.ComponentModel.Design;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;

namespace LoomVisualStudio
{
	/// Summary:
	/// * LoomPackage:
	/// * - Add > Loom Script... in Solution Explorer, on a Loom project's scripts
	/// *   project or any folder or file in it
	/// * - The script is written into the folder that was right-clicked (a file's
	/// *   own folder, or the project's for the project and its filters) by the
	/// *   project's own editor, the one F5 starts, so it gets the same template
	/// *   and the same name checks as Create > Script in the editor
	//
	[PackageRegistration(UseManagedResourcesOnly = true, AllowsBackgroundLoading = true)]
	[ProvideMenuResource("Menus.ctmenu", 1)]
	[ProvideAutoLoad(VSConstants.UICONTEXT.SolutionExistsAndFullyLoaded_string, PackageAutoLoadFlags.BackgroundLoad)]
	[Guid(PackageGuid)]
	public sealed class LoomPackage : AsyncPackage
	{
		public const string PackageGuid = "69f330bb-26c0-41bf-a703-9df61f61d634";

		static readonly Guid commandSet = new Guid("76837ee1-2516-42e3-b0fa-d0c4a6c01015");
		const int addScriptCommand = 0x0100;

		// A scripts project is told apart by what F5 runs, which is also the
		// editor that writes the script.
		const string debuggerCommandProperty = "LocalDebuggerCommand";
		const string editorExecutable = "Loom Editor.exe";

		internal const string Title = "Add Loom Script";

		// Where a script made from the current selection goes.
		sealed class Target
		{
			public IVsHierarchy Hierarchy;
			public string Project;
			public string Folder;
			public string Editor;
		};

		protected override async Task InitializeAsync(CancellationToken cancellationToken, IProgress<ServiceProgressData> progress)
		{
			await JoinableTaskFactory.SwitchToMainThreadAsync(cancellationToken);

			if (!(await GetServiceAsync(typeof(IMenuCommandService)) is OleMenuCommandService commands))
				return;

			// Takes the name as an argument too, as in Project.LoomScript Enemy in the
			// Command Window, which skips asking for it.
			var command = new OleMenuCommand(OnAddScript, new CommandID(commandSet, addScriptCommand))
			{
				ParametersDescription = "$",
			};

			command.BeforeQueryStatus += OnQueryStatus;
			commands.AddCommand(command);
		}

		void OnQueryStatus(object sender, EventArgs e)
		{
			ThreadHelper.ThrowIfNotOnUIThread();

			var command = (OleMenuCommand)sender;
			command.Visible = command.Enabled = SelectedTarget() != null;
		}

		void OnAddScript(object sender, EventArgs e)
		{
			ThreadHelper.ThrowIfNotOnUIThread();

			Target target = SelectedTarget();

			if (target == null)
				return;

			if (!File.Exists(target.Editor))
			{
				Show($"Build the Loom editor first: {target.Editor} is missing.", OLEMSGICON.OLEMSGICON_WARNING);
				return;
			}

			string name = ((e as OleMenuCmdEventArgs)?.InValue as string)?.Trim();

			if (string.IsNullOrEmpty(name))
			{
				var dialog = new ScriptNameDialog(RelativeFolder(target));

				if (dialog.ShowModal() != true)
					return;

				name = dialog.ScriptName;
			}

			if (name.Length > 0)
				_ = JoinableTaskFactory.RunAsync(() => AddScriptAsync(target, name));
		}

		async Task AddScriptAsync(Target target, string name)
		{
			var (exitCode, output, error) = await Task.Run(() => RunEditorAsync(target, name));

			await JoinableTaskFactory.SwitchToMainThreadAsync(DisposalToken);

			if (exitCode != 0)
			{
				Show(error.Length > 0 ? error : $"The editor could not write {name}.hpp.", OLEMSGICON.OLEMSGICON_CRITICAL);
				return;
			}

			// The project takes its scripts from a **\*.hpp glob, which is only
			// expanded again when the project is reloaded.
			if (await GetServiceAsync(typeof(SVsSolution)) is IVsSolution solution &&
				ErrorHandler.Succeeded(solution.GetGuidOfProject(target.Hierarchy, out Guid project)))
				((IVsSolution4)solution).ReloadProject(ref project);

			VsShellUtilities.OpenDocument(this, output);
		}

		static async Task<(int exitCode, string output, string error)> RunEditorAsync(Target target, string name)
		{
			var start = new ProcessStartInfo(target.Editor, $"--new-script {Quote(target.Project)} {Quote(target.Folder)} {Quote(name)}")
			{
				UseShellExecute = false,
				CreateNoWindow = true,
				RedirectStandardOutput = true,
				RedirectStandardError = true,
			};

			using (Process process = Process.Start(start))
			{
				var output = process.StandardOutput.ReadToEndAsync();
				var error = process.StandardError.ReadToEndAsync();

				string written = await output;
				string problem = await error;

				process.WaitForExit();

				return (process.ExitCode, written.Trim(), problem.Trim());
			}
		}

		// A path ending in a backslash would escape its own closing quote.
		static string Quote(string argument) => "\"" + argument.TrimEnd('\\') + "\"";

		static string RelativeFolder(Target target)
		{
			string root = Path.GetDirectoryName(target.Project);
			string relative = target.Folder.Length > root.Length ? target.Folder.Substring(root.Length).TrimStart('\\') : "";

			return Path.Combine(Path.GetFileName(root), relative);
		}

		Target SelectedTarget()
		{
			ThreadHelper.ThrowIfNotOnUIThread();

			var selection = GetService(typeof(SVsShellMonitorSelection)) as IVsMonitorSelection;

			IntPtr hierarchyPointer = IntPtr.Zero;
			IntPtr container = IntPtr.Zero;

			try
			{
				if (selection == null ||
					ErrorHandler.Failed(selection.GetCurrentSelection(out hierarchyPointer, out uint item, out IVsMultiItemSelect multiple, out container)) ||
					hierarchyPointer == IntPtr.Zero ||
					multiple != null)
					return null;

				var hierarchy = Marshal.GetObjectForIUnknown(hierarchyPointer) as IVsHierarchy;

				if (!(hierarchy is IVsProject project) ||
					ErrorHandler.Failed(project.GetMkDocument(VSConstants.VSITEMID_ROOT, out string projectPath)))
					return null;

				string editor = EditorOf(hierarchy);

				if (editor == null)
					return null;

				// A filter has no folder of its own, so a script made there goes
				// beside the project like one made on the project itself.
				string folder = Path.GetDirectoryName(projectPath);

				if (item != VSConstants.VSITEMID_ROOT &&
					ErrorHandler.Succeeded(project.GetMkDocument(item, out string itemPath)) &&
					!string.IsNullOrEmpty(itemPath))
				{
					if (Directory.Exists(itemPath))
						folder = itemPath;
					else if (File.Exists(itemPath))
						folder = Path.GetDirectoryName(itemPath);
				}

				return new Target
				{
					Hierarchy = hierarchy,
					Project = projectPath,
					Folder = folder,
					Editor = editor,
				};
			}
			finally
			{
				if (hierarchyPointer != IntPtr.Zero)
					Marshal.Release(hierarchyPointer);

				if (container != IntPtr.Zero)
					Marshal.Release(container);
			}
		}

		// The editor a Loom scripts project runs under F5 in its active
		// configuration, or null for any other project.
		string EditorOf(IVsHierarchy hierarchy)
		{
			ThreadHelper.ThrowIfNotOnUIThread();

			if (!(hierarchy is IVsBuildPropertyStorage storage) ||
				!(GetService(typeof(SVsSolutionBuildManager)) is IVsSolutionBuildManager builds))
				return null;

			var configurations = new IVsProjectCfg[1];

			if (ErrorHandler.Failed(builds.FindActiveProjectCfg(IntPtr.Zero, IntPtr.Zero, hierarchy, configurations)) ||
				configurations[0] == null ||
				ErrorHandler.Failed(configurations[0].get_CanonicalName(out string configuration)))
				return null;

			if (ErrorHandler.Failed(storage.GetPropertyValue(
					debuggerCommandProperty,
					configuration,
					(uint)_PersistStorageType.PST_PROJECT_FILE,
					out string command)) ||
				string.IsNullOrEmpty(command))
				return null;

			return string.Equals(Path.GetFileName(command), editorExecutable, StringComparison.OrdinalIgnoreCase)
				? command
				: null;
		}

		void Show(string message, OLEMSGICON icon)
		{
			VsShellUtilities.ShowMessageBox(this, message, Title, icon, OLEMSGBUTTON.OLEMSGBUTTON_OK, OLEMSGDEFBUTTON.OLEMSGDEFBUTTON_FIRST);
		}
	}
}
