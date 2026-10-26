using Microsoft.VisualStudio.Text;
using Microsoft.VisualStudio.Text.Editor;
using Microsoft.VisualStudio.Text.Outlining;
using Microsoft.VisualStudio.Utilities;
using System;
using System.ComponentModel.Composition;
using System.Linq;

namespace LoomVisualStudio
{
	/// Summary:
	/// * RegionCollapser:
	/// * - Opens every file with its #pragma region and #region blocks
	/// *   collapsed, so it reads as its outline
	/// * - The language service outlines a file a while after it opens, so
	/// *   regions collapse as they arrive, until the file is edited or one is
	/// *   expanded, after which a region opened or written by hand stays open
	//
	[Export(typeof(IWpfTextViewCreationListener))]
	[ContentType("code")]
	[TextViewRole(PredefinedTextViewRoles.Document)]
	sealed class RegionCollapser : IWpfTextViewCreationListener
	{
		static readonly string[] directives = { "#pragma region", "#region" };

		[Import]
		IOutliningManagerService m_outlining = null;

		public void TextViewCreated(IWpfTextView view)
		{
			IOutliningManager manager = m_outlining.GetOutliningManager(view);

			if (manager == null)
				return;

			void Collapse(object sender, RegionsChangedEventArgs e) => CollapseRegions(manager, e.AffectedSpan);

			void Stop(object sender, EventArgs e)
			{
				manager.RegionsChanged -= Collapse;
				manager.RegionsExpanded -= Stop;
				view.TextBuffer.Changed -= Stop;
				view.Closed -= Stop;
			}

			manager.RegionsChanged += Collapse;
			manager.RegionsExpanded += Stop;
			view.TextBuffer.Changed += Stop;
			view.Closed += Stop;

			CollapseRegions(manager, new SnapshotSpan(view.TextSnapshot, 0, view.TextSnapshot.Length));
		}

		static void CollapseRegions(IOutliningManager manager, SnapshotSpan span)
		{
			manager.CollapseAll(span, region =>
			{
				string line = region.Extent.GetStartPoint(span.Snapshot).GetContainingLine().GetText().TrimStart();
				return directives.Any(directive => line.StartsWith(directive, StringComparison.Ordinal));
			});
		}
	}
}
