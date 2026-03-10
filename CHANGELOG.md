# Changelog

All notable changes to this project are documented in this file.

This project is a fork of [tee-win32](https://github.com/dEajL3kA/tee-win32) by **dEajL3kA**.

---

## [1.4.0] - 2026-03-10

### Added
- **Grep filtering** (`--grep <pat>`) &mdash; regex-based line filtering powered by PCRE2
- **Log rotation** (`--rotate <sz>` / `--keep <n>`) &mdash; automatic file rotation with configurable size and retention
- PCRE2 static library integration; `build_pcre2.bat` builds all platforms/configs via CMake
- `make.cmd` updated for Visual Studio 2022 with PCRE2 build step

### Changed
- Switched to PCRE2 regex engine (replaces previous hand-rolled pattern matching)
- All Release configs compile as C with explicit `/MT` CRT linkage
- ARM64 Release: removed LTCG to resolve `_Interlocked*` intrinsic issues

### Fixed
- ARM64 linker errors for `_Interlocked*`, `__chkstk`, `strchr`
- Release linker conflict: ignore `MSVCRT` when linking with `/MT`
- Various build fixes for PCRE2 integration across all platforms

## [1.3.0] - 2026-03-01

### Added
- **Timestamps** (`-t` / `--timestamp`) &mdash; ISO 8601 UTC timestamps on output file lines
- **Line numbers** (`-n` / `--linenumber`) &mdash; sequential line numbering in output files
- Block-copy optimization for `FMT_RAW` converter mode

### Fixed
- Undefined behavior, ARM64 memory barrier, and micro-optimizations from quality audit

## [1.2.0] - 2026-02-15

### Added
- **ANSI-to-HTML conversion** (`--html`) &mdash; converts ANSI escape codes to HTML in output files
- **ANSI code stripping** (`-s` / `--strip`) &mdash; removes ANSI escape codes from output files
- Comprehensive technical documentation ([DOCUMENTATION.md](DOCUMENTATION.md))

### Changed
- Upgraded project to Visual Studio 2022 (v143 toolset)
- Added ARM64 cross-compilation to CI build matrix

## [1.1.0] - 2026-02-01

### Added
- GitHub Actions CI for Windows x86/x64/ARM64 builds

### Fixed
- `concat_va()` O(n&sup2;) &rarr; O(n) string concatenation
- Replaced obsolete `FatalExit()` with `__debugbreak()` in assertions
- Consistent atomic access on `g_pending[]` for ARM64 safety
- Signal termination on all buffers during shutdown
- Removed dangerous `TerminateThread()` call during shutdown
- Added `FILE_SHARE_DELETE` to output file sharing mode
- Made `to_lower()` Unicode-aware using `CharLowerBuffW`
- Defensive check for `CommandLineToArgvW` returning zero args

## [1.0.0] - 2024

Original release by **dEajL3kA**.

### Features
- Multi-threaded I/O with triple buffering
- Write combining (`-b` / `--buffer`)
- ANSI escape code processing for console (`-e` / `--escape`)
- Flush after write (`-f` / `--flush`)
- Ignore SIGINT (`-i` / `--ignore`)
- Small delay after read (`-d` / `--delay`)
- Append mode (`-a` / `--append`)
- Support for up to 63 output files
- NUL device as output accelerator
- ARM64 platform support
