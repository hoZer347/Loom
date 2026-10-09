using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

// The Raycasts demo's Crowd scene, built in Unity to time against: a walled
// pit, three ring MeshColliders, fifteen hundred boxes and balls dropped from
// a jittered grid, and a row of 160 rays cast through the camera every step.
// Run headless:
//   Unity -batchmode -nographics -projectPath <this> -executeMethod CrowdBenchmark.Run -quit -benchmarkOutput <file>
// Loom's own run of the same scene is the Benchmark suite in Loom Tests;
// run-physics-benchmark.ps1 runs both and sets them side by side.
public static class CrowdBenchmark
{
	const int Count = 1500;
	const int Steps = 1200;
	const float Step = 1.0f / 60.0f;
	const int Seed = 1;

	// The drop: a layer at a time, each body in its own cell of a grid.
	const float LeastSize = 0.4f;
	const float MostSize = 1.4f;
	const float Spacing = MostSize * 1.8f;
	const float Spread = 10.0f;
	const float Above = 12.0f;
	const float Drag = 0.5f;

	// The pit's inside, for counting what got out.
	const float Inside = 12.0f;

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

		Box("Floor", new Vector3(0.0f, -2.0f, 0.0f), new Vector3(32.0f, 4.0f, 32.0f));
		Box("Wall North", new Vector3(0.0f, 7.0f, -14.0f), new Vector3(32.0f, 14.0f, 4.0f));
		Box("Wall South", new Vector3(0.0f, 7.0f, 14.0f), new Vector3(32.0f, 14.0f, 4.0f));
		Box("Wall East", new Vector3(14.0f, 7.0f, 0.0f), new Vector3(4.0f, 14.0f, 24.0f));
		Box("Wall West", new Vector3(-14.0f, 7.0f, 0.0f), new Vector3(4.0f, 14.0f, 24.0f));

		Mesh torus = Torus();
		Ring(torus, new Vector3(-4.0f, 5.0f, -3.0f), new Vector3(70.0f, 20.0f, 0.0f));
		Ring(torus, new Vector3(4.0f, 4.0f, 3.0f), new Vector3(75.0f, -30.0f, 0.0f));
		Ring(torus, new Vector3(0.0f, 8.0f, 1.0f), new Vector3(60.0f, 90.0f, 0.0f));

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
			"ray_us " + Format(rayTime * 1000.0 / rayCount),
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
			float around = 2.0f * Mathf.PI * ring / RingSegments;
			float turn = 2.0f * Mathf.PI * tube / TubeSegments;
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

	static List<Rigidbody> Drop()
	{
		System.Random random = new System.Random(Seed);

		float Between(float least, float most)
		{
			return least + (float)random.NextDouble() * (most - least);
		}

		int perRow = Math.Max(1, (int)(2.0f * Spread / Spacing));
		int perLayer = perRow * perRow;
		float jitter = (Spacing - MostSize) * 0.5f;

		List<Rigidbody> bodies = new List<Rigidbody>();

		for (int i = 0; i < Count; i++)
		{
			int cell = i % perLayer;
			Vector3 slot = new Vector3(
				(cell % perRow - (perRow - 1) * 0.5f) * Spacing,
				Above + (i / perLayer) * Spacing,
				(cell / perRow - (perRow - 1) * 0.5f) * Spacing);

			GameObject body = new GameObject("Body " + i);
			body.transform.position = slot + new Vector3(Between(-jitter, jitter), Between(-jitter, jitter), Between(-jitter, jitter));
			body.transform.rotation = Quaternion.Euler(Between(0.0f, 360.0f), Between(0.0f, 360.0f), Between(0.0f, 360.0f));
			body.transform.localScale = Vector3.one * Between(LeastSize, MostSize);

			if (i % 2 == 0)
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

		float tall = Mathf.Tan(FieldOfView * 0.5f * Mathf.Deg2Rad);
		float wide = tall * Aspect;

		List<Ray> rays = new List<Ray>();

		for (int row = 0; row < Rows; row++)
			for (int column = 0; column < Columns; column++)
			{
				float x = 2.0f * (column + 0.5f) / Columns - 1.0f;
				float y = 1.0f - 2.0f * (row + 0.5f) / Rows;

				rays.Add(new Ray(Eye, (forward + right * (x * wide) + up * (y * tall)).normalized));
			}

		return rays;
	}
}
