using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

// The Profiling demo's Crowd scene, built in Unity to time against: a walled
// pit, three ring MeshColliders, fifteen hundred boxes and balls dropped from
// a jittered grid, and a row of 160 rays cast through the camera every step.
// Every number matches Loom's Benchmark suite, and so does every body, drawn
// from the same generator in the same order.
// Run headless:
//   Unity -batchmode -nographics -projectPath <this> -executeMethod CrowdBenchmark.Run -quit -benchmarkOutput <file>
// Loom's own run of the same scene is the Benchmark suite in Loom Tests;
// run-physics-benchmark.ps1 runs both and sets them side by side.
public static class CrowdBenchmark
{
	const int Count = 1500;
	const int Steps = 1200;
	const float Step = 1.0f / 60.0f;
	const uint Seed = 1;

	const float Half = 0.5f;
	const float FullTurn = 360.0f;
	const double MicrosecondsPerMillisecond = 1000.0;

	// The drop: a layer at a time, each body in its own cell of a grid.
	const float LeastSize = 0.4f;
	const float MostSize = 1.4f;
	// Room for the largest body, turned any way, in each cell of the drop.
	const float Room = 1.8f;
	const float Spacing = MostSize * Room;
	const float Spread = 10.0f;
	const float Above = 12.0f;
	const float Drag = 0.5f;
	const int Kinds = 2;

	// The pit: a floor and four walls around a square Inside either side of
	// the middle, its floor's top at y = 0.
	const float Inside = 12.0f;
	const float Wall = 4.0f;
	const float WallHeight = 14.0f;

	// The demo scene's three rings.
	static readonly (Vector3 position, Vector3 rotation)[] Rings =
	{
		(new Vector3(-4.0f, 5.0f, -3.0f), new Vector3(70.0f, 20.0f, 0.0f)),
		(new Vector3(4.0f, 4.0f, 3.0f), new Vector3(75.0f, -30.0f, 0.0f)),
		(new Vector3(0.0f, 8.0f, 1.0f), new Vector3(60.0f, 90.0f, 0.0f)),
	};

	// The torus every ring is: around Z, half a unit across before scaling.
	const int RingSegments = 48;
	const int TubeSegments = 16;
	const float RingRadius = 0.35f;
	const float TubeRadius = 0.15f;
	const float RingScale = 7.0f;

	// The scanner: one row of the camera's grid a step.
	const int Columns = 160;
	const int Rows = 90;
	const float FieldOfView = 45.0f;
	const float Aspect = 16.0f / 9.0f;
	static readonly Vector3 Eye = new Vector3(0.0f, 42.0f, 34.0f);
	static readonly Vector3 Target = new Vector3(0.0f, 3.0f, 0.0f);

	// The steps landing is timed over, and the last ones, by when it has settled.
	const int Landing = 360;
	const int Settled = 60;

	public static void Run()
	{
		EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);

		Physics.simulationMode = SimulationMode.Script;
		Physics.gravity = new Vector3(0.0f, -9.81f, 0.0f);

		float across = (Inside + Wall) / Half;
		float wallMiddle = Inside + Wall * Half;
		float wallUp = WallHeight * Half;

		Box("Floor", new Vector3(0.0f, -Wall * Half, 0.0f), new Vector3(across, Wall, across));
		Box("Wall North", new Vector3(0.0f, wallUp, -wallMiddle), new Vector3(across, WallHeight, Wall));
		Box("Wall South", new Vector3(0.0f, wallUp, wallMiddle), new Vector3(across, WallHeight, Wall));
		Box("Wall East", new Vector3(wallMiddle, wallUp, 0.0f), new Vector3(Wall, WallHeight, Inside / Half));
		Box("Wall West", new Vector3(-wallMiddle, wallUp, 0.0f), new Vector3(Wall, WallHeight, Inside / Half));

		Mesh torus = Torus();

		foreach ((Vector3 position, Vector3 rotation) in Rings)
			Ring(torus, position, rotation);

		List<Rigidbody> bodies = Drop();
		Physics.SyncTransforms();

		List<Ray> rays = Scanner();

		Stopwatch clock = new Stopwatch();
		double peak = 0.0, total = 0.0, landing = 0.0, settled = 0.0, rayTime = 0.0;
		int rayCount = 0;

		for (int step = 0; step < Steps; step++)
		{
			clock.Restart();
			Physics.Simulate(Step);
			double milliseconds = clock.Elapsed.TotalMilliseconds;

			peak = Math.Max(peak, milliseconds);
			total += milliseconds;

			if (step < Landing)
				landing += milliseconds;

			if (step >= Steps - Settled)
				settled += milliseconds;

			int row = step % Rows;
			clock.Restart();

			for (int column = 0; column < Columns; column++)
				Physics.Raycast(rays[row * Columns + column], out RaycastHit hit);

			rayTime += clock.Elapsed.TotalMilliseconds;
			rayCount += Columns;
		}

		int awake = 0, escaped = 0;

		foreach (Rigidbody body in bodies)
		{
			if (!body.IsSleeping())
				awake++;

			Vector3 at = body.position;

			if (at.y < 0.0f || Mathf.Abs(at.x) > Inside || Mathf.Abs(at.z) > Inside)
				escaped++;
		}

		string[] lines =
		{
			"engine Unity " + Application.unityVersion,
			"bodies " + Count,
			"steps " + Steps,
			"peak_step_ms " + Format(peak),
			"mean_step_ms " + Format(total / Steps),
			"landing_step_ms " + Format(landing / Landing),
			"settled_step_ms " + Format(settled / Settled),
			"ray_us " + Format(rayTime * MicrosecondsPerMillisecond / rayCount),
			"awake " + awake,
			"escaped " + escaped,
		};

		foreach (string line in lines)
			UnityEngine.Debug.Log("BENCH " + line);

		string output = Argument("-benchmarkOutput");

		if (output != null)
			File.WriteAllLines(output, lines);
	}

	static string Format(double value)
	{
		return value.ToString("0.###", CultureInfo.InvariantCulture);
	}

	static string Argument(string name)
	{
		string[] arguments = Environment.GetCommandLineArgs();

		for (int i = 0; i + 1 < arguments.Length; i++)
			if (arguments[i] == name)
				return arguments[i + 1];

		return null;
	}

	static void Box(string name, Vector3 position, Vector3 size)
	{
		GameObject box = new GameObject(name);
		box.transform.position = position;
		box.AddComponent<BoxCollider>().size = size;
	}

	static void Ring(Mesh torus, Vector3 position, Vector3 rotation)
	{
		GameObject ring = new GameObject("Ring");
		ring.transform.position = position;
		ring.transform.rotation = Quaternion.Euler(rotation);
		ring.transform.localScale = Vector3.one * RingScale;
		ring.AddComponent<MeshCollider>().sharedMesh = torus;
	}

	static Mesh Torus()
	{
		List<Vector3> vertices = new List<Vector3>();
		List<int> triangles = new List<int>();

		Vector3 At(int ring, int tube)
		{
			float around = Mathf.PI / Half * ring / RingSegments;
			float turn = Mathf.PI / Half * tube / TubeSegments;
			Vector3 outward = new Vector3(Mathf.Cos(around), Mathf.Sin(around), 0.0f);

			return outward * RingRadius + (outward * Mathf.Cos(turn) + new Vector3(0.0f, 0.0f, Mathf.Sin(turn))) * TubeRadius;
		}

		for (int ring = 0; ring < RingSegments; ring++)
			for (int tube = 0; tube < TubeSegments; tube++)
			{
				int first = vertices.Count;

				vertices.Add(At(ring, tube));
				vertices.Add(At(ring + 1, tube));
				vertices.Add(At(ring + 1, tube + 1));
				vertices.Add(At(ring, tube + 1));

				triangles.AddRange(new[] { first, first + 1, first + 2, first, first + 2, first + 3 });
			}

		Mesh mesh = new Mesh();
		mesh.SetVertices(vertices);
		mesh.SetTriangles(triangles, 0);

		return mesh;
	}

	// Xorshift32, which Loom's Benchmark suite writes out the same way, so
	// both engines drop the same bodies.
	struct Xorshift
	{
		// A float takes the top 24 bits, all its mantissa holds.
		const int DroppedBits = 8;
		const float PerStep = 1.0f / (1 << 24);

		const int ShiftA = 13;
		const int ShiftB = 17;
		const int ShiftC = 5;

		uint state;

		public Xorshift(uint seed) { state = seed; }

		public float Between(float least, float most)
		{
			state ^= state << ShiftA;
			state ^= state >> ShiftB;
			state ^= state << ShiftC;

			return least + (state >> DroppedBits) * PerStep * (most - least);
		}
	}

	static List<Rigidbody> Drop()
	{
		Xorshift random = new Xorshift(Seed);

		int perRow = Math.Max(1, (int)(Spread / Half / Spacing));
		int perLayer = perRow * perRow;
		float jitter = (Spacing - MostSize) * Half;

		List<Rigidbody> bodies = new List<Rigidbody>();

		for (int i = 0; i < Count; i++)
		{
			int cell = i % perLayer;
			Vector3 slot = new Vector3(
				(cell % perRow - (perRow - 1) * Half) * Spacing,
				Above + (i / perLayer) * Spacing,
				(cell / perRow - (perRow - 1) * Half) * Spacing);

			// One at a time, in the order Loom's side draws them.
			float offX = random.Between(-jitter, jitter);
			float offY = random.Between(-jitter, jitter);
			float offZ = random.Between(-jitter, jitter);
			float turnX = random.Between(0.0f, FullTurn);
			float turnY = random.Between(0.0f, FullTurn);
			float turnZ = random.Between(0.0f, FullTurn);
			float size = random.Between(LeastSize, MostSize);

			GameObject body = new GameObject("Body " + i);
			body.transform.position = slot + new Vector3(offX, offY, offZ);
			body.transform.rotation = Quaternion.Euler(turnX, turnY, turnZ);
			body.transform.localScale = Vector3.one * size;

			if (i % Kinds == 0)
				body.AddComponent<BoxCollider>();
			else
				body.AddComponent<SphereCollider>();

			Rigidbody rigidbody = body.AddComponent<Rigidbody>();
			rigidbody.linearDamping = Drag;
			bodies.Add(rigidbody);
		}

		return bodies;
	}

	static List<Ray> Scanner()
	{
		Vector3 forward = (Target - Eye).normalized;
		Vector3 right = Vector3.Cross(forward, Vector3.up).normalized;
		Vector3 up = Vector3.Cross(right, forward);

		float tall = Mathf.Tan(FieldOfView * Half * Mathf.Deg2Rad);
		float wide = tall * Aspect;

		List<Ray> rays = new List<Ray>();

		for (int row = 0; row < Rows; row++)
			for (int column = 0; column < Columns; column++)
			{
				float x = (column + Half) / (Columns * Half) - 1.0f;
				float y = 1.0f - (row + Half) / (Rows * Half);

				rays.Add(new Ray(Eye, (forward + right * (x * wide) + up * (y * tall)).normalized));
			}

		return rays;
	}
}
