// hoZer/Flipbook -- the sheet-sampling half of SpriteManager.
//
// The quad is a unit square and every last thing about the sprite arrives as a uniform:
// which cell of the sheet to read, how fast to walk along the row, how big to draw it and
// where its origin sits inside it. Nothing here is baked into the mesh, so one shared
// quad draws every sprite in the game and changing a clip costs a uniform rather than a
// rebuild.


// ===COMMON===

precision highp float;

uniform vec4 _Quad;			// middle in xy, size in zw, local units
uniform vec4 _Flip;			// flipX, flipY
uniform float _Spin;		// radians, about the pivot


// ===VERTEX===

layout(location = 0) in vec3 aPos;	// unit quad, -0.5 .. 0.5

uniform mat4 mvp;
uniform vec3 _Position;

out vec2 vUV;

void main()
{
	// Cell space before anything is done to it, so a flip mirrors the art rather than
	// the geometry and a spin turns the sprite rather than skewing its sampling.
	vec2 uv = aPos.xy + vec2(0.5);

	if (_Flip.x > 0.5)
		uv.x = 1.0 - uv.x;

	if (_Flip.y > 0.5)
		uv.y = 1.0 - uv.y;

	vUV = uv;

	vec2 local = aPos.xy * _Quad.zw;

	float s = sin(_Spin);
	float c = cos(_Spin);

	local = vec2(
		local.x * c - local.y * s,
		local.x * s + local.y * c);

	gl_Position = vec4(local + _Quad.xy + _Position.xy, _Position.z, 1.0) * mvp;
};


// ===FRAGMENT===

in vec2 vUV;

out vec4 FragColor;

uniform sampler2D _MainTex;

uniform vec4 _SheetSize;	// sheet width, height, in pixels
uniform vec4 _CellSize;		// one cell, in sheet pixels
uniform vec4 _Anim;			// clip row, frames in the clip, frames per second
uniform vec4 _Play;			// looping, start time
uniform vec4 _Color;
uniform vec4 _Outline;
uniform float _OutlineWidth;	// in sheet pixels, so it holds at any scale
uniform float _Time;

// Where a point in cell space lands on the sheet.
vec2 CellToSheet(vec2 uv, float frame)
{
	// Clip 0 is the top row of the sheet, and the sheet's own origin is its bottom
	// left, so the row is counted down from the top.
	float column = frame;
	float row = _Anim.x;

	vec2 pixel = vec2(
		(column + uv.x) * _CellSize.x,
		_SheetSize.y - (row + 1.0) * _CellSize.y + uv.y * _CellSize.y);

	return pixel / _SheetSize.xy;
};

float FrameNow()
{
	float length = max(1.0, _Anim.y);

	// A speed of zero freezes the sprite on frame 0, rather than dividing by it.
	if (_Anim.z <= 0.0)
		return 0.0;

	float elapsed = (_Time - _Play.y) * _Anim.z;

	// Not looping means running once and parking on the last frame -- a death or a hit
	// that snapped back to its first frame would read as the animation having restarted.
	if (_Play.x > 0.5)
		return floor(mod(elapsed, length));

	return min(floor(elapsed), length - 1.0);
};

void main()
{
	float frame = FrameNow();

	vec4 sampled = texture(_MainTex, CellToSheet(vUV, frame));

	// The ring is drawn where the sprite is not: a transparent pixel with ink next to it.
	// Sampled inside the cell only, or a silhouette would bleed into the frame beside it.
	if (sampled.a < 0.5 && _Outline.a > 0.0 && _OutlineWidth > 0.0)
	{
		vec2 step = vec2(_OutlineWidth) / _CellSize.xy;

		float neighbour =
			max(
				max(
					texture(_MainTex, CellToSheet(clamp(vUV + vec2(step.x, 0.0), 0.0, 1.0), frame)).a,
					texture(_MainTex, CellToSheet(clamp(vUV - vec2(step.x, 0.0), 0.0, 1.0), frame)).a),
				max(
					texture(_MainTex, CellToSheet(clamp(vUV + vec2(0.0, step.y), 0.0, 1.0), frame)).a,
					texture(_MainTex, CellToSheet(clamp(vUV - vec2(0.0, step.y), 0.0, 1.0), frame)).a));

		if (neighbour >= 0.5)
		{
			FragColor = vec4(_Outline.rgb, _Outline.a * _Color.a);

			return;
		};
	};

	FragColor = sampled * _Color;
};
