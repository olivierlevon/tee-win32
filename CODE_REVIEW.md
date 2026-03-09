# Code Review: tee-win32

**Date:** 2026-03-09
**Reviewer:** Claude (automated review)
**Version reviewed:** v1.3.4 (commit a8cf6fe)

## Overview

Native implementation of the Unix `tee` command for Windows, written in C using the Win32 API directly (no CRT). Uses multi-threaded I/O with triple buffering and SRW locks for synchronization.

**Overall quality: 7.5/10** - Well-written, compact and performant code with a few areas needing attention.

---

## 1. Security

### 1.1 `TerminateThread()` usage - CRITICAL

**File:** `tee.c:676`

`TerminateThread()` is dangerous per Microsoft's own documentation. It terminates a thread without releasing its resources (locks, memory, DLL state). If the thread holds an SRW lock at termination time, it can corrupt the internal process state.

**Recommendation:** Use a cooperative signaling mechanism (e.g., `g_stop` + timeout) so threads terminate cleanly.

### 1.2 `FatalExit()` is obsolete - MINOR

**File:** `tee.c:41`

`FatalExit` is deprecated. Use `__debugbreak()` or `RaiseException` in debug builds for proper debugger integration.

---

## 2. Concurrency & Synchronization

### 2.1 Triple-buffering architecture - GOOD

The SRW lock + condition variable pattern for triple buffering is solid and correct. Producer (main thread) acquires exclusive locks, consumers (writer threads) acquire shared locks.

### 2.2 Mixed atomic/non-atomic operations on `g_pending[]` - CAUTION

**Files:** `tee.c:304`, `tee.c:335`, `tee.c:592`, `tee.c:622`

`g_pending` is accessed via both `_InterlockedIncrement/_InterlockedDecrement` (line 335) and direct assignment (line 622). While this works on x86/x64 (strongly-ordered memory model), it could be problematic on **ARM64** (weakly-ordered memory). The implicit memory barriers from SRW locks should ensure correctness, but this is fragile.

**Recommendation:** Use `InterlockedCompareExchange(addr, 0, 0)` for atomic reads and `InterlockedExchange` for atomic writes.

### 2.3 Shutdown signals only one buffer - POTENTIAL BUG

**File:** `tee.c:653-663`

Only the current buffer (`myIndex`) is signaled for shutdown. If a writer thread is waiting on a *different* buffer index, it will never receive the termination signal and will remain blocked until the 10-second timeout triggers `TerminateThread`.

**Recommendation:** Signal termination on **all** buffers, not just the current one.

### 2.4 25-second timeout for pending writes - CAUTION

**File:** `tee.c:656`

A 25-second timeout is very long. If a writer thread is blocked on slow I/O (e.g., network disk), the program will hang. Consider making this configurable or using a shorter timeout with retry logic.

---

## 3. Performance

### 3.1 `lstrcatW()` in `concat_va()` - O(n^2) complexity

**File:** `tee.c:138`

`lstrcatW` scans the destination string from the beginning each time (Shlemiel the painter's algorithm). With N strings, this is O(n^2).

**Recommendation:** Maintain a destination pointer and use `memcpy` or `lstrcpyW` with a cumulative offset.

### 3.2 Buffer size could be larger for bulk I/O

**File:** `tee.c:28`

`BUFFER_SIZE` is 8192 bytes on x64 / 4096 on x86. For large file operations, 64KB-128KB buffers would be more performant. Consider making this configurable.

### 3.3 `FlushFileBuffers` per write is costly

**File:** `tee.c:348`

Flushing after every write is very expensive I/O-wise. This is behind the `-f` flag so it's a conscious user choice, but the performance impact should be documented.

---

## 4. Robustness & Correctness

### 4.1 No bounds check on `CommandLineToArgvW` result

**File:** `tee.c:723`

If `nArgs` returns 0 (theoretically possible), `wmain` would be called with `argc=0` and `argv[1]` would be out-of-bounds. Windows guarantees at least `argv[0]` in practice, but a defensive check would be prudent.

### 4.2 `FILE_SHARE_DELETE` not set on output files

**File:** `tee.c:542`

Output files are opened with `FILE_SHARE_READ` only. Adding `FILE_SHARE_DELETE` would allow other processes to rename/delete the file during writing, which is sometimes desirable in Unix-like pipelines.

### 4.3 Macro `CLOSE_HANDLE` evaluates argument multiple times

**File:** `tee.c:159-167`

The `HANDLE` parameter is evaluated multiple times. This is safe in current usage (always called with simple variables), but fragile if misused with expressions having side effects.

---

## 5. Maintenance

### 5.1 Default MSVC path is VS 2019

**File:** `make.cmd:5`

The default path points to VS 2019 Community. VS 2022 is the current version. The script allows override via `%MSVC_PATH%`, but the default is outdated.

---

## 6. Positive Aspects

- Clean, compact, well-organized code
- Elegant multi-threaded architecture with triple buffering
- No CRT dependency (custom `_startup` entry point) - minimal binary size
- Proper pipe handling (`ERROR_BROKEN_PIPE`, zero-byte reads)
- Good null device detection (NUL)
- `SecureZeroMemory` usage prevents optimizer from removing zero-fills
- Correct `do { ... } while(0)` pattern for multi-statement macros
- Clear error messages throughout
- Multi-architecture support (x86, x64, ARM64)

---

## Summary

| Category | Severity | Count |
|----------|----------|-------|
| Critical | `TerminateThread`, partial shutdown signal | 2 |
| Medium   | ARM64 atomics, concat O(n^2), long timeout | 3 |
| Minor    | Obsolete FatalExit, VS 2019 default, macro multi-eval | 3 |
| Positive | Architecture, triple-buffering, no CRT, pipe handling | - |

### Top 4 Recommended Fixes

1. **Signal termination on all buffers** (not just `myIndex`) to avoid thread deadlock on shutdown
2. **Replace `TerminateThread` with cooperative signal** using `g_stop`
3. **Make `g_pending` reads/writes consistently atomic** (important for ARM64 correctness)
4. **Fix `concat_va`** to avoid quadratic complexity
