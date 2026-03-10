# tee for Windows - Detailed Technical Documentation

## Table of Contents

- [Overview](#overview)
- [What is `tee`?](#what-is-tee)
- [Platform Support](#platform-support)
- [Unicode and Character Encoding](#unicode-and-character-encoding)
- [File Name Handling](#file-name-handling)
- [Command-Line Options](#command-line-options)
- [Input and Output Data Format](#input-and-output-data-format)
- [ANSI Escape Code Support](#ansi-escape-code-support)
  - [Console Rendering](#console-rendering--e----escape)
  - [ANSI-to-HTML Conversion](#ansi-to-html-conversion)
  - [ANSI Code Stripping](#ansi-code-stripping)
  - [Converter Architecture](#converter-architecture)
  - [Combining Options](#combining-options)
- [Line Decoration: Timestamps and Line Numbers](#line-decoration-timestamps-and-line-numbers)
  - [Timestamp Format](#timestamp-format)
  - [Line Number Format](#line-number-format)
  - [Combining with ANSI Options](#combining-with-ansi-options)
- [Multi-Threaded Architecture](#multi-threaded-architecture)
- [Buffering and Write Combining](#buffering-and-write-combining)
- [Signal Handling (CTRL+C)](#signal-handling-ctrlc)
- [NUL Device as Output Accelerator](#nul-device-as-output-accelerator)
- [Pipe Behavior and Redirected Programs](#pipe-behavior-and-redirected-programs)
- [File Sharing and Concurrent Access](#file-sharing-and-concurrent-access)
- [Custom CRT Startup (Release Builds)](#custom-crt-startup-release-builds)
- [Build System and CI/CD](#build-system-and-cicd)
- [Exit Codes](#exit-codes)
- [Limitations](#limitations)
- [Line Filtering: Grep](#line-filtering-grep)
  - [Pattern Matching](#pattern-matching)
  - [Grep with Other Options](#grep-with-other-options)
- [Log Rotation](#log-rotation)
  - [Rotation Mechanism](#rotation-mechanism)
  - [Size Suffixes](#size-suffixes)
  - [Keep Count](#keep-count)
  - [Rotation with Converters](#rotation-with-converters)
  - [Rotation with Other Options](#rotation-with-other-options)
- [Examples](#examples)
- [License](#license)

---

## Overview

**tee for Windows** is a native Win32 implementation of the classic Unix `tee` command. It reads data from standard input (stdin) and simultaneously writes it to standard output (stdout) **and** one or more output files. Unlike ports based on MSYS2, Cygwin, or other POSIX emulation layers, this program is built directly on top of the Win32 API for maximum performance and minimal dependencies.

This project is a fork of [tee-win32](https://github.com/dEajL3kA/tee-win32) by **dEajL3kA**, extended with the following features:

- ANSI-to-HTML conversion (`--html`) and ANSI code stripping (`-s`/`--strip`)
- ISO 8601 UTC timestamps (`-t`) and line numbers (`-n`) for output files
- Regex-based line filtering (`--grep`) powered by PCRE2
- Log rotation (`--rotate`/`--keep`) with configurable size thresholds and retention
- ARM64 concurrency fixes (atomic accesses, memory barriers)
- Build system modernisation: Visual Studio 2022, GitHub Actions CI, CMake-based PCRE2 integration

Current version: **1.4.0**

## What is `tee`?

The `tee` command acts as a T-junction (or T-splitter) in a command pipeline. It takes the data flowing through a pipe and duplicates it: one copy continues downstream through stdout (to the next command or to the console), while additional copies are written to one or more files on disk. This is invaluable for logging, debugging, and monitoring command output in real time.

```
                       +---> file_1.txt
                       |
stdin ---> [tee.exe] --+---> file_2.txt
                       |
                       +---> stdout (console or next pipe)
```

## Platform Support

### Operating System

- **Windows only** -- This program exclusively targets Microsoft Windows.
- Minimum requirement: **Windows 10** or later (including Windows Server 2016+). Older versions (Vista, 7, 8, 8.1) are not supported.
- Uses Win32 APIs such as `SleepConditionVariableSRW`, Slim Reader/Writer Locks, and Condition Variables.

### Processor Architectures

The program supports three processor architectures via separate builds:

| Architecture | Identifier | Buffer Size | Notes |
|---|---|---|---|
| **x86** (32-bit Intel/AMD) | `Win32` | 4,096 bytes (32 * 128) | Compatible with the widest range of Windows systems |
| **x64** (64-bit Intel/AMD) | `AMD64` | 8,192 bytes (64 * 128) | Recommended for modern 64-bit Windows |
| **ARM64** (64-bit ARM) | `ARM64` | 8,192 bytes (64 * 128) | For Windows on ARM devices (e.g., Surface Pro X, Snapdragon laptops) |

The buffer size is automatically tuned to the processor word size: `PROCESSOR_BITNESS * 128` bytes.

## Unicode and Character Encoding

### Internal Representation

The program uses **wide characters (UTF-16LE)** internally for all string processing, which is the native encoding of the Windows API. All command-line arguments, file names, and diagnostic messages are handled as `wchar_t` strings.

### Console Output (stderr messages)

When writing diagnostic messages (errors, warnings, version info) to the console:
- If the output handle is a **console** (detected via `GetConsoleMode`), the program uses `WriteConsoleW` to output native UTF-16 text directly. This preserves the full Unicode character set, including CJK characters, emoji, and any glyph the console font supports.
- If the output handle is a **file or pipe** (not a console), the program converts UTF-16 to **UTF-8** using `WideCharToMultiByte(CP_UTF8, ...)` before writing. This ensures that redirected error output is saved in the universally compatible UTF-8 encoding.

### Data Passthrough (stdin to stdout/files)

The actual data being piped through `tee` is treated as **raw bytes** -- it is not interpreted, converted, or modified in any way. Whatever binary encoding the upstream program produces is passed verbatim to both stdout and the output files. This means:
- If the upstream program outputs UTF-8 text, the files will contain UTF-8 text.
- If the upstream program outputs raw binary data, the files will contain the exact same binary data.
- No Byte Order Mark (BOM) is added or removed.
- No line-ending conversion (CR/LF vs LF) is performed.

### File Name Support

File names on the command line support the **full Unicode character set** because the program uses `CommandLineToArgvW` and the wide-character `CreateFileW` API. This means:
- File names with accented characters (e.g., `rapport-resumé.txt`)
- File names with CJK characters (e.g., `output_日本語.log`)
- File names with emoji or other Unicode symbols

### Long File Name Support

Because the program uses `CreateFileW` (the wide-character Win32 API), it inherits Windows' file name length support:
- Standard paths up to **260 characters** (`MAX_PATH`) are always supported.
- On Windows 10 version 1607+ with the long path registry key enabled, paths up to **32,767 characters** can be used.
- UNC paths (`\\server\share\path\file.txt`) are supported.
- The `\\?\` prefix for extended-length paths is supported since the program does not modify or canonicalize the file path before passing it to `CreateFileW`.

## Command-Line Options

```
Usage:
  gizmo.exe [...] | tee.exe [options] <file_1> ... <file_n>
```

Options can be specified as short flags (`-a`), long flags (`--append`), or combined short flags (`-abe`):

| Short | Long | Description |
|---|---|---|
| `-a` | `--append` | **Append mode.** Open output files in append mode instead of truncating them. If the file already exists, new data is written after the existing content. If the file does not exist, it is created. Uses `OPEN_ALWAYS` + `SetFilePointerEx(FILE_END)`. |
| `-b` | `--buffer` | **Write combining / buffering.** Instead of forwarding each tiny chunk immediately, accumulate data until the buffer is at least 1/8 full before dispatching to the writer threads. This can dramatically improve throughput when the upstream program writes many small chunks (e.g., character by character). Without this flag, any amount of data (even 1 byte) triggers a write. |
| `-d` | `--delay` | **Read delay.** Adds a 1-millisecond `Sleep(1)` after each read cycle. This can reduce CPU usage when processing a very fast input stream, at the cost of slightly increased latency. Useful for CPU-constrained environments. |
| `-e` | `--escape` | **ANSI escape code processing.** Enables Virtual Terminal Processing on stdout by setting `ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING` on the console mode. This allows ANSI/VT100 escape sequences (colors, cursor movement, etc.) to be interpreted and rendered by the Windows console. See [ANSI Escape Code Support](#ansi-escape-code-support). |
| `-f` | `--flush` | **Flush after write.** Forces `FlushFileBuffers` on each output file after every write operation. Ensures data is physically committed to disk immediately, which is critical for real-time log monitoring but reduces throughput. Does not flush terminal handles (detected and skipped automatically). |
| `-h` | `--help` | **Help.** Displays the full help screen with version info and usage instructions, then exits. |
| | `--html` | **HTML conversion.** Converts ANSI escape codes in output files to HTML with inline CSS styling. The output is a self-contained HTML document with a dark terminal-style background, monospace font, and `<span>` tags for colors and text attributes. Stdout is not affected (raw passthrough). Mutually exclusive with `--strip`. See [ANSI-to-HTML Conversion](#ansi-to-html-conversion). |
| `-i` | `--ignore` | **Ignore interrupt.** Ignores CTRL+C / CTRL+Break / Console Close signals. The program continues reading and writing even when the user presses CTRL+C. Without this flag, CTRL+C causes a graceful shutdown. |
| `-n` | `--linenumber` | **Line numbers.** Prepends a line number to each line in the output files. Uses a 64-bit counter, supporting logs with up to 18 quintillion lines. Stdout is not affected (raw passthrough). See [Line Decoration](#line-decoration-timestamps-and-line-numbers). |
| `-s` | `--strip` | **Strip ANSI codes.** Removes all ANSI escape sequences from the output files, producing clean plain text. Stdout is not affected (raw passthrough). Mutually exclusive with `--html`. See [ANSI Code Stripping](#ansi-code-stripping). |
| `-t` | `--timestamp` | **Timestamps.** Prepends an ISO 8601 UTC timestamp to each line in the output files. Format: `[YYYY-MM-DDThh:mm:ss.mmmZ]`. Stdout is not affected (raw passthrough). See [Line Decoration](#line-decoration-timestamps-and-line-numbers). |
| `-v` | `--version` | **Version.** Displays the version string, PCRE2 version, copyright, fork origin, license, and project URL, then exits. |
| | `--grep <pat>` | **Grep filter.** Only write lines matching the regular expression `<pat>` (case-insensitive, PCRE2 syntax) to output files. Stdout always receives all data unfiltered. See [Line Filtering: Grep](#line-filtering-grep). |
| | `--rotate <sz>` | **Log rotation.** When an output file reaches `<sz>` bytes, rotate it (rename to `.1`, shift older files, create new empty file). Supports `K`, `M`, `G` suffixes. See [Log Rotation](#log-rotation). |
| | `--keep <n>` | **Rotation keep count.** Keep `<n>` rotated files (default: 5). Oldest files beyond `<n>` are deleted. Only meaningful with `--rotate`. |

### Option Parsing Rules

- Options must appear **before** file names on the command line.
- The special token `--` (double dash alone) stops option parsing; everything after it is treated as a file name, even if it starts with `-`.
- A single `-` alone is treated as a file name, not an option.
- Unknown options cause an error and the program exits immediately.
- Short options can be combined: `-abf` is equivalent to `-a -b -f`.
- Options are case-insensitive (e.g., `-A` is the same as `-a`), using ASCII case folding.

## Input and Output Data Format

### Raw Binary Passthrough

The core data pipeline is **encoding-agnostic**. The program reads raw bytes from stdin and writes the exact same bytes to stdout and all output files. There is no text processing, no character set conversion, and no line-ending normalization. This makes `tee` safe to use with:

- Plain text in any encoding (UTF-8, UTF-16, Latin-1, Shift-JIS, etc.)
- Binary data (executables, images, compressed archives)
- Mixed content

### What Upstream Programs Must Produce

For `tee` to work correctly in a pipeline, the upstream program (the one piping into `tee`) must write its output to **stdout** (standard output handle). This is the standard convention for command-line programs. Specifically:

- The upstream program should use `WriteFile(hStdOut, ...)` or equivalent (e.g., `printf`, `cout`, `Write-Output` in PowerShell).
- Data written to **stderr** by the upstream program is **not** captured by `tee` -- it goes directly to the console. To capture stderr as well, redirect it to stdout before piping: `gizmo.exe 2>&1 | tee.exe output.log`
- If the upstream program writes UTF-8 text, the output files will contain UTF-8 text.
- If the upstream program writes to the console using `WriteConsoleW` directly (bypassing stdout redirection), that output **cannot** be captured by any pipe, including `tee`.

### Output File Format

By default, output files contain the exact byte stream received from stdin. No headers, footers, timestamps, or metadata are added. The files are a byte-for-byte copy of the data that also went to stdout.

When `--html` is specified, output files are self-contained HTML documents with a header (DOCTYPE, `<html>`, `<head>`, CSS stylesheet, `<body>`, `<pre>`) and footer (`</pre>`, `</body>`, `</html>`). ANSI escape codes are converted to inline CSS `<span>` tags.

When `-s` / `--strip` is specified, output files contain only the text content with all ANSI escape sequences removed. The result is clean plain text without any color codes or cursor control sequences.

When `-t` / `--timestamp` and/or `-n` / `--linenumber` is specified, each line in the output files is prefixed with a timestamp and/or line number. These decorations can be combined with any format mode (`--html`, `--strip`, or raw).

## ANSI Escape Code Support

### The Problem

Many modern CLI tools output ANSI escape codes for colored text, progress bars, and cursor positioning. On Windows 10 and later, the console host (conhost.exe) and Windows Terminal support these escape codes, but this support must be explicitly enabled via `ENABLE_VIRTUAL_TERMINAL_PROCESSING`.

When a program's output is piped through `tee`, the stdout handle may lose its console mode flags because it is now a pipe rather than a direct console handle. This means ANSI codes may not be rendered as colors but instead appear as raw escape characters (e.g., `←[31m`).

Additionally, the raw escape codes stored in output files are unreadable by most text editors and web browsers, making it difficult to archive or share colored terminal output.

### Console Rendering: `-e` / `--escape`

When the `-e` flag is specified:
1. The program calls `GetConsoleMode(hStdOut, ...)` to check if stdout is a console.
2. If it is, it sets `ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING` on the console mode.
3. This enables the console to interpret ANSI escape sequences in the data being written to stdout.

**Important notes:**
- This flag only affects **stdout console rendering**. The raw escape codes are always written verbatim to the output files unless `--html` or `--strip` is also specified.
- If stdout is not a console (e.g., piped to another program), the flag has no effect.
- This flag is only meaningful on **Windows 10 version 1511** or later, which introduced Virtual Terminal Processing support.

### ANSI-to-HTML Conversion

The `--html` option converts ANSI escape codes in output files to a self-contained HTML document. This is useful for archiving colored terminal output, sharing it via the web, or viewing it in any browser.

#### Generated HTML Structure

```html
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>tee output</title>
<style>
body { margin:0; padding:1em; background:#000; color:#aaa; }
pre  { font-family:Consolas,'Courier New',monospace; font-size:14px;
       white-space:pre-wrap; word-wrap:break-word; }
</style>
</head>
<body>
<pre>
...converted content with <span> tags...
</pre>
</body>
</html>
```

#### Supported SGR Attributes

The converter implements a **streaming state machine** that parses ANSI escape sequences byte-by-byte, maintaining state across buffer boundaries. The following SGR (Select Graphic Rendition) attributes are mapped to CSS:

| SGR Code | Attribute | CSS Property |
|---|---|---|
| `0` | Reset all | Closes current `<span>` |
| `1` | Bold | `font-weight:bold` |
| `2` | Faint/dim | `opacity:0.5` |
| `3` | Italic | `font-style:italic` |
| `4`, `21` | Underline | `text-decoration:underline` |
| `7` | Reverse video | Swaps foreground and background colors |
| `8` | Concealed | `visibility:hidden` |
| `22` | Normal intensity | Resets bold and faint |
| `23`-`28` | Reset italic, underline, blink, reverse, concealed | Corresponding CSS reset |

#### Supported Color Modes

| Color Mode | SGR Codes | Range | Example |
|---|---|---|---|
| **Standard colors** | `30`-`37` (fg), `40`-`47` (bg) | 8 colors | `ESC[31m` = red text |
| **Bright colors** | `90`-`97` (fg), `100`-`107` (bg) | 8 colors | `ESC[91m` = bright red text |
| **256-color palette** | `38;5;N` (fg), `48;5;N` (bg) | 0-255 | `ESC[38;5;208m` = orange text |
| **24-bit truecolor** | `38;2;R;G;B` (fg), `48;2;R;G;B` (bg) | 16M colors | `ESC[38;2;255;128;0m` = orange text |

The 256-color palette maps:
- **0-7**: Standard ANSI colors (CGA palette)
- **8-15**: Bright ANSI colors
- **16-231**: 6×6×6 color cube (components: 0x00, 0x5F, 0x87, 0xAF, 0xD7, 0xFF)
- **232-255**: 24-step grayscale ramp (0x08 to 0xEE, step 10)

When bold is active and the foreground is a standard color (0-7), the color is automatically promoted to its bright variant (8-15), matching terminal behavior.

#### HTML Font Stack

The generated HTML uses a monospace font stack designed for cross-platform readability:

```css
font-family: Consolas, 'Courier New', monospace;
```

- **Consolas**: Primary choice, available on all modern Windows systems. Excellent readability for terminal output.
- **Courier New**: Fallback for older systems or non-Windows platforms.
- **monospace**: Generic fallback for any environment.

#### Escape Sequences Handled

| Sequence | Type | Action |
|---|---|---|
| `ESC [ ... m` | CSI SGR | Parsed and converted to `<span>` tags |
| `ESC [ ... <other>` | CSI (non-SGR) | Silently discarded (cursor moves, erase, etc.) |
| `ESC ] ... BEL` | OSC (BEL-terminated) | Silently discarded (window title, hyperlinks, etc.) |
| `ESC ] ... ESC \` | OSC (ST-terminated) | Silently discarded |
| `ESC <char>` | Two-byte sequences | Silently discarded |
| `0x9B ...` | Single-byte CSI (C1) | Parsed identically to `ESC [` |

HTML special characters (`<`, `>`, `&`, `"`) are properly escaped. Carriage return characters (`\r`) are stripped; line feeds (`\n`) are preserved (rendered correctly within `<pre>`).

#### Example

```cmd
colored-linter.exe | tee.exe -e --html lint_results.html
```

This produces a browsable HTML file with full color rendering, while the console also displays colors (thanks to `-e`).

### ANSI Code Stripping

The `-s` / `--strip` option removes all ANSI escape sequences from the output files, producing clean plain text. This is useful when:

- You need to process the output with tools that don't understand ANSI codes (e.g., `findstr`, `grep`, text editors).
- You want to archive log output without visual clutter from color codes.
- You're feeding the output to another program that would choke on escape sequences.

The strip mode uses the same streaming state machine as the HTML converter but simply discards all escape sequences and passes only the raw text bytes through.

#### Example

```cmd
REM Save clean text to file, but keep colors in console
colored-tool.exe | tee.exe -es output.txt

REM Multiple files: all files get stripped output
colored-tool.exe | tee.exe -s log1.txt log2.txt
```

### Converter Architecture

The ANSI converter (`ansiconv.c`) is implemented as a self-contained, streaming, stateful parser with zero CRT dependency:

- **Per-file state**: Each output file gets its own converter instance with independent parser state. This means escape sequences split across buffer boundaries are handled correctly. Line counters and line-start flags are also per-file.
- **32 KB output buffer**: Converted output is buffered internally and flushed to the file handle via `WriteFile` when the buffer fills or after each input chunk is processed.
- **Memory**: Each converter instance is allocated via `LocalAlloc` (~32 KB). No dynamic resizing or CRT heap usage.
- **Thread safety**: Each writer thread owns its converter exclusively; no shared mutable state between converters.
- **Line prefix injection**: When timestamps or line numbers are enabled, the converter detects `\n` boundaries in the byte stream and injects prefixes at the start of each new line. The first line of output always receives a prefix. The converter is created even for `FMT_RAW` mode when line decorations are requested.
- **Stdout passthrough**: Stdout is never converted or decorated. The converter only applies to file output threads, ensuring the console always receives raw data (which is then rendered by the terminal if `-e` is used).

### Combining Options

The `-e`, `--html`, and `-s` options serve complementary purposes and can be combined:

| Options | Console (stdout) | Output Files |
|---|---|---|
| *(none)* | Raw bytes | Raw bytes |
| `-e` | ANSI rendered by terminal | Raw bytes (including escape codes) |
| `-s` | Raw bytes | Plain text (escape codes stripped) |
| `--html` | Raw bytes | HTML with CSS colors |
| `-e -s` | ANSI rendered by terminal | Plain text (escape codes stripped) |
| `-e --html` | ANSI rendered by terminal | HTML with CSS colors |

## Line Decoration: Timestamps and Line Numbers

The `-t` / `--timestamp` and `-n` / `--linenumber` options add a prefix at the beginning of each line in the output files. These options only affect **file output** -- stdout always receives the raw, undecorated data.

Both options can be used independently, together, and in combination with any of the ANSI processing modes (`--html`, `--strip`, or raw passthrough).

### Timestamp Format

The `-t` / `--timestamp` option prepends an ISO 8601 UTC timestamp in millisecond resolution to each line:

```
[2026-03-09T14:32:15.123Z] Starting application...
[2026-03-09T14:32:15.456Z] Loading configuration...
[2026-03-09T14:32:16.789Z] Ready.
```

Format breakdown:
- Brackets `[...]` for visual separation
- `YYYY-MM-DDThh:mm:ss.mmmZ` -- ISO 8601 with milliseconds
- `Z` suffix indicates UTC (Coordinated Universal Time)
- Total prefix width: 28 characters (including trailing space)

The timestamp reflects the moment the line is written by the writer thread, not when it was produced by the upstream program. For most use cases the difference is negligible, but under extreme load or with `-b` (buffered mode), there may be a small delay.

Uses `GetSystemTime()` (Win32 API) internally -- no CRT dependency.

### Line Number Format

The `-n` / `--linenumber` option prepends a line number followed by a colon and space:

```
1: Starting application...
2: Loading configuration...
3: Ready.
```

The line counter is a **64-bit unsigned integer** (`ULONGLONG`), supporting up to **18,446,744,073,709,551,615 lines** (over 18 quintillion). This makes it suitable for extremely large log files that run for months or years.

Line numbers use **variable width** -- no leading zeros or padding. This keeps the output compact for small files while still supporting arbitrarily large line counts.

Each output file maintains its own independent line counter. If you write to multiple files, each file will have its own line numbering starting from 1.

### Combining Timestamp and Line Number

When both `-t` and `-n` are specified, the timestamp appears first, followed by the line number:

```
[2026-03-09T14:32:15.123Z] 1: Starting application...
[2026-03-09T14:32:15.456Z] 2: Loading configuration...
[2026-03-09T14:32:16.789Z] 3: Ready.
```

### Combining with ANSI Options

Line decorations are fully compatible with all ANSI processing modes:

| Options | Output Files |
|---|---|
| `-t` | Raw bytes + timestamps |
| `-n` | Raw bytes + line numbers |
| `-tn` | Raw bytes + timestamps + line numbers |
| `-ts` | Plain text (ANSI stripped) + timestamps |
| `-ns` | Plain text (ANSI stripped) + line numbers |
| `-t --html` | HTML with timestamps (prefixes appear inside `<pre>`) |
| `-n --html` | HTML with line numbers (prefixes appear inside `<pre>`) |
| `-tns` | Plain text + timestamps + line numbers |
| `-tn --html` | HTML + timestamps + line numbers |

#### Example: Timestamped stripped log

```cmd
colored-app.exe | tee.exe -ets app.log
```

Console shows colors (via `-e`), file gets clean text with timestamps:
```
[2026-03-09T14:32:15.123Z] INFO: Application started
[2026-03-09T14:32:15.456Z] WARN: Config file not found, using defaults
```

#### Example: Numbered HTML report

```cmd
build-tool.exe 2>&1 | tee.exe -en --html build.html
```

Console shows colors, HTML file has colored output with line numbers.

## Line Filtering: Grep

The `--grep <pattern>` option filters output so that **only lines matching the given regular expression** are written to the output files. This is useful for extracting specific information (errors, warnings, status messages) from a verbose output stream while still seeing everything in the console.

**Key behavior:**
- Stdout (console) always receives **all data unfiltered** -- the grep filter only applies to file outputs.
- Patterns use **PCRE2 regular expression syntax** (Perl-compatible).
- Matching is **case-insensitive** by default (the `PCRE2_CASELESS` flag is set).
- A line matches if the pattern is found **anywhere** in the line (partial match, not anchored).
- Lines are delimited by `\n` (line feed). Partial lines at end-of-stream are also checked.
- Each output file thread maintains its own line accumulation buffer (64 KB) and its own PCRE2 match data for thread safety.
- Invalid regex patterns are detected at startup with a clear error message.

### Pattern Matching

The pattern is specified as a command-line argument, converted to UTF-8 at startup, and compiled by the PCRE2 library. The full PCRE2 regex syntax is available, including:

- **Alternation**: `error|warning|fatal` matches any of the three words
- **Character classes**: `[Ee]rror` matches `Error` or `error`
- **Quantifiers**: `warn(ing)?` matches `warn` or `warning`
- **Anchors**: `^ERROR` matches only lines starting with `ERROR`
- **Wildcards**: `err.*found` matches `error: file not found`
- **Escape sequences**: `\d+` matches one or more digits

Examples of pattern matching:

| Pattern | Line | Match? |
|---|---|---|
| `error` | `[ERROR] File not found` | Yes (case-insensitive) |
| `error` | `No errors detected` | Yes (partial match) |
| `^ERROR` | `[ERROR] File not found` | No (line doesn't start with ERROR) |
| `^\\[ERROR\\]` | `[ERROR] File not found` | Yes (escaped brackets, anchored) |
| `error\|warn` | `WARNING: disk space low` | Yes (alternation) |
| `fatal` | `INFO: all good` | No |
| `\d{3,}` | `Error code 4567` | Yes (3+ digits) |

### Grep with Other Options

The grep filter is applied **before** any converter processing (timestamps, line numbers, HTML, strip). This means:

| Options | Console (stdout) | Output Files |
|---|---|---|
| `--grep "error"` | All data | Only lines containing "error" |
| `--grep "error" -t` | All data | Matching lines with timestamps |
| `--grep "error" -n` | All data | Matching lines with line numbers (numbered from 1, counting only matching lines) |
| `--grep "error" -s` | All data | Matching lines with ANSI codes stripped |
| `--grep "error" --html` | All data | Matching lines converted to HTML |
| `--grep "error" -ets` | ANSI rendered | Matching lines, stripped, with timestamps |

#### Example: Extract errors from a build

```cmd
msbuild project.sln 2>&1 | tee.exe --grep "error" -t errors.log
```

Console shows the full build output. `errors.log` contains only lines with "error" (case-insensitive), each prefixed with a UTC timestamp:
```
[2026-03-09T14:32:15.123Z] src\main.c(42): error C2065: 'foo': undeclared identifier
[2026-03-09T14:32:15.456Z] src\util.c(17): error C2143: syntax error: missing ';'
```

#### Example: Monitor warnings in real time

```cmd
server.exe 2>&1 | tee.exe --grep "warn" -f warnings.log
```

Console shows all server output. `warnings.log` is written and flushed (`-f`) in real time with only warning lines, allowing another terminal to watch with `type warnings.log` or a tail utility.

#### Example: Multiple severity filters

```cmd
ci-build.exe 2>&1 | tee.exe --grep "error" errors.log
```

Note: the same grep pattern applies to all output files. To filter different patterns to different files, chain multiple `tee` instances:

```cmd
ci-build.exe 2>&1 | tee.exe --grep "error" errors.log | tee.exe --grep "warn" warnings.log > full.log
```

The first `tee` filters errors to `errors.log` and passes **all data** to stdout (grep only affects files). The second `tee` filters warnings to `warnings.log`. The final redirect captures everything to `full.log`.

## Log Rotation

The `--rotate <size>` option enables **automatic log rotation**: when an output file reaches the specified size, it is closed, renamed, and a new empty file is created. This prevents log files from growing indefinitely, which is essential for long-running services, CI/CD pipelines, and continuous monitoring.

### Rotation Mechanism

When the output file reaches the rotation threshold, the following sequence occurs:

1. If a converter is active (HTML, strip, timestamps, line numbers):
   - The converter's internal buffer is flushed
   - The format footer is written (HTML closing tags if `--html`)
2. The current file handle is closed
3. Files are shifted:
   - The oldest file (`.N` where N = keep count) is **deleted**
   - Each remaining rotated file is renamed: `.N-1` → `.N`, `.N-2` → `.N-1`, etc.
   - The base file is renamed to `.1`
4. A new empty base file is created
5. If a converter is active:
   - The converter state is fully reset (line counter back to 1, parser state cleared)
   - The format header is written (HTML preamble if `--html`)

```
Before rotation (keep=3):
  build.log      (50 MB, at threshold)
  build.log.1    (previous rotation)
  build.log.2    (older)
  build.log.3    (oldest, will be deleted)

After rotation:
  build.log      (0 bytes, new file)
  build.log.1    (was build.log, 50 MB)
  build.log.2    (was build.log.1)
  build.log.3    (was build.log.2)
  [build.log.3 from before was deleted]
```

### Size Suffixes

The `--rotate` option accepts a numeric value with an optional suffix:

| Suffix | Multiplier | Example | Actual Size |
|---|---|---|---|
| *(none)* | 1 (bytes) | `--rotate 1048576` | 1,048,576 bytes |
| `K` | 1,024 (KiB) | `--rotate 512K` | 524,288 bytes |
| `M` | 1,048,576 (MiB) | `--rotate 50M` | 52,428,800 bytes |
| `G` | 1,073,741,824 (GiB) | `--rotate 1G` | 1,073,741,824 bytes |

Suffixes are **case-insensitive** (`50m` = `50M`). The value must be greater than zero.

### Keep Count

The `--keep <n>` option specifies how many rotated files to retain. Default: **5**.

| `--keep` | Files on disk (after many rotations) |
|---|---|
| `--keep 1` | `app.log`, `app.log.1` |
| `--keep 3` | `app.log`, `app.log.1`, `app.log.2`, `app.log.3` |
| `--keep 10` | `app.log`, `app.log.1` ... `app.log.10` |

Using `--keep` without `--rotate` produces a warning and has no effect.

### Rotation with Converters

Log rotation is fully compatible with all converter modes:

- **Raw mode** (default): Files are simply closed and reopened. No special handling.
- **`--html` mode**: Each rotated file is a **self-contained HTML document** -- the footer (`</pre></body></html>`) is written before closing, and the header (DOCTYPE, CSS, `<pre>`) is written to the new file. This means every `.html`, `.html.1`, `.html.2`, etc. can be opened directly in a browser.
- **`--strip` mode**: ANSI parser state is reset on rotation; the new file starts clean.
- **Timestamps (`-t`)**: Timestamps continue reflecting real time in the new file.
- **Line numbers (`-n`)**: The line counter **resets to 1** on each rotation, so each rotated file has its own independent line numbering.

### Rotation with Other Options

| Options | Behavior |
|---|---|
| `--rotate 50M` | Rotate at 50 MiB, keep 5 files (default) |
| `--rotate 50M --keep 3` | Rotate at 50 MiB, keep 3 files |
| `--rotate 100M -a` | Append mode + rotation (initial append may trigger immediate rotation if file already exceeds threshold) |
| `--rotate 10M -f` | Rotate at 10 MiB, flush after each write |
| `--rotate 50M --html` | Each rotated HTML file is self-contained |
| `--rotate 50M -tn` | Each rotated file has fresh timestamps and line numbers starting from 1 |
| `--rotate 50M --grep "error"` | Only error lines are written; rotation triggers when filtered output reaches 50 MiB |

#### Example: Long-running service with rotation

```cmd
my-service.exe 2>&1 | tee.exe --rotate 100M --keep 10 -t service.log
```

- Console shows all output in real time
- `service.log` is the current log file with timestamps
- When it reaches 100 MiB, it rotates to `service.log.1`
- Up to 10 rotated files are kept (total max ~1.1 GB on disk)
- Each rotated file has its own line numbering and timestamp continuity

#### Example: CI build with HTML rotation

```cmd
ci-pipeline.exe 2>&1 | tee.exe -e --rotate 20M --keep 3 --html build.html
```

- Console shows live colored output
- `build.html` is the current HTML log (self-contained, viewable in browser)
- Rotated files `build.html.1`, `build.html.2`, `build.html.3` are also valid HTML
- Each file has its own DOCTYPE, CSS, and closing tags

#### Example: Error-only rotated log

```cmd
noisy-app.exe 2>&1 | tee.exe --grep "error" --rotate 10M --keep 5 -f errors.log
```

- Console shows everything
- `errors.log` only contains lines matching "error"
- Rotated when the filtered output reaches 10 MiB
- Flushed after each write for real-time monitoring

## Multi-Threaded Architecture

### Design

The program uses a **multi-threaded producer-consumer** architecture with **triple buffering** for maximum I/O throughput:

- **1 main thread (producer)**: Reads data from stdin into a shared buffer.
- **N writer threads (consumers)**: One thread per output destination (stdout + each output file). Each writer thread reads from the shared buffer and writes to its assigned output.

### Triple Buffering

Three buffers are used in a rotating fashion:
1. While the **main thread** fills buffer A with new data from stdin...
2. The **writer threads** are simultaneously writing buffer B to their outputs...
3. Buffer C is standing by, ready to be filled next.

This ensures that reading and writing can overlap, preventing the slower output operations from blocking the input. The rotating buffer scheme is managed via a modular index with a polarity flag to track which "generation" of data each buffer holds.

### Synchronization

- **SRW Locks** (Slim Reader/Writer Locks): Each buffer has its own SRW lock. The main thread acquires exclusive access (writer lock) to fill a buffer. Writer threads acquire shared access (reader lock) to read from it concurrently.
- **Condition Variables**: Two condition variables per buffer coordinate the handoff:
  - `g_condIsReady[i]`: Signaled by the main thread when buffer `i` has new data for the writers.
  - `g_condAllDone[i]`: Signaled by the last writer thread to finish, telling the main thread the buffer is free.
- **Atomic operations**: The `g_pending[]` counters use `InterlockedIncrement`, `InterlockedDecrement`, `InterlockedExchange`, and `InterlockedCompareExchange` for lock-free tracking of how many writer threads still need to process each buffer. This is critical for ARM64 correctness where memory ordering is weaker than x86.

### Maximum Threads

The maximum number of output files is limited by `MAXIMUM_WAIT_OBJECTS` (64 on Windows), minus 1 for the stdout thread. This means up to **63 output files** can be specified simultaneously.

## Buffering and Write Combining

### Default Behavior (no `-b` flag)

By default, the program forwards data as soon as any amount is read from stdin (minimum 1 byte). This provides the lowest latency -- data appears on stdout and in files almost immediately after being produced by the upstream program.

### Buffered Mode (`-b` flag)

When `-b` is specified, the program accumulates data in the buffer until it reaches at least `BUFFER_SIZE / 8` bytes before dispatching to the writer threads:
- On x86 (32-bit): minimum chunk = 512 bytes
- On x64/ARM64 (64-bit): minimum chunk = 1,024 bytes

This reduces the number of write system calls and can significantly improve throughput when the upstream program outputs data in very small increments. The trade-off is slightly increased latency before data appears in the output.

## Signal Handling (CTRL+C)

### Default Behavior

The program installs a console control handler via `SetConsoleCtrlHandler` that catches:
- **CTRL+C** (`CTRL_C_EVENT`)
- **CTRL+Break** (`CTRL_BREAK_EVENT`)
- **Console Close** (`CTRL_CLOSE_EVENT`)

When any of these signals is received, the global `g_stop` flag is atomically set to `TRUE`. The main read loop checks this flag after each iteration and performs a graceful shutdown: it waits for all pending writes to complete, signals all writer threads to terminate, and waits for them to exit before closing file handles.

### Ignore Mode (`-i` flag)

When `-i` is specified, the program continues operating even after CTRL+C. The `g_stop` flag is still set, but the read loop ignores it (`(!g_stop) || options.ignore`). This is useful when `tee` is part of a long-running pipeline that should not be interrupted by accidental CTRL+C presses.

### Graceful Shutdown

The shutdown sequence ensures no data loss:
1. Wait for any pending writes on the current buffer to complete.
2. Signal termination on **all** buffers (not just the current one) by setting `g_bytesTotal` to `MAXDWORD` and `g_pending` to `MAXLONG` or `MINLONG`.
3. Wake all writer threads via `WakeAllConditionVariable`.
4. Wait up to 10 seconds for all writer threads to exit cooperatively.
5. If any thread fails to exit within the timeout, a warning is printed (but no forceful termination is attempted).
6. Flush and close all file handles.

## NUL Device as Output Accelerator

A unique feature of this implementation: specifying `NUL` as a file name causes the program to **skip** opening that file entirely (detected by `is_null_device()`). The NUL device check is case-insensitive and handles path prefixes (e.g., `C:\Temp\NUL` or `\\.\NUL`).

This enables a powerful use case: using `tee` as a **buffered output accelerator** for terminal display:

```cmd
slow-tool.exe | tee.exe NUL
```

In this configuration, `tee` reads from stdin using its multi-threaded buffered pipeline and writes to stdout only (since NUL is discarded). Because `tee`'s I/O pipeline is more efficient than many programs' direct console output, this can result in a noticeable speed-up of terminal rendering.

## File Sharing and Concurrent Access

Output files are opened with the following sharing flags:
- `FILE_SHARE_READ`: Other processes can read the file while `tee` is writing to it. This allows real-time monitoring with tools like `tail -f` or `Get-Content -Wait`.
- `FILE_SHARE_DELETE`: Other processes can rename or delete the file while `tee` still has it open. This supports log rotation scenarios where an external tool renames the current log file and `tee` continues writing to the original handle.

## Custom CRT Startup (Release Builds)

### Why?

In **Release** builds, the program completely bypasses the C Runtime Library (CRT). Instead of linking against `msvcrt.dll` or the Universal CRT, it uses a custom entry point `_startup` that:

1. Calls `SetErrorMode(SEM_FAILCRITICALERRORS)` to suppress system error dialog boxes.
2. Parses the command line using `CommandLineToArgvW(GetCommandLineW(), ...)`.
3. Converts all arguments from UTF-16 to UTF-8 using `WideCharToMultiByte(CP_UTF8, ...)`.
4. Calls `tee_main(argc, argv_utf8)` with the UTF-8 argument array.
5. Frees the UTF-8 arguments and calls `ExitProcess()` with the return value.

The program also provides CRT intrinsic stubs (`memset`, `memcpy`, `memmove`, `strlen`) required by the statically linked PCRE2 library when building without CRT.

### Benefits

- **Zero CRT dependency**: The Release binary only links against `kernel32.lib`, `Shell32.lib`, and `pcre2-8-static.lib`. No CRT DLLs are needed at runtime.
- **Minimal binary size**: The executable is small since it doesn't pull in CRT initialization code, locale support, stdio buffering, etc.
- **Faster startup**: No CRT initialization overhead (heap setup, locale initialization, atexit registration, etc.).
- **No runtime library version conflicts**: The binary works on any supported Windows version without needing a specific Visual C++ Redistributable installed.
- **UTF-8 throughout**: All command-line arguments (options, patterns, file names) are converted to UTF-8 once at startup, eliminating repeated conversions and simplifying the main code.

### Debug Builds

In Debug builds (`_DEBUG` defined), the standard CRT startup is used, which provides:
- AddressSanitizer (ASAN) support (enabled in the project for x86 and x64 Debug builds).
- Standard debug heap with leak detection.
- Standard `wmain` entry point (which wraps to `tee_main` with UTF-8 conversion).

## Build System and CI/CD

### Build Tool

The project uses **Visual Studio 2022** with the **v143 platform toolset**. It is a pure C project with two source files (`tee.c` and `ansiconv.c`) and a Visual Studio solution (`tee.sln`) and project file (`tee.vcxproj`). The project also supports building with the **LLVM/Clang toolset** (`ClangCL`) bundled with VS2022, as well as standalone `clang-cl` via CMake.

### Dependencies

- **PCRE2** (Perl-Compatible Regular Expressions): Statically linked, 8-bit only. Built without JIT and without Unicode tables to minimize size and avoid CRT dependencies. Custom memory allocators (`LocalAlloc`/`LocalFree`) are provided via `pcre2_general_context_create()`. The PCRE2 source should be placed in `deps/pcre2/` and built using `build_pcre2.bat` (or automatically by the CI workflow).

### Build Configurations

| Configuration | Optimization | CRT | ASAN | Assertions | Debug Info | Entry Point |
|---|---|---|---|---|---|---|
| **Debug x86** | Disabled | Debug DLL | Yes | Enabled | Full PDB | Standard CRT |
| **Debug x64** | Disabled | Debug DLL | Yes | Enabled | Full PDB | Standard CRT |
| **Debug ARM64** | Disabled | Debug DLL | No | Enabled | Full PDB | Standard CRT |
| **Release x86** | MaxSpeed + LTCG | None (no CRT) | No | Disabled | PDB (separate) | `_startup` |
| **Release x64** | MaxSpeed + LTCG | None (no CRT) | No | Disabled | PDB (separate) | `_startup` |
| **Release ARM64** | MaxSpeed + LTCG | None (no CRT) | No | Disabled | PDB (separate) | `_startup` |

All configurations generate debug information (PDB files). Release PDBs are separate files (`tee.pdb`) that do not increase the size of the executable. They enable post-mortem debugging with WinDbg, crash dump analysis, and profiling.

Release builds enable:
- Whole Program Optimization (WPO) / Link-Time Code Generation (LTCG) -- MSVC only; skipped for ClangCL
- Function-level linking and COMDAT folding
- Intrinsic functions
- Frame pointer omission
- Warnings treated as errors (Level 4)

### Building with LLVM/Clang

The vcxproj supports the `ClangCL` platform toolset. MSVC-only options (`WholeProgramOptimization`, `LinkTimeCodeGeneration`, `EnableEnhancedInstructionSet`) are automatically skipped when `PlatformToolset=ClangCL`.

```cmd
REM MSBuild with ClangCL
MSBuild.exe /p:PlatformToolset=ClangCL /p:Platform=x64 /p:Configuration=Release /t:rebuild tee.sln

REM CMake with clang-cl (from a VS Developer Command Prompt)
cmake -B build -G Ninja -DCMAKE_C_COMPILER=clang-cl -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Install the ClangCL toolset via the Visual Studio Installer: **Individual components > C++ Clang tools for Windows**.

### GitHub Actions CI

The project includes a GitHub Actions workflow (`.github/workflows/build.yml`) that builds all 6 combinations (3 platforms x 2 configurations) on every push and pull request to `master`. The workflow automatically clones PCRE2, builds the static library for the target platform/configuration, and then builds the project. Release binaries are uploaded as build artifacts.

For local builds, run `build_pcre2.bat` first to compile PCRE2 for all platforms, then open `tee.sln` in Visual Studio.

## Exit Codes

| Code | Meaning |
|---|---|
| `0` | Success. All data was read and written without errors. |
| `1` | Error. Could not open an output file, invalid command-line option, or I/O error during read/write. |
| `-1` | System error. Standard handles could not be obtained, or the command-line could not be parsed. |

## Limitations

1. **Windows only**: There is no support for Linux, macOS, or any other operating system. The program is built entirely on Win32 APIs.
2. **No stdin from console**: The program is designed for pipe usage. It reads from stdin, which must be redirected from another program or a file. It does not provide an interactive mode where you type input directly.
3. **No output to stdout suppression**: Unlike some `tee` implementations, there is no option to write only to files and suppress stdout output. Data always goes to stdout.
4. **No custom timestamp format**: Timestamps use a fixed ISO 8601 UTC format (`[YYYY-MM-DDThh:mm:ss.mmmZ]`). Local time or custom formats are not supported.
5. **Maximum 63 output files**: Limited by `MAXIMUM_WAIT_OBJECTS` (64) minus 1 for the stdout writer thread.
6. **No stderr capture**: Only stdout from the upstream program is captured. To include stderr, use shell redirection (`2>&1`) before the pipe.
7. **Grep limitations**: The `--grep` filter uses PCRE2 regular expressions with case-insensitive matching. Inverted matching (`-v`) and per-file patterns are not supported. The same pattern applies to all output files.

## Examples

### Basic: Log command output to a file

```cmd
dir /s C:\ | tee.exe directory_listing.txt
```

The directory listing is displayed in the console and simultaneously saved to `directory_listing.txt`.

### Append to an existing log file

```cmd
build.exe 2>&1 | tee.exe -a build.log
```

Build output (including errors via `2>&1`) is appended to `build.log` without overwriting previous content.

### Write to multiple files simultaneously

```cmd
deploy.exe | tee.exe deploy_log.txt deploy_backup.txt
```

The same output goes to both files and to the console.

### Buffered mode for high-throughput scenarios

```cmd
fast-generator.exe | tee.exe -b large_output.dat
```

Write combining reduces system call overhead when processing high-volume data streams.

### Real-time log monitoring with flush

```cmd
server.exe | tee.exe -f server.log
```

With `-f`, each write is flushed to disk immediately, so another terminal running `type server.log` (or a tail utility) sees updates in real time.

### Colored output preservation

```cmd
colored-linter.exe | tee.exe -e lint_results.txt
```

The `-e` flag ensures ANSI colors render in the console. The raw escape codes are saved in `lint_results.txt` for later viewing in a compatible viewer.

### Save colored output as HTML

```cmd
colored-linter.exe | tee.exe -e --html lint_results.html
```

ANSI colors render in the console (via `-e`) and the output file is a self-contained HTML document with CSS-styled colors. Open `lint_results.html` in any browser to see the colored output.

### Archive clean log output (strip ANSI codes)

```cmd
colored-tool.exe | tee.exe -es clean_output.txt
```

The console shows full colors (via `-e`), but the file contains clean plain text with all escape codes removed. Ideal for `grep`, `findstr`, or text editors that don't understand ANSI codes.

### Multiple files with HTML conversion

```cmd
ci-build.exe 2>&1 | tee.exe -e --html build_report.html build_report_backup.html
```

Both output files receive the HTML-formatted version. The console shows live colored output.

### Add timestamps to log files

```cmd
server.exe | tee.exe -t server.log
```

Each line in `server.log` is prefixed with `[2026-03-09T14:32:15.123Z] `. Console output is unchanged.

### Add line numbers and timestamps

```cmd
long-running-process.exe | tee.exe -tn process.log
```

Each line gets both a timestamp and a line number:
```
[2026-03-09T14:32:15.123Z] 1: Starting...
[2026-03-09T14:32:15.456Z] 2: Processing item 1...
```

### Numbered, stripped, colored log

```cmd
colored-ci.exe 2>&1 | tee.exe -ens build.log
```

Console shows live colors (`-e`), file gets stripped plain text with line numbers (`-ns`).

### Speed up slow terminal output

```cmd
verbose-tool.exe | tee.exe NUL
```

Using `NUL` as the output file means no file is actually written, but `tee`'s multi-threaded buffered I/O pipeline can accelerate the console rendering of the upstream program's output.

### Ignore CTRL+C for long-running pipelines

```cmd
long-process.exe | tee.exe -i process.log
```

CTRL+C in the console will not stop `tee` -- it continues capturing output even if the user accidentally presses CTRL+C.

### Combined options

```cmd
streaming-app.exe | tee.exe -abef stream.log
```

Append mode + buffering + ANSI escape processing + flush after each write.

### Filter errors to a log file (grep)

```cmd
msbuild project.sln 2>&1 | tee.exe --grep "error" build_errors.log
```

All build output appears in the console. Only lines containing "error" (case-insensitive) are written to `build_errors.log`.

### Filter with timestamps and line numbers

```cmd
deploy.exe 2>&1 | tee.exe --grep "fail" -tn deploy_failures.log
```

Console gets everything. `deploy_failures.log` contains only lines with "fail", each prefixed with a timestamp and line number:
```
[2026-03-09T14:32:15.123Z] 1: FAILED: Connection to db-server timed out
[2026-03-09T14:32:16.789Z] 2: FAILED: Rollback triggered
```

### Filter and strip ANSI codes

```cmd
colored-test-runner.exe | tee.exe --grep "FAIL" -es test_failures.txt
```

Console shows full colored output (`-e`). `test_failures.txt` gets only failing test lines, with ANSI codes stripped (`-s`) for clean text.

### Filter to HTML report

```cmd
ci-build.exe 2>&1 | tee.exe --grep "warning|error" -e --html issues.html
```

Console renders colors. `issues.html` is a browsable HTML file containing only warning/error lines with full color rendering.

### Basic log rotation

```cmd
my-service.exe 2>&1 | tee.exe --rotate 50M service.log
```

`service.log` is rotated when it reaches 50 MiB. Up to 5 rotated files are kept (default): `service.log.1` through `service.log.5`.

### Rotation with custom keep count

```cmd
my-service.exe 2>&1 | tee.exe --rotate 100M --keep 20 service.log
```

Rotate at 100 MiB, keep up to 20 archived files. Total potential disk usage: ~2.1 GB.

### Rotation with small threshold (high-frequency rotation)

```cmd
fast-producer.exe | tee.exe --rotate 1M --keep 3 -b output.dat
```

Rotate every 1 MiB, keep only 3 files. Buffered mode (`-b`) for high throughput.

### Rotation with timestamps and append

```cmd
server.exe 2>&1 | tee.exe --rotate 50M --keep 10 -at server.log
```

Append mode (`-a`) for initial startup (preserves existing data). Timestamps (`-t`) on every line. Rotates at 50 MiB, keeps 10 files.

### Rotation with HTML conversion

```cmd
colored-ci.exe 2>&1 | tee.exe --rotate 20M --keep 5 -e --html build.html
```

Each rotated file (`build.html`, `build.html.1`, ...) is a **self-contained HTML document** with its own DOCTYPE, CSS, and closing tags. Open any of them directly in a browser.

### Rotation with line numbers

```cmd
batch-job.exe | tee.exe --rotate 10M --keep 3 -n batch.log
```

Line numbers **reset to 1** in each new rotated file, making each file independently readable.

### Rotation combined with grep

```cmd
verbose-service.exe 2>&1 | tee.exe --grep "error" --rotate 5M --keep 10 -f errors.log
```

Only error lines are written to `errors.log`. The file is rotated when the filtered output reaches 5 MiB. Flush after each write (`-f`) for real-time monitoring.

### Rotation + grep + timestamps + strip

```cmd
colored-app.exe 2>&1 | tee.exe --grep "warn" --rotate 20M --keep 5 -ets warnings.log
```

Console shows all colored output (`-e`). `warnings.log` gets only warning lines, with ANSI codes stripped (`-s`), timestamps (`-t`), rotated at 20 MiB.

### Chain multiple tee instances for different filters

```cmd
build.exe 2>&1 | tee.exe --grep "error" errors.log | tee.exe --grep "warn" warnings.log > full.log
```

The first `tee` filters errors to `errors.log` and passes all data to stdout. The second `tee` filters warnings to `warnings.log`. The final redirect saves the complete output to `full.log`.

### Full-featured production pipeline

```cmd
server.exe 2>&1 | tee.exe --rotate 100M --keep 20 --grep "error" -etsf errors.log
```

All options combined:
- `--rotate 100M` -- rotate error log at 100 MiB
- `--keep 20` -- retain 20 rotated files
- `--grep "error"` -- only capture error lines
- `-e` -- render ANSI colors in console
- `-t` -- add UTC timestamps to each error line
- `-s` -- strip ANSI codes from file output
- `-f` -- flush after each write for real-time monitoring

### Stop parsing options with `--`

```cmd
some-tool.exe | tee.exe -- -weird-filename.txt
```

The `--` ensures that `-weird-filename.txt` is treated as a file name, not as an option.

## License

MIT License. Copyright (c) 2026 Olivier Levon. Copyright (c) 2024 "dEajL3kA".

See [LICENSE.txt](LICENSE.txt) for the full license text.
