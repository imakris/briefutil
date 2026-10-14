# Building and packaging

For a quick build and first letter, see the [README](../README.md).

## Requirements

- CMake 3.24 or newer
- a C++20 compiler
- Qt 6: `Core`, `Gui`, `Qml`, `Quick`, `QuickControls2`, and `QuickDialogs2`
- network access on first configure if a sibling `mark2haru` checkout is not
  available, because CMake fetches `mark2haru`

Letter layout, Markdown parsing, and PDF rendering use Qt 6 and mark2haru in
native C++. To use another local mark2haru checkout, pass
`-DBRIEFUTIL_MARK2HARU_SOURCE_DIR=<path>` when configuring.

## Desktop app and CLI

From the repository root:

```powershell
cmake -S briefutil -B briefutil/build -DCMAKE_PREFIX_PATH="C:/Qt/6.x/<toolchain>"
cmake --build briefutil/build --config Release
```

Set `CMAKE_PREFIX_PATH` to your Qt installation and use a matching compiler.
With multi-config generators such as Visual Studio, the executables are:

```text
briefutil/build/app/Release/briefutil.exe
briefutil/build/cli/Release/briefutil_cli.exe
```

With a single-config generator such as Ninja, pass `-DCMAKE_BUILD_TYPE=Release`
at configure time. The executables are directly under `briefutil/build/app/`
and `briefutil/build/cli/`, without a `Release/` subdirectory; non-Windows
executables have no `.exe` suffix.

On Windows, the GUI build runs Qt deployment steps to place the required Qt
DLLs, plugins, and QML modules in its output directory.

## CLI without the desktop app

Disable the Qt Quick app to build the core, CLI, and tests without its GUI
requirements:

```powershell
cmake -S briefutil -B briefutil/build-cli -DCMAKE_PREFIX_PATH="C:/Qt/6.x/<toolchain>" -DBRIEFUTIL_BUILD_GUI=OFF
cmake --build briefutil/build-cli --config Release --target briefutil_cli
```

The CLI requires Qt `Core` and runs without a display server. Building the
default target in this configuration also builds the tests.

## Install and package

Install a build with:

```powershell
cmake --install briefutil/build --config Release --prefix C:/apps/briefutil
```

The app and CLI are installed under `bin/`, together with bundled fonts. On
Windows, installation also deploys the Qt runtime dependencies.

Create a package with:

```powershell
cpack -C Release --config briefutil/build/CPackConfig.cmake
```

The Windows CPack configuration uses NSIS.

## Portable Windows package

`build_portable.bat` uses the local tool paths in `build_config.bat` to build
and assemble a distribution under `dist/portable/`, then create a ZIP in
`dist/`. Its layout is:

```text
dist/portable/
  briefutil.exe
  briefutil_cli.bat
  briefutil_runtime/
```

The visible top-level `briefutil.exe` is a launcher. The real Qt application
binary, CLI binary, DLLs, plugins, QML modules, and fonts live in
`briefutil_runtime/`. `briefutil_cli.bat` starts the CLI from that directory.
Keep the runtime directory with the launchers.

For output-directory overrides in this layout, see
[Data locations](usage.md#data-locations).

## Tests

After building the default target, run:

```powershell
ctest --test-dir briefutil/build -C Release --output-on-failure
```
