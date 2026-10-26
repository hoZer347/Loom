#include "EditableMesh.h"

#include "Mesh.h"

#include "glm/gtc/constants.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>


namespace Loom
{
	namespace
	{
		constexpr size_t floats_per_position = 3;
		constexpr size_t floats_per_uv = 2;
		constexpr size_t floats_per_color = 4;
		constexpr size_t corners_per_triangle = 3;

		// Below this a ray runs along a triangle's plane, or a hit is the
		// triangle the ray starts on.
		constexpr float ray_epsilon = 1e-7f;

		// Two faces whose normals are this close lie flat together, and faces
		// whose normals add up to less than this have no way they face.
		constexpr float flat_epsilon = 1e-4f;

		// A UV square's corners, in the order a quad's corners go round.
		const std::vector<glm::vec2> quad_uvs = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };

		// On a circle in the XZ plane, going round anticlockwise seen from above.
		glm::vec3 Round(float radius, float y, int step, int steps)
		{
			const float angle = glm::two_pi<float>() * step / steps;

			return glm::vec3(radius * std::cos(angle), y, -radius * std::sin(angle));
		};

		// A cap's UV for the same point, the circle filling the texture.
		glm::vec2 RoundUV(int step, int steps)
		{
			constexpr float half = 0.5f;

			const float angle = glm::two_pi<float>() * step / steps;

			return glm::vec2(half + half * std::cos(angle), half + half * std::sin(angle));
		};
	};

	EditableMesh::Edge EditableMesh::EdgeOf(uint32_t a, uint32_t b)
	{
		return a < b ? Edge(a, b) : Edge(b, a);
	};

	EditableMesh EditableMesh::FromTriangles(const std::vector<float>& positions, const std::vector<float>& uvs, const std::vector<float>& normals, const std::vector<float>& colors, const std::vector<UVLayer>& layers)
	{
		const size_t count = positions.size() / floats_per_position / corners_per_triangle * corners_per_triangle;

		EditableMesh mesh;
		mesh.hasUVs = count > 0 && uvs.size() == count * floats_per_uv;
		mesh.hasNormals = count > 0 && normals.size() == count * floats_per_position;
		mesh.hasColors = count > 0 && colors.size() == count * floats_per_color;

		std::vector<const UVLayer*> kept{ };

		for (const UVLayer& layer : layers)
			if (count > 0 && layer.values.size() == count * layer.words)
			{
				kept.push_back(&layer);
				mesh.layers.push_back({ layer.id, layer.words });
			};

		std::map<std::array<float, floats_per_position>, uint32_t> welded{ };

		for (size_t i = 0; i < count; i++)
		{
			const glm::vec3 position(positions[i * floats_per_position], positions[i * floats_per_position + 1], positions[i * floats_per_position + 2]);

			// Adding zero turns -0 into 0, which compare equal but would key apart.
			const std::array<float, floats_per_position> key{ position.x + 0.0f, position.y + 0.0f, position.z + 0.0f };
			const auto [found, added] = welded.try_emplace(key, (uint32_t)mesh.vertices.size());

			if (added)
				mesh.vertices.push_back(position);

			if (i % corners_per_triangle == 0)
				mesh.faces.emplace_back();

			Corner corner{ found->second };

			if (mesh.hasUVs)
				corner.uv = glm::vec2(uvs[i * floats_per_uv], uvs[i * floats_per_uv + 1]);

			if (mesh.hasNormals)
				corner.normal = glm::vec3(normals[i * floats_per_position], normals[i * floats_per_position + 1], normals[i * floats_per_position + 2]);

			if (mesh.hasColors)
				corner.color = glm::vec4(colors[i * floats_per_color], colors[i * floats_per_color + 1], colors[i * floats_per_color + 2], colors[i * floats_per_color + 3]);

			for (const UVLayer* layer : kept)
				corner.layers.insert(corner.layers.end(), layer->values.begin() + i * layer->words, layer->values.begin() + (i + 1) * layer->words);

			mesh.faces.back().corners.push_back(corner);
		};

		return mesh;
	};

	void EditableMesh::ToTriangles(std::vector<float>& positions, std::vector<float>& uvs, std::vector<float>& normals, std::vector<float>& colors) const
	{
		positions.clear();
		uvs.clear();
		normals.clear();
		colors.clear();

		const auto add = [&](const Corner& corner)
		{
			const glm::vec3& position = vertices[corner.vertex];
			positions.insert(positions.end(), { position.x, position.y, position.z });

			if (hasUVs)
				uvs.insert(uvs.end(), { corner.uv.x, corner.uv.y });

			if (hasNormals)
				normals.insert(normals.end(), { corner.normal.x, corner.normal.y, corner.normal.z });

			if (hasColors)
				colors.insert(colors.end(), { corner.color.r, corner.color.g, corner.color.b, corner.color.a });
		};

		for (const Face& face : faces)
			for (size_t i = 2; i < face.corners.size(); i++)
			{
				add(face.corners[0]);
				add(face.corners[i - 1]);
				add(face.corners[i]);
			};
	};

	std::vector<UVLayer> EditableMesh::ToUVLayers() const
	{
		std::vector<UVLayer> out{ };

		for (const UVLayout& layer : layers)
			out.push_back({ layer.id, layer.words });

		const auto add = [&](const Corner& corner)
		{
			auto words = corner.layers.begin();

			for (UVLayer& layer : out)
			{
				layer.values.insert(layer.values.end(), words, words + layer.words);
				words += layer.words;
			};
		};

		for (const Face& face : faces)
			for (size_t i = 2; i < face.corners.size(); i++)
			{
				add(face.corners[0]);
				add(face.corners[i - 1]);
				add(face.corners[i]);
			};

		return out;
	};

	void EditableMesh::WriteTo(Mesh& mesh) const
	{
		std::vector<float> positions{ };
		std::vector<float> uvs{ };
		std::vector<float> normals{ };
		std::vector<float> colors{ };
		ToTriangles(positions, uvs, normals, colors);

		mesh.m_vertices = std::move(positions);
		mesh.m_textureCoordinates = std::move(uvs);
		mesh.m_normals = std::move(normals);
		mesh.m_colors = std::move(colors);

		mesh.SetUVLayers(ToUVLayers());
	};

	uint32_t EditableMesh::AddFace(const std::vector<uint32_t>& corners, const std::vector<glm::vec2>& uvs)
	{
		Face face;

		for (size_t i = 0; i < corners.size(); i++)
			face.corners.push_back(Corner{ corners[i], i < uvs.size() ? uvs[i] : glm::vec2(0.0f) });

		return AddFace(std::move(face));
	};

	uint32_t EditableMesh::AddFace(Face face)
	{
		faces.push_back(std::move(face));

		const uint32_t added = (uint32_t)faces.size() - 1;
		Flatten(added);

		return added;
	};

	EditableMesh EditableMesh::Cube(float size)
	{
		constexpr int corner_count = 8;

		const float half = size / 2.0f;

		EditableMesh mesh;
		mesh.hasUVs = true;
		mesh.hasNormals = true;

		// Bit 0 is x, bit 1 y and bit 2 z, each set for the positive side.
		for (int i = 0; i < corner_count; i++)
			mesh.vertices.emplace_back(i & 1 ? half : -half, i & 2 ? half : -half, i & 4 ? half : -half);

		mesh.AddFace({ 4, 5, 7, 6 }, quad_uvs);
		mesh.AddFace({ 1, 0, 2, 3 }, quad_uvs);
		mesh.AddFace({ 5, 1, 3, 7 }, quad_uvs);
		mesh.AddFace({ 0, 4, 6, 2 }, quad_uvs);
		mesh.AddFace({ 6, 7, 3, 2 }, quad_uvs);
		mesh.AddFace({ 0, 1, 5, 4 }, quad_uvs);

		return mesh;
	};

	EditableMesh EditableMesh::Plane(float size)
	{
		const float half = size / 2.0f;

		EditableMesh mesh;
		mesh.hasUVs = true;
		mesh.hasNormals = true;
		mesh.vertices = { { -half, 0.0f, half }, { half, 0.0f, half }, { half, 0.0f, -half }, { -half, 0.0f, -half } };
		mesh.AddFace({ 0, 1, 2, 3 }, quad_uvs);

		return mesh;
	};

	EditableMesh EditableMesh::Cylinder(float radius, float height, int segments)
	{
		const float half = height / 2.0f;

		EditableMesh mesh;
		mesh.hasUVs = true;
		mesh.hasNormals = true;

		// Bottom ring first, then the top one.
		for (const float y : { -half, half })
			for (int i = 0; i < segments; i++)
				mesh.vertices.push_back(Round(radius, y, i, segments));

		std::vector<uint32_t> top{ };
		std::vector<uint32_t> bottom{ };
		std::vector<glm::vec2> top_uvs{ };
		std::vector<glm::vec2> bottom_uvs{ };

		for (int i = 0; i < segments; i++)
		{
			const uint32_t next = (uint32_t)((i + 1) % segments);
			const float u = (float)i / segments;
			const float next_u = (float)(i + 1) / segments;

			mesh.AddFace({ (uint32_t)i, next, next + segments, (uint32_t)(i + segments) }, { { u, 0.0f }, { next_u, 0.0f }, { next_u, 1.0f }, { u, 1.0f } });

			top.push_back((uint32_t)(i + segments));
			top_uvs.push_back(RoundUV(i, segments));
			bottom.insert(bottom.begin(), (uint32_t)i);
			bottom_uvs.insert(bottom_uvs.begin(), RoundUV(i, segments));
		};

		mesh.AddFace(top, top_uvs);
		mesh.AddFace(bottom, bottom_uvs);

		return mesh;
	};

	EditableMesh EditableMesh::Cone(float radius, float height, int segments)
	{
		constexpr float apex_u = 0.5f;

		const float half = height / 2.0f;

		EditableMesh mesh;
		mesh.hasUVs = true;
		mesh.hasNormals = true;

		for (int i = 0; i < segments; i++)
			mesh.vertices.push_back(Round(radius, -half, i, segments));

		const uint32_t apex = (uint32_t)mesh.vertices.size();
		mesh.vertices.emplace_back(0.0f, half, 0.0f);

		std::vector<uint32_t> bottom{ };
		std::vector<glm::vec2> bottom_uvs{ };

		for (int i = 0; i < segments; i++)
		{
			const uint32_t next = (uint32_t)((i + 1) % segments);

			mesh.AddFace({ (uint32_t)i, next, apex }, { { (float)i / segments, 0.0f }, { (float)(i + 1) / segments, 0.0f }, { apex_u, 1.0f } });

			bottom.insert(bottom.begin(), (uint32_t)i);
			bottom_uvs.insert(bottom_uvs.begin(), RoundUV(i, segments));
		};

		mesh.AddFace(bottom, bottom_uvs);

		return mesh;
	};

	EditableMesh EditableMesh::Sphere(float radius, int segments, int rings)
	{
		EditableMesh mesh;
		mesh.hasUVs = true;
		mesh.hasNormals = true;

		const uint32_t north = 0;
		const uint32_t south = 1;

		mesh.vertices = { { 0.0f, radius, 0.0f }, { 0.0f, -radius, 0.0f } };

		// The rings between the poles, from the north down.
		for (int ring = 1; ring < rings; ring++)
		{
			const float down = glm::pi<float>() * ring / rings;

			for (int i = 0; i < segments; i++)
				mesh.vertices.push_back(Round(radius * std::sin(down), radius * std::cos(down), i, segments));
		};

		const auto at = [&](int ring, int i) { return (uint32_t)(2 + (ring - 1) * segments + (i % segments)); };
		const auto uv = [&](int ring, int i) { return glm::vec2((float)i / segments, 1.0f - (float)ring / rings); };

		for (int i = 0; i < segments; i++)
		{
			mesh.AddFace({ north, at(1, i), at(1, i + 1) }, { uv(0, i), uv(1, i), uv(1, i + 1) });

			for (int ring = 1; ring + 1 < rings; ring++)
				mesh.AddFace({ at(ring + 1, i), at(ring + 1, i + 1), at(ring, i + 1), at(ring, i) }, { uv(ring + 1, i), uv(ring + 1, i + 1), uv(ring, i + 1), uv(ring, i) });

			mesh.AddFace({ south, at(rings - 1, i + 1), at(rings - 1, i) }, { uv(rings, i), uv(rings - 1, i + 1), uv(rings - 1, i) });
		};

		return mesh;
	};

	EditableMesh EditableMesh::Capsule(float radius, float height, int segments, int cap_rings)
	{
		const float half = (std::max)(height / 2.0f, radius);
		const float straight = half - radius;
		const int rows = 2 * cap_rings;

		EditableMesh mesh;
		mesh.hasUVs = true;
		mesh.hasNormals = true;

		const uint32_t north = 0;
		const uint32_t south = 1;

		mesh.vertices = { { 0.0f, half, 0.0f }, { 0.0f, -half, 0.0f } };

		// The rings from the north down, the waist twice: once ending the top
		// half sphere and once starting the bottom one.
		for (int row = 0; row < rows; row++)
		{
			const bool top = row < cap_rings;
			const float down = glm::pi<float>() * (top ? row + 1 : row) / rows;
			const float y = radius * std::cos(down) + (top ? straight : -straight);

			for (int i = 0; i < segments; i++)
				mesh.vertices.push_back(Round(radius * std::sin(down), y, i, segments));
		};

		const auto at = [&](int row, int i) { return (uint32_t)(2 + row * segments + (i % segments)); };
		const auto uv = [&](uint32_t vertex, int i) { return glm::vec2((float)i / segments, (mesh.vertices[vertex].y + half) / (2.0f * half)); };

		for (int i = 0; i < segments; i++)
		{
			mesh.AddFace({ north, at(0, i), at(0, i + 1) }, { uv(north, i), uv(at(0, i), i), uv(at(0, i + 1), i + 1) });

			for (int row = 0; row + 1 < rows; row++)
			{
				const uint32_t a = at(row + 1, i);
				const uint32_t b = at(row + 1, i + 1);
				const uint32_t c = at(row, i + 1);
				const uint32_t d = at(row, i);

				mesh.AddFace({ a, b, c, d }, { uv(a, i), uv(b, i + 1), uv(c, i + 1), uv(d, i) });
			};

			mesh.AddFace({ south, at(rows - 1, i + 1), at(rows - 1, i) }, { uv(south, i), uv(at(rows - 1, i + 1), i + 1), uv(at(rows - 1, i), i) });
		};

		return mesh;
	};

	glm::vec3 EditableMesh::FaceNormal(uint32_t face) const
	{
		const std::vector<Corner>& corners = faces[face].corners;
		glm::vec3 sum(0.0f);

		for (size_t i = 0; i < corners.size(); i++)
		{
			const glm::vec3& a = vertices[corners[i].vertex];
			const glm::vec3& b = vertices[corners[(i + 1) % corners.size()].vertex];

			sum += glm::vec3((a.y - b.y) * (a.z + b.z), (a.z - b.z) * (a.x + b.x), (a.x - b.x) * (a.y + b.y));
		};

		const float length = glm::length(sum);

		return length > 0.0f ? sum / length : glm::vec3(0.0f);
	};

	glm::vec3 EditableMesh::FaceCentre(uint32_t face) const
	{
		const std::vector<Corner>& corners = faces[face].corners;
		glm::vec3 sum(0.0f);

		for (const Corner& corner : corners)
			sum += vertices[corner.vertex];

		return corners.empty() ? sum : sum / (float)corners.size();
	};

	std::vector<EditableMesh::Edge> EditableMesh::Edges() const
	{
		std::vector<Edge> edges{ };
		std::set<Edge> seen{ };

		for (const Face& face : faces)
			for (size_t i = 0; i < face.corners.size(); i++)
			{
				const Edge edge = EdgeOf(face.corners[i].vertex, face.corners[(i + 1) % face.corners.size()].vertex);

				if (seen.insert(edge).second)
					edges.push_back(edge);
			};

		return edges;
	};

	std::set<uint32_t> EditableMesh::VerticesOfFaces(const std::set<uint32_t>& selected) const
	{
		std::set<uint32_t> found{ };

		for (const uint32_t face : selected)
			for (const Corner& corner : faces[face].corners)
				found.insert(corner.vertex);

		return found;
	};

	std::set<uint32_t> EditableMesh::VerticesOfEdges(const std::set<Edge>& edges) const
	{
		std::set<uint32_t> found{ };

		for (const Edge& edge : edges)
		{
			found.insert(edge.first);
			found.insert(edge.second);
		};

		return found;
	};

	std::set<uint32_t> EditableMesh::FacesWithin(const std::set<uint32_t>& selected) const
	{
		std::set<uint32_t> found{ };

		for (uint32_t face = 0; face < faces.size(); face++)
			if (std::all_of(faces[face].corners.begin(), faces[face].corners.end(), [&](const Corner& corner) { return selected.contains(corner.vertex); }))
				found.insert(face);

		return found;
	};

	std::set<EditableMesh::Edge> EditableMesh::EdgesWithin(const std::set<uint32_t>& selected) const
	{
		std::set<Edge> found{ };

		for (const Edge& edge : Edges())
			if (selected.contains(edge.first) && selected.contains(edge.second))
				found.insert(edge);

		return found;
	};

	void EditableMesh::Flatten(uint32_t face)
	{
		const glm::vec3 normal = FaceNormal(face);

		for (Corner& corner : faces[face].corners)
			corner.normal = normal;
	};

	void EditableMesh::Transform(const std::set<uint32_t>& moved, const glm::mat4& matrix)
	{
		for (const uint32_t vertex : moved)
			vertices[vertex] = glm::vec3(matrix * glm::vec4(vertices[vertex], 1.0f));

		for (uint32_t face = 0; face < faces.size(); face++)
			if (std::any_of(faces[face].corners.begin(), faces[face].corners.end(), [&](const Corner& corner) { return moved.contains(corner.vertex); }))
				Flatten(face);
	};

	bool EditableMesh::Extrude(const std::set<uint32_t>& selected, float distance)
	{
		glm::vec3 direction(0.0f);

		for (const uint32_t face : selected)
			direction += FaceNormal(face);

		if (glm::length(direction) < flat_epsilon)
			return false;

		const glm::vec3 offset = glm::normalize(direction) * distance;

		// An edge only one of the faces has is on the rim, where a wall goes.
		std::map<Edge, int> uses{ };

		for (const uint32_t face : selected)
		{
			const std::vector<Corner>& corners = faces[face].corners;

			for (size_t i = 0; i < corners.size(); i++)
				uses[EdgeOf(corners[i].vertex, corners[(i + 1) % corners.size()].vertex)]++;
		};

		std::map<uint32_t, uint32_t> lifted{ };

		for (const uint32_t vertex : VerticesOfFaces(selected))
		{
			lifted[vertex] = (uint32_t)vertices.size();
			vertices.push_back(vertices[vertex] + offset);
		};

		std::vector<Face> walls{ };

		for (const uint32_t face : selected)
		{
			std::vector<Corner>& corners = faces[face].corners;

			for (size_t i = 0; i < corners.size(); i++)
			{
				const Corner& from = corners[i];
				const Corner& to = corners[(i + 1) % corners.size()];

				if (uses[EdgeOf(from.vertex, to.vertex)] == 1)
					walls.push_back({ {
						{ from.vertex, from.uv, { }, from.color, from.layers },
						{ to.vertex, to.uv, { }, to.color, to.layers },
						{ lifted[to.vertex], to.uv, { }, to.color, to.layers },
						{ lifted[from.vertex], from.uv, { }, from.color, from.layers } } });
			};

			for (Corner& corner : corners)
				corner.vertex = lifted[corner.vertex];
		};

		for (Face& wall : walls)
			AddFace(std::move(wall));

		RemoveUnused();

		return true;
	};

	std::set<uint32_t> EditableMesh::Duplicate(const std::set<uint32_t>& selected)
	{
		std::map<uint32_t, uint32_t> copied{ };

		for (const uint32_t vertex : VerticesOfFaces(selected))
		{
			copied[vertex] = (uint32_t)vertices.size();
			vertices.push_back(vertices[vertex]);
		};

		std::set<uint32_t> added{ };

		for (const uint32_t face : selected)
		{
			Face copy = faces[face];

			for (Corner& corner : copy.corners)
				corner.vertex = copied[corner.vertex];

			added.insert((uint32_t)faces.size());
			faces.push_back(std::move(copy));
		};

		return added;
	};

	std::set<uint32_t> EditableMesh::Append(const EditableMesh& other)
	{
		// An empty mesh says nothing about what a mesh has, so takes the other's.
		// Colours are white where there were none, so either having them keeps them.
		if (faces.empty())
		{
			hasUVs = other.hasUVs;
			hasNormals = other.hasNormals;
			hasColors = other.hasColors;
			layers = other.layers;
		}
		else
			hasColors = hasColors || other.hasColors;

		// Where each of this mesh's layers' words come from in the other's
		// corners, if it has that layer.
		std::vector<std::optional<size_t>> from{ };
		size_t words = 0;

		for (const UVLayout& layer : layers)
		{
			size_t offset = 0;
			from.emplace_back();

			for (const UVLayout& theirs : other.layers)
			{
				if (theirs == layer)
					from.back() = offset;

				offset += theirs.words;
			};

			words += layer.words;
		};

		const uint32_t first_vertex = (uint32_t)vertices.size();
		vertices.insert(vertices.end(), other.vertices.begin(), other.vertices.end());

		std::set<uint32_t> added{ };

		for (const Face& face : other.faces)
		{
			Face copy = face;

			for (Corner& corner : copy.corners)
			{
				corner.vertex += first_vertex;

				if (layers == other.layers)
					continue;

				const std::vector<uint32_t> theirs = std::move(corner.layers);
				corner.layers.assign(words, 0);

				auto into = corner.layers.begin();

				for (size_t i = 0; i < layers.size(); i++)
				{
					if (from[i])
						std::copy_n(theirs.begin() + *from[i], layers[i].words, into);

					into += layers[i].words;
				};
			};

			added.insert((uint32_t)faces.size());
			faces.push_back(std::move(copy));
		};

		return added;
	};

	void EditableMesh::DeleteFaces(const std::set<uint32_t>& selected)
	{
		std::vector<Face> kept{ };

		for (uint32_t face = 0; face < faces.size(); face++)
			if (!selected.contains(face))
				kept.push_back(std::move(faces[face]));

		faces = std::move(kept);
		RemoveUnused();
	};

	void EditableMesh::DeleteVertices(const std::set<uint32_t>& selected)
	{
		std::set<uint32_t> doomed{ };

		for (uint32_t face = 0; face < faces.size(); face++)
			if (std::any_of(faces[face].corners.begin(), faces[face].corners.end(), [&](const Corner& corner) { return selected.contains(corner.vertex); }))
				doomed.insert(face);

		DeleteFaces(doomed);
	};

	void EditableMesh::DeleteEdges(const std::set<Edge>& edges)
	{
		std::set<uint32_t> doomed{ };

		for (uint32_t face = 0; face < faces.size(); face++)
		{
			const std::vector<Corner>& corners = faces[face].corners;

			for (size_t i = 0; i < corners.size(); i++)
				if (edges.contains(EdgeOf(corners[i].vertex, corners[(i + 1) % corners.size()].vertex)))
					doomed.insert(face);
		};

		DeleteFaces(doomed);
	};

	void EditableMesh::Flip(const std::set<uint32_t>& selected)
	{
		for (const uint32_t face : selected)
		{
			std::vector<Corner>& corners = faces[face].corners;
			std::reverse(corners.begin(), corners.end());

			for (Corner& corner : corners)
				corner.normal = -corner.normal;
		};
	};

	void EditableMesh::JoinTriangles()
	{
		constexpr size_t triangle = 3;

		std::map<Edge, std::vector<uint32_t>> owners{ };

		for (uint32_t face = 0; face < faces.size(); face++)
			if (faces[face].corners.size() == triangle)
				for (size_t i = 0; i < triangle; i++)
					owners[EdgeOf(faces[face].corners[i].vertex, faces[face].corners[(i + 1) % triangle].vertex)].push_back(face);

		std::vector<bool> taken(faces.size(), false);
		std::vector<Face> joined{ };

		// The quad two triangles make across the edge from first's corner i,
		// if they lie flat, it is convex and the edge's corners agree.
		const auto join = [&](uint32_t first, size_t i, uint32_t second) -> std::optional<Face>
		{
			const std::vector<Corner>& a = faces[first].corners;
			const std::vector<Corner>& b = faces[second].corners;

			if (glm::dot(FaceNormal(first), FaceNormal(second)) < 1.0f - flat_epsilon)
				return std::nullopt;

			for (size_t j = 0; j < triangle; j++)
			{
				const Corner& from = b[j];
				const Corner& to = b[(j + 1) % triangle];

				// Wound the same way, the second has the edge backwards.
				if (from != a[(i + 1) % triangle] || to != a[i])
					continue;

				const Face quad{ { a[i], b[(j + 2) % triangle], a[(i + 1) % triangle], a[(i + 2) % triangle] } };
				const glm::vec3 normal = FaceNormal(first);

				for (size_t k = 0; k < quad.corners.size(); k++)
				{
					const glm::vec3& p = vertices[quad.corners[k].vertex];
					const glm::vec3& q = vertices[quad.corners[(k + 1) % quad.corners.size()].vertex];
					const glm::vec3& r = vertices[quad.corners[(k + 2) % quad.corners.size()].vertex];

					if (glm::dot(glm::cross(q - p, r - q), normal) <= 0.0f)
						return std::nullopt;
				};

				return quad;
			};

			return std::nullopt;
		};

		for (uint32_t face = 0; face < faces.size(); face++)
		{
			if (taken[face])
				continue;

			taken[face] = true;
			std::optional<Face> quad{ };

			for (size_t i = 0; !quad && faces[face].corners.size() == triangle && i < triangle; i++)
				for (const uint32_t other : owners[EdgeOf(faces[face].corners[i].vertex, faces[face].corners[(i + 1) % triangle].vertex)])
					if (!taken[other] && (quad = join(face, i, other)))
					{
						taken[other] = true;
						break;
					};

			joined.push_back(quad ? *quad : faces[face]);
		};

		faces = std::move(joined);
	};

	std::optional<EditableMesh::Hit> EditableMesh::Raycast(const glm::vec3& origin, const glm::vec3& direction) const
	{
		std::optional<Hit> nearest{ };

		for (uint32_t face = 0; face < faces.size(); face++)
		{
			const std::vector<Corner>& corners = faces[face].corners;

			for (size_t i = 2; i < corners.size(); i++)
			{
				// Moller and Trumbore's test, against both sides.
				const glm::vec3& a = vertices[corners[0].vertex];
				const glm::vec3 ab = vertices[corners[i - 1].vertex] - a;
				const glm::vec3 ac = vertices[corners[i].vertex] - a;

				const glm::vec3 p = glm::cross(direction, ac);
				const float determinant = glm::dot(ab, p);

				if (std::abs(determinant) < ray_epsilon)
					continue;

				const glm::vec3 t = origin - a;
				const float u = glm::dot(t, p) / determinant;

				if (u < 0.0f || u > 1.0f)
					continue;

				const glm::vec3 q = glm::cross(t, ab);
				const float v = glm::dot(direction, q) / determinant;

				if (v < 0.0f || u + v > 1.0f)
					continue;

				const float distance = glm::dot(ac, q) / determinant;

				if (distance > ray_epsilon && (!nearest || distance < nearest->distance))
					nearest = Hit{ face, distance };
			};
		};

		return nearest;
	};

	std::pair<glm::vec3, glm::vec3> EditableMesh::Bounds() const
	{
		if (vertices.empty())
			return { glm::vec3(0.0f), glm::vec3(0.0f) };

		glm::vec3 low = vertices.front();
		glm::vec3 high = vertices.front();

		for (const glm::vec3& vertex : vertices)
		{
			low = glm::min(low, vertex);
			high = glm::max(high, vertex);
		};

		return { low, high };
	};

	void EditableMesh::RemoveUnused()
	{
		std::vector<bool> used(vertices.size(), false);

		for (const Face& face : faces)
			for (const Corner& corner : face.corners)
				used[corner.vertex] = true;

		std::vector<uint32_t> renumbered(vertices.size(), 0);
		std::vector<glm::vec3> kept{ };

		for (size_t vertex = 0; vertex < vertices.size(); vertex++)
			if (used[vertex])
			{
				renumbered[vertex] = (uint32_t)kept.size();
				kept.push_back(vertices[vertex]);
			};

		for (Face& face : faces)
			for (Corner& corner : face.corners)
				corner.vertex = renumbered[corner.vertex];

		vertices = std::move(kept);
	};
};
