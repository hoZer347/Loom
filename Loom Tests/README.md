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

The first two scripts take `-NoBuild` to run the last binary as-is, and pass anything
after `--` straight through to doctest:

```powershell
.\run-tests.ps1 -- -ts=GameObject       # only the "GameObject" suite
.\run-web-tests.ps1 -- -tc="*hierarchy*" # only cases matching a pattern
```

All three exit with the test runner's exit code, so CI can gate on any of them.

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
| `LoomObject` | ID allocation, naming, `NameAndID`, the `GetByID` registry, deregistration on destruction |
| `Component` | Base callbacks as no-ops, virtual dispatch and destruction, type identity |
| `GameObject` | Deferred `Attach`, `GetComponent` by type, selective update/render registration, children, `Destroy`, `DetachComponent`, thread inheritance, the transform's fields and matrix |
| `Scene` | Registration in `GetScenes`, root delegation, `Update`/`Render`/`Physics` walking the hierarchy |
| `Light` | Registration, `FindComponent` reaching it through the hierarchy, the shadow map's light-space transform |
| `Camera` | Registration, aiming down the transform or at a target, depth order and aspect, a camera looking straight down, toggling the target without turning the view |
| `Mesh` | Geometry and draw-type defaults, and that rendering without a material or shader stops before it touches GL |
| `Material` | Defaults, and being found by a `Mesh` on the same GameObject |
| `Shader` | Source loading failures on the local build (the web build fetches over HTTP instead, so those cases are skipped there) |
| `Texture` | The type exists; it has no state yet |
| `Collider` | Type-level only — see the note in the file |
| `Input` | Button edges: down for one frame, held, up for one frame, a click inside one frame, buttons GLFW does not have; cursor and screen fields |
| `SpeedManager` | `ClampMagnitude` and `MoveTowards`: clamping, no overshoot, zero-length input |
| `Browser` | Run by `run-browser-tests.ps1` only: real mouse moves, buttons and the spacebar reaching `Input`, GLFW and `DialogueInput`; the page being cross-origin isolated |
| `SceneHistory` | The editor's undo and redo: exact text both ways, redo cleared by a new edit, unrecorded edits, several scenes, closed scenes, the step cap, guids and references coming back, scene order |
| `EditCommands` | The editor's Ctrl+Z/Y/X/C/V and Delete read from real ImGui key events, a text box keeping them, held keys not repeating, which object can be taken and where a paste lands, cut/copy/paste through undo |
| `AxisGui` | One axis-coloured stripe over each of the first three drag boxes, nothing else added beside ImGui's own `DragScalarN`, nothing drawn in a collapsed window |

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

Nothing constructs an `Engine`, so no window and no GL context is ever created.
That is what makes the suite runnable in CI and under node.

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

## Not covered

- Anything needing a live GL context: `Engine`'s constructor, `Start`,
  `renderFrame`, shader compilation, `Mesh::OnRender` past its null guards,
  `Mesh::Draw`, `Light::RenderShadowMap`, `Apply` and `ApplyNone`,
  `Camera::Apply`, `Material::Apply`, `Engine::CaptureFrame`, `Scene::Render`
  setting and clearing `Light::current` (a light renders its shadow map).
  `Loom Editor.exe Demos/Shadows --screenshot <file>` is the check for those,
  with the OpenGL debug output on stderr. A clean run prints one GL line,
  "Pixel transfer is synchronized with 3D rendering", which is the screenshot's
  own readback; anything else is a regression.
- Anything needing an ImGui frame: every `OnGui` and `GameObject::Gui`.
- `Loom WASM/Utilities` (the hoZer state machines, dialogue, attribute and
  sprite layer). It is compiled and linked in, so it has to build, but nothing
  exercises it yet: it was landing while this suite was being written, and it is
  what replaced the old `Loom::State` stack machine, whose suite was removed
  when `Loom WASM/State.h` and `State.cpp` were deleted.
- `Physics::Collider`'s behaviour. Its constructor reads `m_gameObject` before
  `Attach` has assigned it, so constructing one is undefined; only type-level
  checks are possible until that changes.
- `Loom SQL`, `Loom Demos`, `Space Explorers`, and `Loom Editor` beyond
  `SceneHistory.cpp` and `EditCommands.cpp`, the two of its sources that need
  no window and are compiled into the suite beside the engine's.
