# briefutil

Small Qt Quick utility I use to turn ad-hoc letter text into nicely formatted PDFs. It now renders letters natively with Qt 6, CMake, and libHaru; there is no LaTeX or MiKTeX runtime in the repo anymore.

<p align="center">
  <img src="example.png" alt="Example UI" width="946" height="1678" style="max-width: 100%; height: auto;">
  <br>
  <img src="sample_screenshot.png" alt="Sample UI" width="786" height="660" style="max-width: 100%; height: auto;">
</p>

## Repository layout
```
.
|- briefutil/         # Application sources, templates, and build files
|- example.png        # Screenshot used in the README
|- sample_screenshot.png
|- LICENSE.txt
\- README.md
```
The `briefutil` directory contains the Qt 6 / CMake application sources, JSON sender-profile defaults, and build configuration.

## Build requirements
- CMake 3.24 or newer
- A C++17 compiler with `std::filesystem`
- Qt 6 with the Core, Gui, Qml, Quick, and QuickControls2 modules
- Network access on first configure to fetch libHaru, zlib, and libpng

## Configure and build
```bash
cmake -S briefutil -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.10.1/llvm-mingw_64"
cmake --build build --config Release
```
Adjust `CMAKE_PREFIX_PATH` to point at your local Qt installation.

## Install and package
```bash
cmake --install build --config Release --prefix C:/apps/briefutil
cpack -C Release --config build/CPackConfig.cmake
```

## Runtime notes
- On first launch the executable creates `~/briefutil/templates/` and populates it with anonymized JSON sender profiles plus a synthetic signature image. Replace those files with your own stationery as needed.
- Output is written under `~/briefutil/output/` by default. Override via `output_dir.conf` if you prefer a different location.
- Leftover `.tex` templates from older versions are ignored by the active path. The app logs a warning so you can convert them to JSON sender profiles.
- The bundled example profiles are intentionally lightweight and tailored to the two shipped letter styles: simple and commercial.

## License
Source code is provided under the Simplified BSD License (see `LICENSE.txt`). The included templates and signature use generic "Max Mustermann" placeholder data so that nothing personal is shipped with the repository.
