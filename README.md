# iRASPA WinUI 3

Native WinUI 3 + DirectX 12 port of iRASPA (no Qt, no OpenGL/Vulkan/OpenCL).

## Prerequisites

- Visual Studio 2022/18 with the C++ desktop workload and the MSIX packaging tools
- vcpkg with **liblzma** and **zlib**, one triplet per target architecture
  (default `C:\vcpkg`, override with `/p:VcpkgRoot=`):

```powershell
vcpkg install zlib:x64-windows-static-md liblzma:x64-windows-static-md `
              zlib:arm64-windows-static-md liblzma:arm64-windows-static-md
```

  Both cross-compile on an x64 machine. The `-md` triplet gives static libraries
  that use the dynamic CRT, matching the app's `/MD` build, so there is exactly one
  CRT in the process and no extra DLLs to ship. Both libraries are named explicitly
  on the link line and vcpkg's global MSBuild integration is disabled for the app
  project, so the build does not depend on whether `vcpkg integrate install` has
  been run.

  Note that zlib's vcpkg port renamed its static library to `zs.lib`/`zsd.lib` in
  1.3.2 (it was `zlib.lib`/`zlibd.lib` before), and that `vcpkg install` upgrades
  already-installed ports to the current registry baseline. If a link fails with an
  unresolved `zs.lib`, check the names in `$(VcpkgStaticRoot)\lib`.
- Nothing needs to be installed for Python; see [Embedded Python](#embedded-python).

## Build

```powershell
cd C:\Users\ddubb\source\repos\iRASPA-WINUI3
nuget restore iRASPA-WINUI3.sln
msbuild iRASPA\iRASPA.vcxproj /p:Configuration=Debug /p:Platform=x64 /m
msbuild iRASPA\iRASPA.vcxproj /p:Configuration=Release /p:Platform=ARM64 /m
```

Open `iRASPA-WINUI3.sln` in Visual Studio for packaging.

NuGet packages restore into `packages\`. Supported platforms are **x64** and
**ARM64**; both build on an x64 host, though ARM64 can only be run and debugged on
ARM64 hardware. There is no x86 or ARM32 build: the Windows App SDK ships no ARM32
binaries at all, and one of its dependencies has no x86 variant.

Output is per-platform, `build\<Platform>\<Configuration>\`.

## Run

`run.bat [x64|ARM64] [Debug|Release]`, or the executable directly at
`build\x64\Debug\iRASPA.exe`. The build copies the gallery database
(`libraryofstructures.irspdoc`), the About-panel icon and the Python runtime next
to the executable.

## Embedded Python

The console runs a CPython interpreter that ships inside the app, so users do not
need Python installed. Two separate pieces, which must stay on the same version:

| Piece | Where | Role |
|-------|-------|------|
| Runtime | `PythonRunTimes\<Platform>\` | The embeddable distribution that ships, copied to `Python\` beside the exe and into the package |
| SDK | `python` / `pythonarm64` NuGet packages | Headers and the import library, build-time only |

`python314.dll` is delay-loaded and `PythonConsole.cpp` loads it from the app-local
`Python\` folder, so a missing payload degrades to "Python unavailable" rather than
preventing startup. The standard library is read from `python314.zip`; the search
paths are set explicitly through `PyConfig`, so start-up does not depend on
`python314._pth`, the registry, or `%PYTHONHOME%` (that variable is still honoured
as a developer escape hatch for pointing at another interpreter).

To move to a new Python, replace the folders under `PythonRunTimes\`, then update
`PythonVersion`/`PythonAbi` in `iRASPA\iRASPA.vcxproj`, the package versions in
`iRASPA\packages.config`, and `kPythonDll`/`kPythonZip` in `PythonConsole.cpp`.
The DLL name is duplicated in the last of those because the delay-load stub
resolves the import by module name.

## Layout

| Path | Role |
|------|------|
| `iRASPA/` | The application: WinUI 3 shell, views, document controllers |
| `iRASPA/Hosting/` | DirectX 12 swap-chain panel host |
| `iRASPA/iraspa/` | Shared domain kits (Math/Foundation/Symmetry/Simulation/Render/iRASPA) |
| `iRASPA/iraspa/datafiles/` | Gallery databases, icons, licences |
| `PythonRunTimes/` | Embeddable CPython that ships with the app, per architecture |
| `packaging/` | MSIX packaging project for the Microsoft Store |

The kits used to be six static libraries. They are now compiled directly into
the application; their sources keep C++17 and build without the precompiled
header, while the app code is C++20 and uses `pch.h`.

## Microsoft Store

`packaging\iRASPA.Packaging.wapproj` wraps the app in an MSIX under the existing
Store identity (app ID `9P9513KNH8VC`, reserved name `iRASPA`). The app project
itself stays unpackaged and self-contained, so the loose
`build\x64\Debug\iRASPA.exe` remains directly runnable and debuggable.

The output is an `x64|arm64` bundle. Each architecture is a separate pass over the
packaging project, so every package carries only its own binaries and its own copy
of the Python runtime. The packaging project also ships the app `.xaml` files into
the package resource index — without that, WinUI `LoadComponent` fail-fasts on
launch inside `Microsoft.UI.Xaml.dll` even though the `.xbf` files are present.

Bump `Version` in `packaging\Package.appxmanifest` and the matching
`FILEVERSION`/`PRODUCTVERSION` in `iRASPA\iRASPA.rc` together — the About panel
reads the version back out of the executable, and Store certification expects
the two to agree.

In Visual Studio, open `iRASPA-WINUI3.sln`, set the startup project to
**iRASPA.Packaging**, then **Publish → Create App Packages…** → Microsoft Store,
and select **Release** for **x64** and **ARM64**.

Or package from the command line (unsigned `.msixupload`, no certificate needed):

```powershell
msbuild packaging\iRASPA.Packaging.wapproj /p:Configuration=Release /p:Platform=x64 `
  /p:UapAppxPackageBuildMode=StoreOnly /p:GenerateAppxPackageOnBuild=true
```


The signing certificates live outside the repository, in `..\certificate_files\`
(a sibling of the repo root), so private key material is never committed. Point
the build somewhere else with:

```powershell
msbuild packaging\iRASPA.Packaging.wapproj /p:CertificateDir=D:\keys\ ...
```

`StoreOnly` submission packages are unsigned and need no certificate at all.

Package for local sideload testing (signs with `IRASPAPackaging_StoreKey.pfx`):

```powershell
msbuild packaging\iRASPA.Packaging.wapproj /p:Configuration=Release /p:Platform=x64 `
  /p:UapAppxPackageBuildMode=SideloadOnly /p:GenerateAppxPackageOnBuild=true
```

Output lands in `packaging\AppPackages\`.
