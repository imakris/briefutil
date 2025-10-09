# briefutil

Small Qt Quick app I use to turn short letter drafts into nicely formatted PDFs via LaTeX. It’s a personal tool built around my own workflow, shared here in case someone happens to find it useful.

![briefutil preview](example.png)

## Project layout

```
.
|- briefutil/         # Application sources, templates, and build files
|- example.png        # Screenshot used in the README
|- LICENSE.txt
\- README.md
```

The `briefutil` directory contains both the original qmake project (`briefutil.pro`) and a modern CMake build system.

## Build & run

Requirements:

- CMake 3.16 or newer
- A C++17 compiler with `std::filesystem`
- Qt 5.15+ or Qt 6 with the Core, Gui, Qml, and Quick modules
- PowerShell (Windows) to unpack the MiKTeX bootstrap archive

Configure & build:

```bash
cmake -S briefutil -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.9.1/msvc2022_64"
cmake --build build --config Release
```

Optional: bundle a portable MiKTeX runtime (downloads ~200 MB the first time):

```bash
cmake --build build --target portable_miktex --config Release
```

Skip that step, or pass `-DBRIEFUTIL_BUNDLE_MIKTEX=OFF` during configuration, if you prefer to rely on a system-wide MiKTeX installation.

Install / package:

```bash
cmake --install build --config Release --prefix C:/apps/briefutil
cpack -C Release --config build/CPackConfig.cmake   # optional NSIS installer
```

## Templates & data

On first launch the application creates `~/briefutil/templates/` with anonymised "Max Mustermann" stationery and a synthetic signature image. Replace them with your own assets as required. Output is written under `~/briefutil/output/` by default; override via `output_dir.conf` if you prefer a different location.

## Runtime dependency

At runtime the program locates `texify.exe` from the bundled portable MiKTeX tree (`<install>/miktex/texmfs/install/miktex/bin/x64/texify.exe`) and falls back to whatever is available on `PATH` otherwise. Keep MiKTeX reasonably up to date if you use the bundled copy.

## License

Source code is provided under the Simplified BSD License (see `LICENSE.txt`). The included templates and signature rely on generic "Max Mustermann" placeholder data so that nothing personal is shipped with the repository.

Feel free to tailor everything to your own letterhead.

## GitHub Actions badges for different operating systems

GitHub generates one status badge per workflow file. When a workflow file
contains a matrix that runs on both Linux and Windows, the badge aggregates the
result across every matrix job. To show separate badges for Linux and Windows
you need to split the automation into two workflow files (for example,
`ci-linux.yml` and `ci-windows.yml`) or otherwise create distinct workflows that
filter on the target operating system. Each workflow then gets its own badge
URL, letting you embed independent status images such as:

```markdown
![Linux build](https://github.com/<owner>/<repo>/actions/workflows/ci-linux.yml/badge.svg)
![Windows build](https://github.com/<owner>/<repo>/actions/workflows/ci-windows.yml/badge.svg)
```

Within each workflow you can keep the job matrix focused on a single operating
system—for example, a Linux-only workflow might use `runs-on: ubuntu-latest`
while the Windows workflow uses `runs-on: windows-latest`. This keeps the badge
status meaningful for the platform it represents while still letting you share
common steps between the two files via [reusable workflows](https://docs.github.com/actions/using-workflows/reusing-workflows)
or composite actions when you want to avoid duplication.
