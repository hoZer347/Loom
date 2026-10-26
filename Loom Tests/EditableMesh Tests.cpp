#include "doctest.h"

#include "EditableMesh.h"

#include "glm/gtc/matrix_transform.hpp"

#include <cmath>
#include <vector>


namespace
{
	using Loom::EditableMesh;

	constexpr float TOLERANCE = 1e-5f;

	constexpr int SEGMENTS = 8;
	constexpr int RINGS = 4;

	// Two triangles making the unit square in the XY plane, facing +Z, the
	// way an imported quad arrives.
	const std::vector<float> SQUARE = {
		0.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,  1.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f,  1.0f, 1.0f, 0.0f,  0.0f, 1.0f, 0.0f,
	};

	bool Near(const glm::vec3& a, const glm::vec3& b)
	{
		return glm::length(a - b) < TOLERANCE;
	};

	// Whether every face faces away from the middle, as a closed shape round
	// the origin should.
	bool FacesOut(const EditableMesh& mesh)
	{
		for (uint32_t face = 0; face < mesh.faces.size(); face++)
			if (glm::dot(mesh.FaceNormal(face), mesh.FaceCentre(face)) <= 0.0f)
				return false;

		return true;
	};

	// Whether every edge is shared by exactly two faces, so the shape has no
	// holes.
	bool Closed(const EditableMesh& mesh)
	{
		for (const EditableMesh::Edge& edge : mesh.Edges())
		{
			int uses = 0;

			for (const EditableMesh::Face& face : mesh.faces)
				for (size_t i = 0; i < face.corners.size(); i++)
					uses += EditableMesh::EdgeOf(face.corners[i].vertex, face.corners[(i + 1) % face.corners.size()].vertex) == edge;

			if (uses != 2)
				return false;
		};

		return true;
	};
};

TEST_SUITE("EditableMesh")
{
	TEST_CASE("EditableMesh welds the corners of triangles that meet")
	{
		const EditableMesh mesh = EditableMesh::FromTriangles(SQUARE, { }, { }, { });

		CHECK(mesh.vertices.size() == 4);
		CHECK(mesh.faces.size() == 2);
		CHECK(mesh.Edges().size() == 5);
		CHECK_FALSE(mesh.hasUVs);
		CHECK_FALSE(mesh.hasNormals);
	};

	TEST_CASE("EditableMesh gives back the triangles it was made from")
	{
		std::vector<float> uvs{ };
		std::vector<float> normals{ };

		for (size_t i = 0; i < SQUARE.size() / 3; i++)
		{
			uvs.insert(uvs.end(), { SQUARE[i * 3], SQUARE[i * 3 + 1] });
			normals.insert(normals.end(), { 0.0f, 0.0f, 1.0f });
		};

		const EditableMesh mesh = EditableMesh::FromTriangles(SQUARE, uvs, normals, { });

		std::vector<float> positions_out{ };
		std::vector<float> uvs_out{ };
		std::vector<float> normals_out{ };
		std::vector<float> colors_out{ };
		mesh.ToTriangles(positions_out, uvs_out, normals_out, colors_out);

		CHECK(positions_out == SQUARE);
		CHECK(uvs_out == uvs);
		CHECK(normals_out == normals);
	};

	TEST_CASE("EditableMesh gives back the colours of the triangles it was made from, and none it was not given")
	{
		const std::vector<float> colors = {
			1.0f, 0.0f, 0.0f, 1.0f,  0.0f, 1.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f, 1.0f,
			1.0f, 0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f, 1.0f,  1.0f, 1.0f, 1.0f, 0.5f,
		};

		std::vector<float> positions{ };
		std::vector<float> uvs{ };
		std::vector<float> normals{ };
		std::vector<float> colors_out{ };

		const EditableMesh coloured = EditableMesh::FromTriangles(SQUARE, { }, { }, colors);
		CHECK(coloured.hasColors);
		coloured.ToTriangles(positions, uvs, normals, colors_out);
		CHECK(positions == SQUARE);
		CHECK(colors_out == colors);

		const EditableMesh short_of = EditableMesh::FromTriangles(SQUARE, { }, { }, { 1.0f, 0.0f, 0.0f, 1.0f });
		CHECK_FALSE(short_of.hasColors);
		short_of.ToTriangles(positions, uvs, normals, colors_out);
		CHECK(colors_out.empty());
	};

	TEST_CASE("EditableMesh ignores UVs and normals that do not cover every vertex")
	{
		const EditableMesh mesh = EditableMesh::FromTriangles(SQUARE, { 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { });

		CHECK_FALSE(mesh.hasUVs);
		CHECK_FALSE(mesh.hasNormals);
	};

	TEST_CASE("EditableMesh gives back the UV layers of the triangles it was made from, and none that do not cover every vertex")
	{
		constexpr uint32_t WEIGHT = 1;
		constexpr uint32_t TINT = 2;
		constexpr uint32_t SHORT_OF = 3;
		constexpr uint32_t TINT_WORDS = 3;
		constexpr size_t CORNERS = 6;

		Loom::UVLayer weight{ WEIGHT, 1 };
		Loom::UVLayer tint{ TINT, TINT_WORDS };

		for (uint32_t i = 0; i < CORNERS; i++)
		{
			weight.values.push_back(i);
			tint.values.insert(tint.values.end(), { i, i * 2, i * 3 });
		};

		const Loom::UVLayer short_of{ SHORT_OF, 1, { 0 } };

		const EditableMesh mesh = EditableMesh::FromTriangles(SQUARE, { }, { }, { }, { weight, short_of, tint });

		const std::vector<Loom::UVLayout> layouts = { { WEIGHT, 1 }, { TINT, TINT_WORDS } };
		CHECK(mesh.layers == layouts);

		const std::vector<Loom::UVLayer> layers = { weight, tint };
		CHECK(mesh.ToUVLayers() == layers);
	};

	TEST_CASE("EditableMesh keeps triangles whose corners hold different UV layer words apart")
	{
		constexpr size_t CORNERS = 6;

		Loom::UVLayer same{ 1, 1, std::vector<uint32_t>(CORNERS, 0) };
		EditableMesh joined = EditableMesh::FromTriangles(SQUARE, { }, { }, { }, { same });
		joined.JoinTriangles();
		CHECK(joined.faces.size() == 1);

		// The second triangle's copy of the shared diagonal holds other words.
		Loom::UVLayer seam = same;
		seam.values[3] = 1;
		seam.values[4] = 1;
		EditableMesh apart = EditableMesh::FromTriangles(SQUARE, { }, { }, { }, { seam });
		apart.JoinTriangles();
		CHECK(apart.faces.size() == 2);
	};

	TEST_CASE("EditableMesh's primitives are closed and face outwards")
	{
		constexpr float SIZE = 2.0f;

		for (const EditableMesh& mesh : {
			EditableMesh::Cube(SIZE),
			EditableMesh::Cylinder(SIZE, SIZE, SEGMENTS),
			EditableMesh::Cone(SIZE, SIZE, SEGMENTS),
			EditableMesh::Sphere(SIZE, SEGMENTS, RINGS) })
		{
			CHECK(FacesOut(mesh));
			CHECK(Closed(mesh));

			for (uint32_t face = 0; face < mesh.faces.size(); face++)
				for (const EditableMesh::Corner& corner : mesh.faces[face].corners)
					CHECK(Near(corner.normal, mesh.FaceNormal(face)));
		};

		const EditableMesh cube = EditableMesh::Cube(SIZE);
		CHECK(cube.vertices.size() == 8);
		CHECK(cube.faces.size() == 6);
		CHECK(Near(cube.Bounds().second, glm::vec3(SIZE / 2.0f)));

		const EditableMesh plane = EditableMesh::Plane(SIZE);
		CHECK(Near(plane.FaceNormal(0), glm::vec3(0.0f, 1.0f, 0.0f)));
	};

	TEST_CASE("EditableMesh moves a vertex in every face it is part of and flattens them")
	{
		EditableMesh mesh = EditableMesh::Cube(2.0f);
		const glm::vec3 lift(0.0f, 1.0f, 0.0f);

		// The +X +Y +Z corner.
		constexpr uint32_t CORNER = 7;

		mesh.Transform({ CORNER }, glm::translate(glm::mat4(1.0f), lift));

		CHECK(Near(mesh.vertices[CORNER], glm::vec3(1.0f, 2.0f, 1.0f)));

		std::vector<float> positions{ };
		std::vector<float> uvs{ };
		std::vector<float> normals{ };
		std::vector<float> colors{ };
		mesh.ToTriangles(positions, uvs, normals, colors);

		int moved = 0;

		for (size_t i = 0; i < positions.size(); i += 3)
			moved += Near(glm::vec3(positions[i], positions[i + 1], positions[i + 2]), glm::vec3(1.0f, 2.0f, 1.0f));

		// Three quads have the corner; split into fans they reach it four or more
		// times between them, and never from the old place.
		CHECK(moved >= 3);

		for (uint32_t face = 0; face < mesh.faces.size(); face++)
			CHECK(Near(mesh.faces[face].corners[0].normal, mesh.FaceNormal(face)));
	};

	TEST_CASE("EditableMesh extrudes a face of a cube into a closed taller shape")
	{
		EditableMesh mesh = EditableMesh::Cube(2.0f);

		// The +Y face.
		constexpr uint32_t TOP = 4;
		constexpr float DISTANCE = 1.0f;

		mesh.Extrude({ TOP }, DISTANCE);

		CHECK(mesh.faces.size() == 10);
		CHECK(mesh.vertices.size() == 12);
		CHECK(Closed(mesh));
		CHECK(Near(mesh.FaceCentre(TOP), glm::vec3(0.0f, 2.0f, 0.0f)));
		CHECK(Near(mesh.FaceNormal(TOP), glm::vec3(0.0f, 1.0f, 0.0f)));
		CHECK(Near(mesh.Bounds().second, glm::vec3(1.0f, 2.0f, 1.0f)));

		// Every wall faces out from the column's middle.
		for (uint32_t face = 6; face < mesh.faces.size(); face++)
		{
			const glm::vec3 centre = mesh.FaceCentre(face);
			CHECK(glm::dot(mesh.FaceNormal(face), glm::vec3(centre.x, 0.0f, centre.z)) > 0.0f);
		};
	};

	TEST_CASE("EditableMesh extrudes two neighbouring faces as one region")
	{
		EditableMesh mesh = EditableMesh::FromTriangles(SQUARE, { }, { }, { });

		mesh.Extrude({ 0, 1 }, 1.0f);

		// The two triangles and a wall for each of the square's four sides; the
		// diagonal they share gets none.
		CHECK(mesh.faces.size() == 6);
		CHECK(mesh.vertices.size() == 8);
		CHECK(Near(mesh.FaceCentre(0), glm::vec3(2.0f / 3.0f, 1.0f / 3.0f, 1.0f)));
	};

	TEST_CASE("EditableMesh's extruded walls take the colours of the edges they rise from")
	{
		constexpr size_t CORNERS = 6;
		const glm::vec4 red(1.0f, 0.0f, 0.0f, 1.0f);

		std::vector<float> colors{ };

		for (size_t i = 0; i < CORNERS; i++)
			colors.insert(colors.end(), { red.r, red.g, red.b, red.a });

		EditableMesh mesh = EditableMesh::FromTriangles(SQUARE, { }, { }, colors);
		mesh.Extrude({ 0, 1 }, 1.0f);

		REQUIRE(mesh.faces.size() == 6);

		for (const EditableMesh::Face& face : mesh.faces)
			for (const EditableMesh::Corner& corner : face.corners)
				CHECK(corner.color == red);
	};

	TEST_CASE("EditableMesh's extruded walls take the UV layer words of the edges they rise from")
	{
		constexpr uint32_t WORD = 7;
		constexpr size_t CORNERS = 6;

		EditableMesh mesh = EditableMesh::FromTriangles(SQUARE, { }, { }, { }, { { 1, 1, std::vector<uint32_t>(CORNERS, WORD) } });
		mesh.Extrude({ 0, 1 }, 1.0f);

		REQUIRE(mesh.faces.size() == 6);

		const std::vector<uint32_t> words = { WORD };

		for (const EditableMesh::Face& face : mesh.faces)
			for (const EditableMesh::Corner& corner : face.corners)
				CHECK(corner.layers == words);
	};

	TEST_CASE("EditableMesh deletes faces and the vertices only they used")
	{
		EditableMesh mesh = EditableMesh::Cube(2.0f);

		mesh.DeleteFaces({ 4 });
		CHECK(mesh.faces.size() == 5);
		CHECK(mesh.vertices.size() == 8);

		mesh.DeleteVertices({ 0 });
		CHECK(mesh.faces.size() == 2);
		CHECK(mesh.vertices.size() == 6);

		for (const EditableMesh::Face& face : mesh.faces)
			for (const EditableMesh::Corner& corner : face.corners)
				CHECK(corner.vertex < mesh.vertices.size());
	};

	TEST_CASE("EditableMesh deletes the faces on an edge")
	{
		EditableMesh mesh = EditableMesh::FromTriangles(SQUARE, { }, { }, { });

		// The diagonal the two triangles share.
		mesh.DeleteEdges({ EditableMesh::EdgeOf(0, 2) });

		CHECK(mesh.faces.empty());
		CHECK(mesh.vertices.empty());
	};

	TEST_CASE("EditableMesh duplicates and appends onto vertices of their own")
	{
		EditableMesh mesh = EditableMesh::Cube(2.0f);

		const std::set<uint32_t> copies = mesh.Duplicate({ 0, 1 });
		CHECK(copies == std::set<uint32_t>{ 6, 7 });
		CHECK(mesh.vertices.size() == 16);
		CHECK(mesh.VerticesOfFaces(copies).size() == 8);

		const std::set<uint32_t> added = mesh.Append(EditableMesh::Plane(1.0f));
		CHECK(added == std::set<uint32_t>{ 8 });
		CHECK(mesh.vertices.size() == 20);
	};

	TEST_CASE("EditableMesh keeps colours once either side of an append has them")
	{
		const std::vector<float> grey(SQUARE.size() / 3 * 4, 0.5f);
		const EditableMesh coloured = EditableMesh::FromTriangles(SQUARE, { }, { }, grey);

		EditableMesh empty{ };
		empty.Append(coloured);
		CHECK(empty.hasColors);

		EditableMesh cube = EditableMesh::Cube(2.0f);
		cube.Append(coloured);
		CHECK(cube.hasColors);
		CHECK(cube.faces.front().corners.front().color == glm::vec4(1.0f));
		CHECK(cube.faces.back().corners.front().color == glm::vec4(0.5f));

		EditableMesh square = coloured;
		square.Append(EditableMesh::Plane(1.0f));
		CHECK(square.hasColors);
	};

	TEST_CASE("EditableMesh keeps its own UV layers through an append, and the other's words of those it shares")
	{
		constexpr uint32_t SHARED = 1;
		constexpr uint32_t OURS = 2;
		constexpr uint32_t THEIRS = 3;
		constexpr uint32_t OUR_WORD = 5;
		constexpr uint32_t THEIR_WORD = 9;
		constexpr size_t CORNERS = 6;

		const std::vector<uint32_t> ours(CORNERS, OUR_WORD);
		const std::vector<uint32_t> theirs(CORNERS, THEIR_WORD);

		const EditableMesh other = EditableMesh::FromTriangles(SQUARE, { }, { }, { }, { { SHARED, 1, theirs }, { THEIRS, 1, theirs } });

		EditableMesh empty{ };
		empty.Append(other);
		CHECK(empty.layers == other.layers);

		EditableMesh mesh = EditableMesh::FromTriangles(SQUARE, { }, { }, { }, { { SHARED, 1, ours }, { OURS, 1, ours } });
		const std::vector<Loom::UVLayout> layouts = mesh.layers;
		mesh.Append(other);
		CHECK(mesh.layers == layouts);

		const std::vector<uint32_t> kept = { OUR_WORD, OUR_WORD };
		const std::vector<uint32_t> padded = { THEIR_WORD, 0 };
		CHECK(mesh.faces.front().corners.front().layers == kept);
		CHECK(mesh.faces.back().corners.front().layers == padded);

		EditableMesh cube = EditableMesh::Cube(2.0f);
		cube.Append(other);
		CHECK(cube.layers.empty());
		CHECK(cube.faces.back().corners.front().layers.empty());
	};

	TEST_CASE("EditableMesh flips faces round")
	{
		EditableMesh mesh = EditableMesh::Cube(2.0f);

		mesh.Flip({ 0 });

		CHECK(Near(mesh.FaceNormal(0), glm::vec3(0.0f, 0.0f, -1.0f)));
		CHECK(Near(mesh.faces[0].corners[0].normal, glm::vec3(0.0f, 0.0f, -1.0f)));
	};

	TEST_CASE("EditableMesh finds the nearest face a ray goes into")
	{
		const EditableMesh mesh = EditableMesh::Cube(2.0f);

		const std::optional<EditableMesh::Hit> front = mesh.Raycast(glm::vec3(0.2f, 0.3f, 5.0f), glm::vec3(0.0f, 0.0f, -1.0f));
		REQUIRE(front);
		CHECK(front->face == 0);
		CHECK(std::abs(front->distance - 4.0f) < TOLERANCE);

		CHECK_FALSE(mesh.Raycast(glm::vec3(3.0f, 0.0f, 5.0f), glm::vec3(0.0f, 0.0f, -1.0f)));
	};

	TEST_CASE("EditableMesh joins triangles that lie flat together into quads")
	{
		EditableMesh mesh = EditableMesh::FromTriangles(SQUARE, { }, { }, { });
		mesh.JoinTriangles();

		REQUIRE(mesh.faces.size() == 1);
		CHECK(mesh.faces[0].corners.size() == 4);
		CHECK(Near(mesh.FaceNormal(0), glm::vec3(0.0f, 0.0f, 1.0f)));

		std::vector<float> cube{ };
		std::vector<float> uvs{ };
		std::vector<float> normals{ };
		std::vector<float> colors{ };
		EditableMesh::Cube(2.0f).ToTriangles(cube, uvs, normals, colors);

		EditableMesh welded = EditableMesh::FromTriangles(cube, uvs, normals, { });
		welded.JoinTriangles();
		CHECK(welded.faces.size() == 6);
		CHECK(Closed(welded));
		CHECK(FacesOut(welded));
	};

	TEST_CASE("EditableMesh keeps triangles apart where a seam or a fold runs between them")
	{
		// The second triangle's UVs start again at the shared edge.
		std::vector<float> uvs{ 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.5f, 0.5f, 1.0f, 1.0f, 0.0f, 1.0f };

		EditableMesh seam = EditableMesh::FromTriangles(SQUARE, uvs, { }, { });
		seam.JoinTriangles();
		CHECK(seam.faces.size() == 2);

		std::vector<float> folded = SQUARE;
		folded.back() = 1.0f;

		EditableMesh fold = EditableMesh::FromTriangles(folded, { }, { }, { });
		fold.JoinTriangles();
		CHECK(fold.faces.size() == 2);
	};

	TEST_CASE("EditableMesh converts between vertices, edges and faces")
	{
		const EditableMesh mesh = EditableMesh::Cube(2.0f);

		const std::set<uint32_t> front = mesh.VerticesOfFaces({ 0 });
		CHECK(front == std::set<uint32_t>{ 4, 5, 6, 7 });
		CHECK(mesh.FacesWithin(front) == std::set<uint32_t>{ 0 });
		CHECK(mesh.EdgesWithin(front).size() == 4);
		CHECK(mesh.VerticesOfEdges(mesh.EdgesWithin(front)) == front);
	};
};
