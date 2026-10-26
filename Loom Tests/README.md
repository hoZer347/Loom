# Loom Tests

Unit tests for the Loom codebase, using [doctest](https://github.com/doctest/doctest)
(vendored at `External Libraries/doctest/doctest.h`, same as every other
dependency here — no package manager).

One suite, built twice: by MSVC into a desktop executable, and by emcc into a
WebAssembly module run under node. Both builds compile the same test sources
against the same engine sources, so a change that works on one target and not
the other shows up as a failure rather than as a surprise in the browser.

## Running

```powershell
.\run-tests.ps1                          # local build: MSVC, x64, Debug
.\run-tests.ps1 -Configuration Release

.\run-web-tests.ps1                      # web build: emcc -> wasm, run under node
.\run-web-tests.ps1 -Configuration Release

.\run-browser-tests.ps1                  # the cases that need a real page, in headless Chrome
```

The first two scripts take `-NoBuild` to run the last binary as-is, and pass
`-TestArgs` straight through to doctest:

```powershell
.\run-tests.ps1 -TestArgs '-ts=GameObject'        # only the "GameObject" suite
.\run-web-tests.ps1 -TestArgs '-tc=*hierarchy*'   # only cases matching a pattern
```

All three exit with the test runner's exit code, so CI can gate on any of them.

A desktop run waits for any other, from any slot, to finish before it starts,
saying so on stderr while it waits, so two never share the `Screenshots` folder
or anything else on the machine. A crash or failed assert in it is written to
stderr, never put up as a dialog.

The `Performance` suite is timed against the clock and takes minutes under node, so
neither script runs it unless asked to. `daily-performance.ps1` runs it on both builds
once a day, in a slot brought up to `master`, and when it fails, starts Claude Code
there to find the slowdown and fix it, ending in the usual review window.
`daily-performance.ps1 -Register` schedules it, and its logs go to
`%LOCALAPPDATA%\Loom\Performance`:

```powershell
.\run-tests.ps1 -TestArgs '-ts=Performance','--no-skip'  # the Performance suite alone
```

In Visual Studio, set **Loom Tests** as the startup project and hit Ctrl+F5 for
the local build.

### What the web build needs

An installed emsdk — either `EMSDK` set, a copy at `C:\emsdk`, or the one
vendored at `External Libraries/emsdk` after `emsdk install latest &&
emsdk activate latest` — plus `node` on PATH. Nothing in the suite opens a
window or a GL context, so the module runs headless; no canvas, no browser.

### The browser suite

`run-browser-tests.ps1` builds `Browser/` together with the `Input` and
`Threading` suites, against the engine library Play Web uses. `Browser/run.mjs`
then runs it in headless Chrome (or Edge). When a case needs input it sets
`Module.loomRequest`, and run.mjs sends that move, click or key through Chrome's
own input pipeline, so it takes the same path a person's does. The page is
served cross-origin isolated, so the threads are the browser's own. Chrome runs
with a profile of its own and never shows a window. Beyond emsdk and node, it
needs Chrome or Edge installed.

## Suites

| Suite | Covers |
| --- | --- |
| `Platform` | What the two builds are allowed to differ on: pointer width, ID width, endianness, threading, how shaders load |
| `Macro Helpers` | `HAS_FUNCTION_*`, `HAS_VARIABLE_*`, `Str`, `VARIABLE_NAME` |
| `Engine` | Unique IDs (including across threads), the task queue and `DoTasks`, frame-loop defaults |
| `Threading` | Threads running at the same time, work queued from other threads running in order on the draining thread, objects built on other threads keeping their own fields |
| `Jobs` | Every chunk of a `For` given out once with its own range, whatever the count and over thousands of `For`s, a limit of one thread keeping them all on the caller, the pool's cores unmoved by any limit and covering the threads running, `WaitFor` returning at once or sleeping until another thread raises its counter |
| `LoomObject` | ID allocation, naming, `NameAndID`, the `GetByID` registry, deregistration on destruction |
| `Component` | Base callbacks as no-ops, virtual dispatch and destruction, type identity |
| `GameObject` | Deferred `Attach`, `GetComponent` by type, selective update/render/physics registration, children, `Destroy`, `DetachComponent`, thread inheritance, the transform's fields and matrix |
| `Scene` | Registration in `GetScenes`, root delegation, `Update`/`Render`/`Physics` walking the hierarchy |
| `Light` | Registration, `FindComponent` reaching it through the hierarchy, the shadow map's light-space transform |
| `Audio` | One-shots from `PlayAt` and `Play` until they play through or `StopAll`, holding while the scenes are paused, files read once until `Unload` or not at all, an `AudioSource` starting on its first update or on `Play`, held while its object is disabled and swapping clips, and which `AudioListener` listens |
| `Camera` | Registration, aiming down the transform or at a target, depth order and aspect, a camera looking straight down, toggling the target without turning the view |
| `Mesh` | Geometry and draw-type defaults, and that rendering without a material or shader stops before it touches GL |
| `Material` | Defaults, and being found by a `Mesh` on the same GameObject |
| `Shader` | Source loading failures on the local build (the web build fetches over HTTP instead, so those cases are skipped there) |
| `Texture` | The type exists; it has no state yet |
| `Collider` | Box and sphere colliders by name and through `GetComponent<Collider>`, where rays meet them under any transform, misses from inside or past `maxDistance`, bounds and wireframes |
| `ColliderTree` | A scene's colliders found once a physics step syncs them and where it left them, resized ones from the next step, nearest hit and every hit, dropping out when disabled, detached, destroyed or moved to another scene, `Physics::Raycast` over every scene, and a tree of hundreds staying shallow and agreeing with testing every collider through moves, removals and reinsertions |
| `MeshCollider` | Hitting a mesh's triangles from either side with the normal toward the ray, under any transform, from triangles, strips and fans but not points or lines, through a mesh's indices, nothing without a Mesh, catching up with edited vertices at the next step or at once when asked directly, no wireframe, and thousands of triangles agreeing with testing each one |
| `Contacts` | An edge resting across an edge pushed straight up, off a box mirrored by its scale as off a plain one |
| `KeySet` | Keys in once and out again through tens of thousands put in and taken out in mixed order, agreeing with `std::set`, and `EraseIf` taking out just what it is told |
| `Rigidbody` | Falling under gravity, spheres and tumbling boxes coming to rest on boxes and meshes, a stack standing, bounce, an elastic exchange, friction stopping a slide, a kinematic body pushing, a fast small ball unable to pass through a thin box or a mesh, nor one sent suddenly fast by a velocity or a force, both sides told of a collision's start, stay and end with normals out of the other side, triggers passed through and told, sleeping, and waking when struck, touched by a kinematic body or left unsupported, a kinematic body keeping the turn its script gives it, every force and torque mode, a collider attached after its Rigidbody, a body under a moved and turned parent, a pile ending up the same on one thread as on many |
| `Performance` | Skipped unless asked for with `--no-skip`, and run once a day by `daily-performance.ps1`: twenty thousand colliders, how long a physics step takes, still and with some moved, and a raycast through the tree against testing every collider, which it must beat twentyfold; a raycast among sixteen times the colliders costing at most six times as much, timed as the quickest of several batches. Reports its timings |
| `PhysicsPiles` | Two hundred bodies dropped into a pit all staying inside it and settling |
| `Benchmark` | Skipped unless asked for with `--no-skip`: the Crowd scene timed step by step as `Benchmarks/Unity Crowd` times it in Unity. `run-physics-benchmark.ps1` runs both in Release and sets them side by side |
| `ColliderGizmos` | The colliders under a selection drawn in one go over the view, and nothing drawn or compiled without an enabled one or for a MeshCollider |
| `Input` | Button edges: down for one frame, held, up for one frame, a click inside one frame, buttons GLFW does not have; cursor and screen fields |
| `SpeedManager` | `ClampMagnitude` and `MoveTowards`: clamping, no overshoot, zero-length input |
| `Browser` | Run by `run-browser-tests.ps1` only: real mouse moves, buttons and the spacebar reaching `Input`, GLFW and `DialogueInput`; the page being cross-origin isolated |
| `SceneHistory` | The editor's undo and redo: exact text both ways, redo cleared by a new edit, unrecorded edits, several scenes, closed scenes, the step cap, guids and references coming back, scene order |
| `EditCommands` | The editor's Ctrl+Z/Y/X/C/V and Delete read from real ImGui key events, a text box keeping them, held keys not repeating, which object can be taken and where a paste lands, cut/copy/paste through undo |
| `ProjectAssets` | Create > Script writing one self-registering header in the project's namespace, refusing a taken, registered or non-C++ name |
| `AxisGui` | One axis-coloured stripe over each of the first three drag boxes, nothing else added beside ImGui's own `DragScalarN`, nothing drawn in a collapsed window |
| `Render` | Desktop only: a frame read back at the window's size, top row first, a whole `renderFrame` drawing a scene, and a screenshot saved as a PNG of that size |

## How it is wired up

`Loom Tests.vcxproj` compiles the engine's `.cpp` files directly into the test
binary — by wildcard, `..\Loom WASM\*.cpp` plus `Utilities\*.cpp` — rather than
linking `Loom WASM.lib`. Two reasons: the suite keeps building when other parts
of that project are mid-refactor, and the wildcard means a new engine source is
picked up without anyone remembering to add it here.

`run-web-tests.ps1` reads that same list back out of the `.vcxproj` and hands it
to emcc, so the two builds cannot drift apart. ImGui is linked as a library on
the desktop and compiled from source for the web, since there is no prebuilt
wasm artefact.

Only `LoomTests::RenderFixture` constructs an `Engine`. Everything else runs
with no window and no GL context, which is what makes the suite runnable in CI
and under node.

### Rendering

A case that needs to see what is drawn includes `Render Support.h` and uses
`TEST_CASE_FIXTURE(LoomTests::RenderFixture, ...)`. The fixture opens the
`Engine` on a hidden window, `DEFAULT_SIZE` pixels across, with vsync and its
GUI off, and closes it when the case ends. `Frame()` runs a whole
`renderFrame` and `Draw(...)` clears the window and runs what it is given;
both hand back a `LoomTests::Image`. `At(x, y)` reads a pixel from the top
left, and `Save(name)` writes `<name>.png` to `Screenshots` beside the
executable, which `main` empties as a run starts. A demo's review can show
them with `review.ps1 -ShowImages`.

The fixture takes a `Loom::Backend` after the size, OpenGL unless told
otherwise, so a case can draw the same scene on Vulkan through a fixture of its
own that passes `Loom::Backend::Vulkan`. Those cases need a Vulkan driver on
the machine running them.

These cases are desktop only, inside `#ifndef __EMSCRIPTEN__`, since node has
no GL.

### The task queue

`GameObject`, `Scene` and `LoomObject` defer their real work through
`Engine::QueueTask`, which the engine drains once a frame from `renderFrame`.
Tests have no frame loop, so they call `LoomTests::Pump()` (`Engine::DoTasks`)
instead. **Nothing an engine object promises is observable until that runs** —
`Attach` hands back a component the GameObject does not own yet, `AddChild`
returns a child not yet in `GetChildren()`.

Every case that builds an engine object derives from `LoomTests::EngineFixture`,
which drains on the way in and on the way out. Construct, then `Pump()`
immediately: a `LoomObject` queues its own registration with a captured `this`,
and letting that fire after the object has gone runs it against freed memory.

## Adding a test

1. Drop a `<Thing> Tests.cpp` next to the others and add a `<ClCompile>` entry
   for it in `Loom Tests.vcxproj` (and `.vcxproj.filters`). The web build picks
   it up from there automatically.
2. `#include "doctest.h"` and `"Test Support.h"`, then wrap cases in a
   `TEST_SUITE("<Thing>")`.
3. Use `TEST_CASE_FIXTURE(LoomTests::EngineFixture, ...)` for anything touching
   an engine object.
4. Guard anything platform-specific with `#ifdef __EMSCRIPTEN__` and say why.
5. `main.cpp` is the only file that defines doctest's implementation — do not
   define `DOCTEST_CONFIG_IMPLEMENT*` anywhere else.
6. A passing run prints only doctest's summary. A case that makes the engine or
   editor complain on purpose holds a `LoomTests::Captured` on `std::cerr` and
   checks the complaint with `Says`. Before `main`, while the component types
   register, `main.cpp` holds back only the ComponentRegistry suite's expected
   `STARTUP_CLASH` line, so anything else said then still shows. A case that
   runs by default reports measurements with `INFO`, which shows only on a
   failure; the skipped `Performance` and `Benchmark` cases keep `MESSAGE`,
   since their numbers are the point.

## Not covered

- Anything needing a live GL context, beyond what the `Render` suite and
  `RenderFixture` cases draw: `Start`, `Light::RenderShadowMap`, the light's
  `Apply`, `Engine::CaptureFrame`, `Scene::Render` setting and clearing
  `Light::current` (a light renders its shadow map).
  `Loom Editor.exe Demos/Shadows --screenshot <file>` is the check for those,
  with the OpenGL debug output on stderr. A clean run prints one GL line,
  "Pixel transfer is synchronized with 3D rendering", which is the screenshot's
  own readback; anything else is a regression. The editor is a Windows program,
  so a shell does not wait for it: run it with `Start-Process -Wait -PassThru`,
  or through a pipe, to wait and read its exit code.
- Anything needing an ImGui frame: every `OnGui` and `GameObject::Gui`.
- `Loom WASM/Utilities` (the hoZer state machines, dialogue, attribute and
  sprite layer). It is compiled and linked in, so it has to build, but nothing
  exercises it yet: it was landing while this suite was being written, and it is
  what replaced the old `Loom::State` stack machine, whose suite was removed
  when `Loom WASM/State.h` and `State.cpp` were deleted.
- `Loom SQL`, `Loom Demos`, `Space Explorers`, and `Loom Editor` beyond
  `SceneHistory.cpp` and `EditCommands.cpp`, the two of its sources that need
  no window and are compiled into the suite beside the engine's.
