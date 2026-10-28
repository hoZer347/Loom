# Loom house style
Derived from settled files: Loom WASM/Camera.h, Camera.cpp, Audio.h, Audio.cpp, Utilities/Attributes.h, Utilities/StateMachine.cpp; Loom Editor/EditorSettings.h, EditorSettings.cpp, SceneHistory.h, EditorConsole.cpp, EditorTheme.h; Demo Sources/Raycasts/Scripts/Laser.hpp, Assets/Beam.shader, Demo Sources/Profiling/Scripts/Scanner.hpp; run-tests.ps1, Loom Visual Studio/install.ps1, ScriptNameDialog.cs.

## Braces and blocks
- Allman everywhere: namespaces, types, functions, ifs, loops, lambdas.
- Every closing brace takes a semicolon: functions, ifs, loops, namespaces. `if (...) { ... };`
- Only exception: a `}` followed by `else` or `catch` is bare (`}` then `else ++shot;`).
- Single-statement bodies drop braces, body on the next line: `if (!renderer)` / `return;`
- A one-statement `else` may share its line: `else ma_sound_set_spatialization_enabled(&shot, MA_FALSE);`
- Inline bodies in headers keep the semicolon: `bool CanUndo() const { return !m_undo.empty(); };`
- Brace init has inner spaces, empty is `{ }`: `glm::mat4 m_pose{ 1.0f };`, `std::vector<Step> m_undo{ };`

## Line breaks
- Long argument or parameter list: break after `(`, one per line, one tab deeper, `)` on the last one.
  `glm::perspective(` / `glm::radians((float)fieldOfView),` / `aspect,` / `(float)farPlane);`
- Multi-condition if: operator at line end, continuation one tab deeper.
  `if (listener == INVALID_SOCKET ||` / `bind(...) != 0 ||` / `listen(...) != 0)`
- Utilities/ and some engine physics put the operator first instead: `if (a` / `|| b)`.
- Long ternary: `? a` and `: b` on their own lines, one tab deeper.
- Lambda passed as argument: capture list on its own line, one tab deeper, brace below it.

## Indentation and spacing
- Tabs in C++, shaders and C#. PowerShell uses 4 spaces.
- Two blank lines between the last `#include` and `namespace`.
- One blank line between functions; between a declaration run and the control flow that uses it; before a trailing `return` after work.
- Header fields grouped by meaning, a blank line and a comment between groups.

## Naming
- Types, functions, methods: PascalCase (`SceneHistory`, `ViewProjection`, `OnFieldChanged`). `struct` only; no `class` in C++.
- Private members: `m_` + camelCase: `m_autoScroll`, `m_pose`.
- Public fields and `Serial` fields on engine components: camelCase, no prefix: `Serial<float> fieldOfView = 45.0f;`
- Script components keep their `Serial` fields private with `m_`: `Loom::Serial<float> m_range = 12.0f;`
- Locals and parameters: snake_case in the Editor (`select_all`, EditorConsole.cpp). camelCase in the engine, Utilities and scripts (`toLocal`, Laser.hpp).
- File-scope constants (anonymous namespace, `static constexpr` in scripts): UPPER_SNAKE: `constexpr float PARALLEL = 1e-4f;`
- Public constants on a type or namespace: PascalCase (`EditorTheme::DefaultFontSize`, `EditorSettings::MinScale`). A type whose constants already use another casing keeps it for new ones (`SpriteAsset::maxFrames`). The Editor's undo histories use snake_case, and a new one follows them (`SceneHistory::max_steps`, `UndoHistory::max_steps`).
- Function-local constants: UPPER_SNAKE in the engine and scripts (`constexpr float HALF = 0.5f;`). snake_case in the Editor and its tests, like its locals (`constexpr int column_count = 4;`, TestsPanel.cpp).
- `enum class` with PascalCase values: `enum class Theme { Loom, Dark, Light, Classic, Count };`
- Template parameters: `T`, `Enum`, or `_ValueType` in Utilities.
- Files: PascalCase, `.h`/`.cpp` in engine and editor, `.hpp` for game scripts; a few engine files have spaces (`Loom API.h`, `Macro Helpers.h`); tests are `<Name> Tests.cpp`.
- Script namespaces: `<Project>Scripts` (`RaycastsScripts`); engine and editor live in `namespace Loom`.

## Keywords and layout
- `final` on structs not meant to be derived: `struct LOOM_API Audio final`.
- Exported engine types carry `LOOM_API` between `struct` and the name.
- Components derive `Component<Self, "Category">`; scripts use `Loom::Component<Self>`.
- Order: `static inline` / `static constexpr` / `inline constexpr`; `constexpr const char*` for strings.
- `const` after the parameter list, `override` last: `void OnFieldChanged(const SerializedField& field) override;`
- `override` only, never `virtual` on overrides. `[[nodiscard]]` leads the declaration.
- Public API first with no label; one `private:` at the struct's own indent, then private types, helpers, members last.
- File-local helpers and constants go in an anonymous `namespace` inside `namespace Loom` (Utilities uses file `static`).
- A file or struct past about 200 lines, or holding several concerns, is cut into one `#pragma region Name` / `#pragma endregion` per concern within each of the public API, the private helpers and the members, indented with the code, a blank line inside each end: `#pragma region Default Bindings` (DialogueManager.cpp). Regions follow the braces, never straddle an `#if`, and a header and its .cpp name matching concerns alike.
- C-style casts: `(float)size.x`. `nullptr`; engine tests `if (!ptr)`, scripts `if (ptr == nullptr)`.
- `const` on every local that is not reassigned: `const float aspect = ...;`

## Includes
- `#pragma once` first line of every header.
- Groups split by one blank line: own header (in .cpp); `"Loom API.h"` alone; engine headers; third-party in quotes (`"glm/glm.hpp"`, `"imgui.h"`); then `<std>`.
- Alphabetical within each group. Forward declarations after includes: `struct ma_engine;`
- Implementation `#define`s sit directly above the include they configure.

## Constants and fields
- No magic numbers: every meaningful literal is a named `constexpr` with a comment saying what it is.
  `// How far ahead a target goes when the old one sat on the camera.` / `constexpr float DEFAULT_TARGET_DISTANCE = 1.0f;`
- Serialised field: `Serial<T> name = default;` with a unit or meaning comment above when not obvious: `// Vertical, in degrees.`
- A field naming a file in the project is `Serial<Asset<".ext">>`, never a `Serial<std::string>` path, so the inspector picks it from the project's files: `Serial<Asset<".shader">> m_shader = "Assets/Lit.shader";`. It lists every extension its loader reads, the one it is filed under first; a kind with several is a `using` beside its loader and is used by name: `FontAsset`, `TextureAsset`, `SoundAsset`. A field naming a folder is `Serial<Folder>`.
- React to inspector edits with `void OnFieldChanged(const SerializedField& field) override` and compare `field.data == &*member`.
- Editor panels: `bool Draw(bool* open)` opening `ImGui::Begin(EditorPanels::<Name>, open)`; settings as `key=value` keyed by `constexpr const char* X_KEY`.

## Comments
- Each type opens with a block naming it, then dash bullets:
  `/**` / `* Loom::Camera` / `* - Where the scene is seen from: ...` / `*/`
- `//` full sentences above each declaration in headers, describing behaviour and contracts, not the name.
- In .cpp, comments are sparse and give the reason: `// A target on the eye has no direction, so the transform's stands.`
- Utilities uses `///` for every declaration and `--` asides.
- British spelling (`colour`, `normalise`, `initialiser`).

## Shaders (.shader)
- One file, sections marked `// ===SETTINGS===`, `// ===VERTEX===`, `// ===FRAGMENT===`; a short header comment.
- Settings as `key = value`. Uniforms `u_camelCase`, attributes `aPos`, output `FragColor`. Allman, tabs.

## PowerShell
- Comment-based help (`.SYNOPSIS`, `.EXAMPLE`, `.NOTES`), `[CmdletBinding()]`, `param()` with PascalCase typed params.
- `$ErrorActionPreference = 'Stop'` at the top; `$camelCase` locals; Verb-Noun functions (`Find-MSBuild`).
- Allman braces, 4-space indent; one-line `if (...) { ... }` for a single action; backtick continuations.
- Single quotes unless interpolating; check `$LASTEXITCODE` after native calls and exit with it.

## C# (Loom Visual Studio)
- Allman, tabs, `sealed class`; members `m_camelCase`, `readonly`; `const` locals and fields camelCase.
