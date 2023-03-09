# Altirra 4.10 Build Instructions

## Prerequisites
### Build machine
* Required: Windows 10 x64 or newer

As of 4.10, building on a 32-bit (x86) system is no longer supported.

### Software components
The following components are required to minimally build Altirra:
* Visual Studio 2022 version 17.4.3 or newer (any edition), with the following components installed:
    * C++ core desktop features
    * MSVC v143 - VS 2022 C++ x64/x86 build tools (Latest)
    * Windows 11 SDK (10.0.22621.0 or newer)
* MADS 2.1.0+ (6502 assembler) (<https://mads.atari8.info>)

VS2019 or earlier, MinGW, GCC, and Clang compilers are not supported.

Additional components to compile for Windows on ARM (ARM64) (*not* AMD64):
* Visual Studio components
    * MSVC v143 - VS 2022 C++ ARM64 build tools (Latest)

Additional components to compile the help file:
* Visual Studio components
    * .NET Framework 4.8 Targeting Pack
    * C++/CLI support for v142 build tools (Latest)
* HTML Help 1.4 Toolkit
    This is sometimes automatically installed by Visual Studio. If not, it is available as part of the
    separate HTML Help Workshop 1.3 download. This download may be difficult to download from Microsoft
    at this point, so you may need to download it from the Internet Archive instead:
    <http://web.archive.org/web/20160201063255/http://download.microsoft.com/download/0/A/9/0A939EF6-E31C-430F-A3DF-DFAE7960D564/htmlhelp.exe>

Additional components to run the release script (release.py) and auxiliary scripts:
* [Python 3](https://www.python.org/) (3.10 or later recommended)
* [7-zip archiver](https://www.7-zip.org/)
* [AdvanceCOMP recompression utilities](https://github.com/amadvance/advancecomp/)

### Outdated components
If you had set up to build earlier versions of Altirra, these are no longer required in 4.10
and can be omitted:

* YASM
* DirectX SDK


If you have the DirectX SDK in your default VC++ Directories setup, make sure that its include
headers are lower priority than the Windows SDK per Microsoft's integration guidelines. Otherwise,
you can get compile warnings or errors due to it overriding newer DXGI headers from the Windows
SDK.

### Additional Tools

Newer versions of these tools should work but have not been tested. Older versions should be avoided;
some older versions of MADS, for instance, will either fail to build the AltirraOS kernel or will
introduce subtle corruption into floating point constants in the math pack.

## Build setup
### Build location

Altirra *should* build in a path that has spaces, but that's not tested, and the build may blow up -- so it's better
just to build it in a folder where the path has no spaces.

### Local overrides

The build projects are set up to allow project settings to be altered without needing to
change the base project files or change each project individually. This is optional, but is done by placing
property sheet files in the `localconfig/active` folder. Use the examples in the
`localconfig/example` folder as a starting point.


Visual Studio doesn't always pick up changes to `.props` files while open, so make sure to restart
the IDE after adding or changing any such file.

### MADS assembler override

By default, the build process will attempt to source `mads.exe` from PATH.
However, you might not want it in PATH, in which case you can override the path to it in the `ATMadsPath` property of `localconfig/active/Altirra.local.props`.
Use `localconfig/example/Altirra.local.props` as a reference.

### Platform setup override

The build is preconfigured to use the Visual Studio 2022 compiler (`v143`).
The `src/build/PlatformSetup.props` file is designed to make it easy to change this if
you want to try a different compiler, such as a future version of Visual C++ or LLVM/Clang.


To switch the toolchain locally, create `localconfig/active/PlatformSetup.local.props`
and modify the toolchain setting in it. Use `localconfig/example/PlatformSetup.local.props`
as a reference. All projects will be switched over to the new setting.


You shouldn't get a dialog from Visual Studio offering to upgrade the projects. If you do, don't do it -- it
will break the platform setup in the project files. Use the instructions above instead.

## Building
### First time

Open `Altirra.sln` and set the startup project to `Altirra` within the Native Projects folder in the Solution Explorer.


Change the active solution configuration and platform to Release x86 and build it first. This is needed
to build some common build tools that are used by other configurations/platforms. After that, you can
build other configurations such as Debug x86 or Release x64.

### Build targets

There are end projects that can be built from the three solution files:

* **Altirra**: This is the main computer emulator.
* **AltirraRMT**: Plugins for Raster Music Tracker (RMT).
* **ATHelpFile**: Help file for the computer emulator.


These projects have no direct dependency on each other, and the "run only startup"
option in Visual Studio can be used to avoid having to build both Altirra and AltirraShell
on every modification.


For the emulators, three configurations are supported: Debug (unoptimized),
Profile (optimized), and Release (LTCG). Debug is noticeably slower, but the
performance difference between Profile and Release is minor.


### Output locations and debugging setup

The final program and other build outputs go to the `out` folder.


Intermediate build artifacts like .obj files go to the `obj` folder,
and libraries to the `lib` folder. You can freely delete these as needed.

### Possible failures

Link failures usually mean that an upstream project or file failed to build.
Visual Studio has an unfortunate tendency to uselessly attempt linking a project
whose dependencies have failed, so always look for the first error.


### Release script

For shipping builds, the release script (`release.py`) automates the process
of generating and packaging a clean build. It is meant to be run in a Visual Studio
Developer Command Prompt. Run it with `py release.py` for usage.

## Deployment
### Licensing

**Please note that Altirra is licensed under the GNU General Public License, version 2 or above.**
**All rebuilt versions of it must be distributed under compatible conditions.**

The source code for the rebuilt version must be made available per the terms of the GPL for any released binaries.
This is true even for pre-release versions.


A few source code files have licenses that are more permissive than the GPL, and
may be used as such on their own. This only applies to the files that have a block
comment at the top describing the more permissive license option. In particular,
the system library and much of the 6502-based code is licensed more permissively.


The RMT plugins, and the libraries used in those RMT plugins, have a special license
exception added to the GPLv2 to explicitly allow their use with Raster Music Tracker.
This applies only to the libraries that have that exception
in their source files.

### Third party content

The following third party content is present in Altirra with the following licences, believed to be GPL-compatible:

* `system\source\hash.cpp`: MurmurHash3 (public domain; see source for reference)


The Tuffy TrueType font is no longer used or included with Altirra as of 4.10.


In addition, the built-in kernel contains a copy of the Atari system bitmap font. This is believed
to not be copyrightable, but concerned parties should consult a qualified legal advisor for the
pertinent jurisdictions as the author cannot provide legal assurance. No other parts of the Atari system ROMs are
included or derived from in the included source.


Altirra does not link with the Microsoft D3DX or D3DCompiler libraries. The latter is used in the build, but only
the build tools link to D3DCompiler and the outputs are used without it.

### Deployment dependencies

Altirra is statically linked against all necessary runtimes. In particular, neither the
DirectX nor Visual C++ Redistributable are needed with the final executable regardless of
OS.

## Additional topics
### Rebuilding ps1.x shaders

Since 2.90, the DirectX SDK is no longer necessary for the build as D3DCompiler from the Windows SDK is used
instead. However, since D3DCompiler no longer supports shader model 1.x, the remaining
vs/ps1.1 shaders have been included in precompiled form. The source has been included and
they can be rebuilt with the included `rebuildps11.cmd` script, but doing so requires
an older version of the DirectX SDK that still has `psa.exe`, since the latest SDK
only includes `fxc` which can only compile HLSL.


Since D3DCompiler no longer supports shader model 1.x, the remaining
vs/ps1.1 shaders have been included in precompiled form. The source has been included and
they can be rebuilt with the included `rebuildps11.cmd` script, but doing so requires
an older version of the DirectX SDK that still has `psa.exe`, since the latest SDK
only includes `fxc` which can only compile HLSL.


If you really, really feel the need to edit the pixel shaders and don't want to install
DXSDK November 2008 to get psa.exe, you can write a small wrapper around D3DXAssembleShader().
Sorry, but at this point even the author's tolerance for ps1.x is waning.
