#pragma once

#include "Renderer.h"
#include "UVLayer.h"

#include "glm/glm.hpp"

#include <cstdint>
#include <optional>
#include <set>
#include <utility>
#include <vector>


namespace Loom
{
	/**
	* Loom::EditableMesh
	* - A Mesh's triangles as the Model panel edits them: corners that sat at
	*   the same place share one vertex, so moving it moves every face it is
	*   part of
	* - A face has any number of corners and goes back to triangles as a fan,
	*   so a quad stays one face through moves and extrusions
	* - Each corner keeps its own UV, normal, colour and UV layers' words, as the Mesh had them. A face
	*   whose shape changes takes its own flat normal at every corner
	*/
	struct EditableMesh final
	{
		struct Corner
		{
			uint32_t vertex = 0;
			glm::vec2 uv{ 0.0f };
			glm::vec3 normal{ 0.0f };
			glm::vec4 color{ 1.0f };

			// Its words of each of EditableMesh::layers in turn.
			std::vector<uint32_t> layers;

			bool operator==(const Corner&) const = default;
		};

		struct Face
		{
			std::vector<Corner> corners;

			bool operator==(const Face&) const = default;
		};

		// The two vertices, the lower first, so an edge is the same whichever
		// face it is read from.
		using Edge = std::pair<uint32_t, uint32_t>;

		static Edge EdgeOf(uint32_t a, uint32_t b);

		// Unindexed triangles, as Mesh keeps them: three floats a vertex for
		// positions and normals, two for UVs and four for colours. UVs,
		// normals and colours count only when there are as many of them as
		// vertices, and so do the UV layers.
		static EditableMesh FromTriangles(const std::vector<float>& positions, const std::vector<float>& uvs, const std::vector<float>& normals, const std::vector<float>& colors, const std::vector<UVLayer>& layers = { });

		// Back to Mesh's arrays. UVs, normals and colours stay empty for a
		// mesh that had none.
		void ToTriangles(std::vector<float>& positions, std::vector<float>& uvs, std::vector<float>& normals, std::vector<float>& colors) const;

		// The UV layers, a vertex for each ToTriangles gives.
		std::vector<UVLayer> ToUVLayers() const;

		// Centred on the origin, with flat normals and each face's UVs spanning
		// the texture.
		static EditableMesh Cube(float size);
		static EditableMesh Plane(float size);
		static EditableMesh Cylinder(float radius, float height, int segments);
		static EditableMesh Cone(float radius, float height, int segments);
		static EditableMesh Sphere(float radius, int segments, int rings);

		// Unit length, by Newell's method so it holds for any flat polygon.
		// Zero for a face with no area.
		glm::vec3 FaceNormal(uint32_t face) const;
		glm::vec3 FaceCentre(uint32_t face) const;

		// Every edge once, in the order faces first reach them.
		std::vector<Edge> Edges() const;

		std::set<uint32_t> VerticesOfFaces(const std::set<uint32_t>& faces) const;
		std::set<uint32_t> VerticesOfEdges(const std::set<Edge>& edges) const;

		// Faces and edges whose every vertex is in the set.
		std::set<uint32_t> FacesWithin(const std::set<uint32_t>& vertices) const;
		std::set<Edge> EdgesWithin(const std::set<uint32_t>& vertices) const;

		// Moves the vertices by the matrix, and flattens the normals of every
		// face they are in.
		void Transform(const std::set<uint32_t>& vertices, const glm::mat4& matrix);

		// Lifts the faces off the rest of the mesh by distance along their
		// average normal, walling in the gap from where they were. The faces
		// keep their indices; the walls go on the end. False, with nothing
		// done, for none, or for faces facing every way so they have no
		// average, such as the whole of a closed shape.
		bool Extrude(const std::set<uint32_t>& faces, float distance);

		// The faces copied onto vertices of their own, which the indices
		// returned name.
		std::set<uint32_t> Duplicate(const std::set<uint32_t>& faces);

		// Each of the other's faces on the end, onto vertices of their own. The
		// indices of the faces added. Their corners keep the words of the
		// layers this mesh has too, and read zeros for the rest.
		std::set<uint32_t> Append(const EditableMesh& other);

		void DeleteFaces(const std::set<uint32_t>& faces);

		// Along with every face they are in.
		void DeleteVertices(const std::set<uint32_t>& vertices);
		void DeleteEdges(const std::set<Edge>& edges);

		// Turns the faces round, so they face the other way.
		void Flip(const std::set<uint32_t>& faces);

		// Joins each pair of triangles that lie flat together across an edge,
		// with the same UV, normal and colour on both sides of it, into a quad, as a
		// model file that split its quads in two had them.
		void JoinTriangles();

		// The nearest face the ray goes into from either side, and how far
		// along the ray it is, in lengths of direction.
		struct Hit
		{
			uint32_t face = 0;
			float distance = 0.0f;
		};

		std::optional<Hit> Raycast(const glm::vec3& origin, const glm::vec3& direction) const;

		// The smallest box round every vertex. Both corners zero for none.
		std::pair<glm::vec3, glm::vec3> Bounds() const;

		std::vector<glm::vec3> vertices;
		std::vector<Face> faces;

		bool hasUVs = false;
		bool hasNormals = false;
		bool hasColors = false;

		// The UV layers every corner holds words of.
		std::vector<UVLayout> layers;

		bool operator==(const EditableMesh&) const = default;

	private:
		void Flatten(uint32_t face);

		// Drops the vertices no face uses, numbering the rest down.
		void RemoveUnused();

		// A face from vertices given in order, with flat normals and the UVs given.
		uint32_t AddFace(const std::vector<uint32_t>& corners, const std::vector<glm::vec2>& uvs);

		// The face on the end, with flat normals.
		uint32_t AddFace(Face face);
	};
};
