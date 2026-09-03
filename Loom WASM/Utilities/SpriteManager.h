#pragma once

#include "SpriteSheetScanner.h"

#include "Component.h"

#include "Vector.h"

#include <string>
#include <vector>


namespace Loom
{
	struct Shader;

	/// Draws one cell of a sprite sheet, and walks along a row of them to animate.
	///
	/// The sheet is read as a plain grid of cells -- no sprite assets, no import slicing,
	/// nothing to re-slice when the art changes. A clip is a row; its length is measured
	/// off the sheet itself (see SpriteSheetScanner) so adding a frame to an animation is
	/// a change to the .png and nothing else.
	///
	/// Everything the frame needs is a uniform on a shared unit quad, so switching clip
	/// costs a uniform rather than a rebuild. The sprite carries its own position, since
	/// GameObject has no transform, and the sheet is a GL texture handle the game
	/// supplies. sortingOrder is honoured by drawing order.
	struct SpriteManager : Component<SpriteManager>
	{
		SpriteManager();
		~SpriteManager();

		#pragma region Sheet

		/// The sheet, as an OpenGL texture handle. Whatever loaded the .png owns it.
		unsigned int texture = 0;

		/// The size of that texture, in pixels. The shader needs it to find a cell, and it
		/// cannot be asked of a texture under GL ES.
		Int2 sheetSize{ 0, 0 };

		/// The sheet's pixels, for Section to measure. Optional: without them the stored
		/// clip lengths stand, which is what a game that measured them once and wrote them
		/// down wants.
		PixelSheet sheetPixels{ };

		/// Size of one cell, in sheet pixels.
		Int2 spriteSize{ 100, 100 };

		void SetSpriteSize(const Int2& size);

		/// Sheet pixels per world unit. Sizes the quad; has no say in which cell is sampled.
		float pixelsPerUnit = 100.0f;

		/// Multiplies the sprite's natural size -- cell size divided by pixels per unit.
		float scale = 1.0f;

		/// Where the origin sits inside the cell, normalised. (0.5, 0) puts it at the
		/// bottom middle.
		Float2 pivot{ 0.5f, 0.0f };

		#pragma endregion

		#pragma region Animation

		/// Row of the sheet to play, counted from the top.
		int Clip() const { return clip; };

		/// Sets the clip, adopting its measured length and restarting it. A same-value
		/// assignment is ignored -- see Play for the one that always restarts.
		void SetClip(int value);

		/// Restarts even when it is already the current clip.
		void Play(int newClip);

		/// Frames in the current clip. Filled in by Section when the sheet has been measured.
		int animationLength = 1;

		/// Playback rate, in frames per second. Zero freezes the sprite on frame 0.
		float animationSpeed = 12.0f;

		/// Off makes this a one-shot: it runs from when it was started and parks on its
		/// last frame.
		bool loop = true;

		/// True once a one-shot has run its length. Always false while looping.
		bool Finished() const;

		void Restart();

		#pragma endregion

		#pragma region Sectioning

		/// Re-measures the clip lengths from the sheet's pixels. False when there are none
		/// to read, in which case whatever lengths are already stored stand.
		bool Section();

		/// Frames per clip, indexed by row. Measured from the sheet; hand edits stick until
		/// it is measured again.
		const std::vector<int>& ClipLengths() const { return clipLengths; };

		void SetClipLengths(std::vector<int> lengths);

		/// Alpha at or above which a pixel counts as ink while measuring.
		unsigned char emptyAlphaThreshold = 1;

		int ClipCount() const { return int(clipLengths.size()); };

		int MeasuredLengthOf(int index) const;

		#pragma endregion

		#pragma region Draw

		/// Where the sprite is. Loom's GameObject has no transform, so the sprite carries
		/// its own.
		Math::vec3<float> position{ };

		/// Mirrors horizontally about the pivot.
		bool flipX = false;

		/// Mirrors vertically about the pivot.
		bool flipY = false;

		/// Turns the sprite in the plane it is drawn on, in degrees.
		float spin = 0.0f;

		/// Multiplied over the sprite. Alpha fades it.
		float color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

		/// Ring drawn around the sprite's silhouette. Alpha 0 turns it off.
		float outline[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

		/// How thick that ring is, in sheet pixels -- so it holds its thickness relative to
		/// the art at any scale.
		float outlineWidth = 2.0f;

		/// Draw order. Higher draws in front.
		int sortingOrder = 0;

		/// The sprite's size in world units -- cell size over pixels per unit, scaled.
		Float2 WorldSize() const;

		/// The rect the shader stretches the shared quad into: middle in xy, size in zw,
		/// local units.
		void QuadVector(float out[4]) const;

		#pragma endregion

		#pragma region Shared setup

		/// The shader every sprite draws with. Compiled once, on the first draw.
		static inline std::string shaderPath = "Flipbook.shader";

		/// The camera matrix handed to that shader. Identity until a game sets one --
		/// Loom has no camera to ask.
		static inline float viewProjection[16] =
		{
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f,
		};

		#pragma endregion

		void OnRender() override;
		void OnGui() override;

	private:
		void AdoptLength();

		int clip = 0;

		std::vector<int> clipLengths{ };

		float startTime = 0.0f;

		unsigned int m_program = 0;
	};
};
