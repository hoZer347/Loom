using Microsoft.VisualStudio.PlatformUI;
using System.Windows;
using System.Windows.Controls;

namespace LoomVisualStudio
{
	// Asks what the new script is called, the way Add New Item does.
	sealed class ScriptNameDialog : DialogWindow
	{
		const string defaultName = "NewScript";
		const double spacing = 8;
		const double nameWidth = 280;
		const double buttonWidth = 75;

		readonly TextBox m_name = new TextBox { Text = defaultName, MinWidth = nameWidth };

		public ScriptNameDialog(string folder)
		{
			Title = LoomPackage.Title;
			SizeToContent = SizeToContent.WidthAndHeight;
			ResizeMode = ResizeMode.NoResize;
			WindowStartupLocation = WindowStartupLocation.CenterOwner;
			HasMinimizeButton = false;
			HasMaximizeButton = false;

			var add = new Button { Content = "Add", IsDefault = true, MinWidth = buttonWidth, Margin = new Thickness(0, 0, spacing, 0) };
			var cancel = new Button { Content = "Cancel", IsCancel = true, MinWidth = buttonWidth };

			add.Click += (sender, e) => DialogResult = true;

			var buttons = new StackPanel
			{
				Orientation = Orientation.Horizontal,
				HorizontalAlignment = HorizontalAlignment.Right,
				Margin = new Thickness(0, spacing, 0, 0),
			};

			buttons.Children.Add(add);
			buttons.Children.Add(cancel);

			var layout = new StackPanel { Margin = new Thickness(spacing * 2) };

			layout.Children.Add(new TextBlock { Text = $"Name of the new component, written to {folder}:", Margin = new Thickness(0, 0, 0, spacing) });
			layout.Children.Add(m_name);
			layout.Children.Add(buttons);

			Content = layout;

			Loaded += (sender, e) =>
			{
				m_name.Focus();
				m_name.SelectAll();
			};
		}

		public string ScriptName => m_name.Text.Trim();
	}
}
