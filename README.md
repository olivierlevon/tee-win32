# tee for Windows

[![Build](https://github.com/olivierlevon/tee-win32/actions/workflows/build.yml/badge.svg)](https://github.com/olivierlevon/tee-win32/actions/workflows/build.yml)

A simple [**`tee`**](https://en.wikipedia.org/wiki/Tee_(command)) implementation for Microsoft Windows.

This is a fork of [tee-win32](https://github.com/dEajL3kA/tee-win32) by **dEajL3kA**, with the following added features:

- **ANSI-to-HTML conversion** (`--html`) &mdash; converts ANSI escape codes to HTML in output files
- **ANSI code stripping** (`-s` / `--strip`) &mdash; strips ANSI escape codes from output files
- **Timestamps** (`-t` / `--timestamp`) &mdash; adds ISO 8601 UTC timestamps to output file lines
- **Line numbers** (`-n` / `--linenumber`) &mdash; adds line numbers to output files
- **Grep filtering** (`--grep <pat>`) &mdash; only writes lines matching a regex pattern to output files (powered by PCRE2)
- **Log rotation** (`--rotate <sz>` / `--keep <n>`) &mdash; rotates output files when they reach a given size
- **ARM64 safety fixes** &mdash; consistent atomic access and memory barriers for ARM64
- **Build system upgrades** &mdash; Visual Studio 2022 (v143), GitHub Actions CI, CMake-based PCRE2 integration
- **Comprehensive documentation** &mdash; detailed technical documentation in [DOCUMENTATION.md](DOCUMENTATION.md)

![tee](etc/images/tee.png)  
<small>(image created by [Sven](https://commons.wikimedia.org/wiki/User:Sven), CC BY-SA 4.0)</small>

## Usage

```
tee for Windows

Copy standard input to output file(s), and also to standard output.

Usage:
  gizmo.exe [...] | tee.exe [options] [--] <file_1> ... <file_n>

Options:
  -a --append      Append to the existing file, instead of truncating
  -b --buffer      Enable write combining, i.e. buffer small chunks
  -e --escape      Enable standard output ANSI escape code processing
  -f --flush       Flush output file after each write operation
  -i --ignore      Ignore the interrupt signal (SIGINT), e.g. CTRL+C
  -n --linenumber  Add line numbers to output file(s)
  -s --strip       Strip ANSI escape codes from output file(s)
  -t --timestamp   Add ISO 8601 UTC timestamps to output file(s)
     --html        Convert ANSI escape codes to HTML in output file(s)
  -d --delay       Add a small delay after each read operation
     --grep <pat>  Only write lines matching regex <pat> to output file(s)
     --rotate <sz> Rotate output file(s) when they reach <sz> (e.g., 50M)
     --keep <n>    Keep <n> rotated files (default: 5)
  -h --help        Display this help message and exit
  -v --version     Display version information and exit
```

### Terminal output

Tee can be used as an intermediate buffer (i.e. *without* writing to a file) to greatly speed-up terminal output:
```
gizmo.exe [...] | tee.exe NUL
```

## Implementation

This is a "native" implementation of the **`tee`** command that builds directly on top of the Win32 API.

It uses multi-threaded I/O and triple buffering for maximum throughput. See [DOCUMENTATION.md](DOCUMENTATION.md) for details.

## System Requirements

This application requires **Windows 10** or later (including Windows 11 and Windows Server 2016+). All 32-Bit and 64-Bit editions, including ARM64, are supported. Older versions of Windows (Vista, 7, 8, 8.1) are **not** supported.

## Website

* <https://github.com/olivierlevon/tee-win32>

## Building from Source

### Prerequisites

- **Visual Studio 2022** (Community edition or higher) with the **Desktop development with C++** workload
  - Includes MSBuild, v143 toolset, and Windows SDK
  - ARM64 build tools if you need the ARM64 target
  - **Optional:** LLVM/Clang toolset (`ClangCL`) &mdash; install via the VS Installer ("C++ Clang tools for Windows")
- **CMake** (in PATH) &mdash; required to build the PCRE2 dependency
- **Pandoc** (optional) &mdash; only needed to regenerate `README.html`
- **Git** &mdash; to clone the PCRE2 source

### Getting the PCRE2 source

```cmd
git clone https://github.com/PCRE2Project/pcre2.git deps\pcre2
```

### Quick build (all platforms)

Simply run:
```cmd
make.cmd
```

This will:
1. Build PCRE2 static libraries for Win32, x64 and ARM64 (via `build_pcre2.bat`)
2. Build `tee.exe` (Release) for all three platforms
3. Copy the binaries to the `out\` directory
4. Generate `out\README.html` (if Pandoc is installed)

### Building with CMake

```cmd
REM 1. Clone PCRE2 (if not already done)
git clone https://github.com/PCRE2Project/pcre2.git deps\pcre2

REM 2. Configure and build (e.g., x64 Release)
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Supported architectures: `Win32`, `x64`, `ARM64`.

### Step-by-step build (MSBuild)

If you prefer to build manually:

```cmd
REM 1. Build PCRE2 static libs (all platforms x Debug/Release)
build_pcre2.bat

REM 2. Open a VS2022 Developer Command Prompt, then:
MSBuild.exe /p:Platform=x86   /p:Configuration=Release /t:rebuild tee.sln
MSBuild.exe /p:Platform=x64   /p:Configuration=Release /t:rebuild tee.sln
MSBuild.exe /p:Platform=ARM64 /p:Configuration=Release /t:rebuild tee.sln
```

Or open `tee.sln` in Visual Studio 2022 and build from the IDE.

### Building with LLVM/Clang (ClangCL)

The project supports building with the LLVM/Clang toolset bundled with Visual Studio 2022.
Override the platform toolset via MSBuild:

```cmd
MSBuild.exe /p:PlatformToolset=ClangCL /p:Platform=x64 /p:Configuration=Release /t:rebuild tee.sln
```

Or via CMake with `clang-cl` (from a VS Developer Command Prompt):

```cmd
cmake -B build -G Ninja -DCMAKE_C_COMPILER=clang-cl -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Environment variables

| Variable | Default | Description |
|---|---|---|
| `MSVC_PATH` | `C:\Program Files\Microsoft Visual Studio\2022\Community` | Path to your VS2022 installation |
| `PANDOC_EXE` | `C:\Program Files\Pandoc\pandoc.exe` | Path to Pandoc executable |

### Build output

| File | Platform |
|---|---|
| `out\tee-x86.exe` | Windows x86 (32-bit) |
| `out\tee-x64.exe` | Windows x64 (64-bit) |
| `out\tee-a64.exe` | Windows ARM64 |

All configurations (Debug and Release) generate PDB debug symbol files alongside the executables, enabling post-mortem debugging and crash dump analysis.

## License

Copyright (c) 2026 Olivier Levon
Copyright (c) 2024 "dEajL3kA" &lt;Cumpoing79@web.de&gt;
This work has been released under the MIT license. See [LICENSE.txt](LICENSE.txt) for details!

### Acknowledgement

Using [T-junction icons](https://www.flaticon.com/free-icons/t-junction) created by Smashicons &ndash; Flaticon.
