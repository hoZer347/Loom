using Microsoft.VisualStudio.Text;
using Microsoft.VisualStudio.Text.Tagging;
using Microsoft.VisualStudio.Utilities;
using System;
using System.Collections.Generic;
using System.ComponentModel.Composition;
using System.Linq;

namespace LoomVisualStudio
{
	/// Summary:
	/// * RegionOutliner:
	/// * - Outlines each C++ #pragma region and C# #region block as collapsed
	/// *   by default, which Visual Studio applies to the outlines a file has
	/// *   as it opens
	/// * - C# is covered too, since Roslyn's own default-collapsed regions
	/// *   miss a file opened before its workspace has loaded; Visual Studio
	/// *   merges the two identical outlines into one
	/// * - An outline that arrives later, such as a region typed by hand,
	/// *   starts open
	//
	[Export(typeof(ITaggerProvider))]
	[TagType(typeof(IOutliningRegionTag))]
	[ContentType(cppContentType)]
	[ContentType(csharpContentType)]
	sealed class RegionOutlinerProvider : ITaggerProvider
	{
		const string cppContentType = "C/C++";
		const string csharpContentType = "CSharp";

		public ITagger<T> CreateTagger<T>(ITextBuffer buffer) where T : ITag
		{
			return buffer.Properties.GetOrCreateSingletonProperty(() => buffer.ContentType.IsOfType(csharpContentType)
				? new RegionOutliner(buffer, "#region", "#endregion")
				: new RegionOutliner(buffer, "#pragma region", "#pragma endregion")) as ITagger<T>;
		}
	}

	sealed class RegionOutliner : ITagger<IOutliningRegionTag>
	{
		const int hintLines = 20;

		readonly string m_beginDirective;
		readonly string m_endDirective;
		List<SnapshotSpan> m_regions;

		public event EventHandler<SnapshotSpanEventArgs> TagsChanged;

		public RegionOutliner(ITextBuffer buffer, string beginDirective, string endDirective)
		{
			m_beginDirective = beginDirective;
			m_endDirective = endDirective;
			m_regions = FindRegions(buffer.CurrentSnapshot);
			buffer.Changed += OnChanged;
		}

		public IEnumerable<ITagSpan<IOutliningRegionTag>> GetTags(NormalizedSnapshotSpanCollection spans)
		{
			if (spans.Count == 0)
				yield break;

			foreach (SnapshotSpan region in m_regions)
			{
				SnapshotSpan span = region.TranslateTo(spans[0].Snapshot, SpanTrackingMode.EdgeExclusive);

				if (!spans.IntersectsWith(span))
					continue;

				ITextSnapshotLine first = span.Start.GetContainingLine();
				int lastHintLine = Math.Min(first.LineNumber + hintLines, span.End.GetContainingLine().LineNumber);
				string hint = new SnapshotSpan(span.Start, span.Snapshot.GetLineFromLineNumber(lastHintLine).End).GetText();

				yield return new TagSpan<IOutliningRegionTag>(span, new OutliningRegionTag(true, false, first.GetText().Trim(), hint));
			}
		}

		void OnChanged(object sender, TextContentChangedEventArgs e)
		{
			IEnumerable<SnapshotSpan> moved = m_regions.Select(region => region.TranslateTo(e.After, SpanTrackingMode.EdgeExclusive));

			List<SnapshotSpan> regions = FindRegions(e.After);
			bool changed = !regions.SequenceEqual(moved);

			m_regions = regions;

			if (changed)
				TagsChanged?.Invoke(this, new SnapshotSpanEventArgs(new SnapshotSpan(e.After, 0, e.After.Length)));
		}

		// Each region runs from its directive to the end of its endregion's
		// line, so the collapsed line keeps its indent.
		List<SnapshotSpan> FindRegions(ITextSnapshot snapshot)
		{
			List<SnapshotSpan> regions = new List<SnapshotSpan>();
			Stack<int> open = new Stack<int>();

			foreach (ITextSnapshotLine line in snapshot.Lines)
			{
				string text = line.GetText();
				int indent = text.Length - text.TrimStart().Length;

				if (string.CompareOrdinal(text, indent, m_beginDirective, 0, m_beginDirective.Length) == 0)
					open.Push(line.Start + indent);
				else if (string.CompareOrdinal(text, indent, m_endDirective, 0, m_endDirective.Length) == 0 && open.Count > 0)
					regions.Add(new SnapshotSpan(snapshot, Span.FromBounds(open.Pop(), line.End)));
			}

			regions.Sort((a, b) => a.Start.CompareTo(b.Start));
			return regions;
		}
	}
}
