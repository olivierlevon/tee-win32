/*
 * tee for Windows
 * Copyright (c) 2024 "dEajL3kA" <Cumpoing79@web.de>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
 * associated documentation files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge, publish, distribute,
 * sub license, and/or sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions: The above copyright notice and this
 * permission notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
 * NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
 * OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#define WIN32_LEAN_AND_MEAN 1
#include <Windows.h>
#include <ShellAPI.h>
#include <intrin.h>
#include <stdarg.h>
#include "include/cpu.h"
#include "include/version.h"
#include "include/ansiconv.h"
#include <pcre2.h>

#pragma intrinsic(_InterlockedIncrement, _InterlockedDecrement, _InterlockedExchange, _InterlockedCompareExchange)

#define ATOMIC_READ(PTR)        _InterlockedCompareExchange((PTR), 0L, 0L)
#define ATOMIC_WRITE(PTR, VAL)  _InterlockedExchange((PTR), (VAL))

#define BUFFER_SIZE (PROCESSOR_BITNESS * 128U)
#define BUFFERS 3U
#define MAX_THREADS MAXIMUM_WAIT_OBJECTS
#define GREP_LINE_BUF_SIZE 65536U

// --------------------------------------------------------------------------
// Assertions
// --------------------------------------------------------------------------

#ifndef NDEBUG
#define ASSERT(CONDITION, HANDLE_OUT, MESSAGE) do { \
    static const wchar_t *const _message = L"[tee] Assertion Failed: " MESSAGE L"\n"; \
    if (!(CONDITION)) { \
        write_text((HANDLE_OUT), _message); \
        __debugbreak(); \
    } \
} while(0)
#else
#define ASSERT(CONDITION, HANDLE_OUT, MESSAGE) ((void)0)
#endif

// --------------------------------------------------------------------------
// Utilities
// --------------------------------------------------------------------------

static wchar_t to_lower(const wchar_t c)
{
    wchar_t buf = c;
    CharLowerBuffW(&buf, 1);
    return buf;
}

static BOOL is_terminal(const HANDLE handle)
{
    DWORD mode;
    return GetConsoleMode(handle, &mode);
}

static DWORD count_handles(const HANDLE *const array, const size_t maximum)
{
    DWORD counter;
    for (counter = 0U; counter < (DWORD)maximum; ++counter)
    {
        if (!array[counter])
        {
            break;
        }
    }

    return counter;
}

static const wchar_t *get_filename(const wchar_t *filePath)
{
    for (const wchar_t *ptr = filePath; *ptr != L'\0'; ++ptr)
    {
        if ((*ptr == L'\\') || (*ptr == L'/'))
        {
            filePath = ptr + 1U;
        }
    }

    return filePath;
}

static BOOL is_null_device(const wchar_t *filePath)
{
    filePath = get_filename(filePath);
    if ((to_lower(filePath[0U]) == L'n') && (to_lower(filePath[1U]) == L'u') && (to_lower(filePath[2U]) == L'l'))
    {
        return ((filePath[3U] == L'\0') || (filePath[3U] == L'.'));
    }

    return FALSE;
}

static wchar_t *format_string(const wchar_t *const format, ...)
{
    wchar_t* buffer = NULL;
    va_list ap;

    va_start(ap, format);
    const DWORD result = FormatMessageW(FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ALLOCATE_BUFFER, format, 0U, 0U, (LPWSTR)&buffer, 1U, &ap);
    va_end(ap);

    if ((!result) && buffer)
    {
        LocalFree(buffer);
        buffer = NULL;
    }

    return buffer;
}

static wchar_t *concat_va(const wchar_t *const first, ...)
{
    const wchar_t *ptr;
    va_list ap;

    va_start(ap, first);
    size_t len = 0U;
    for (ptr = first; ptr != NULL; ptr = va_arg(ap, const wchar_t*))
    {
        len += lstrlenW(ptr);
    }
    va_end(ap);

    wchar_t *const buffer = (wchar_t*)LocalAlloc(LPTR, sizeof(wchar_t) * (len + 1U));
    if (buffer)
    {
        wchar_t *dest = buffer;
        va_start(ap, first);
        for (ptr = first; ptr != NULL; ptr = va_arg(ap, const wchar_t*))
        {
            const int ptrLen = lstrlenW(ptr);
            if (ptrLen > 0)
            {
                CopyMemory(dest, ptr, sizeof(wchar_t) * ptrLen);
                dest += ptrLen;
            }
        }
        va_end(ap);
        *dest = L'\0';
    }

    return buffer;
}

#define CONCAT(...) concat_va(__VA_ARGS__, NULL)

#define VALID_HANDLE(HANDLE) (((HANDLE) != NULL) && ((HANDLE) != INVALID_HANDLE_VALUE))

#define FILL_ARRAY(ARRAY, VALUE) do \
{ \
    for (size_t _index = 0U; _index < ARRAYSIZE(ARRAY); ++_index) \
    { \
        ARRAY[_index] = (VALUE); \
    } \
} \
while (0)

#define CLOSE_HANDLE(HANDLE) do \
{ \
    if (VALID_HANDLE(HANDLE)) \
    { \
        CloseHandle((HANDLE)); \
        (HANDLE) = NULL; \
    } \
} \
while (0)

// --------------------------------------------------------------------------
// Console CTRL+C handler
// --------------------------------------------------------------------------

static volatile LONG g_stop = FALSE;

static BOOL WINAPI console_handler(const DWORD ctrlType)
{
    switch (ctrlType)
    {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
        _InterlockedExchange(&g_stop, TRUE);
        return TRUE;
    default:
        return FALSE;
    }
}

// --------------------------------------------------------------------------
// Text output
// --------------------------------------------------------------------------

static char *utf16_to_utf8(const wchar_t *const input)
{
    const int buff_size = WideCharToMultiByte(CP_UTF8, 0, input, -1, NULL, 0, NULL, NULL);
    if (buff_size > 0)
    {
        char *const buffer = (char*)LocalAlloc(LPTR, buff_size);
        if (buffer)
        {
            const int result = WideCharToMultiByte(CP_UTF8, 0, input, -1, buffer, buff_size, NULL, NULL);
            if ((result > 0) && (result <= buff_size))
            {
                return buffer;
            }
            LocalFree(buffer);
        }
    }

    return NULL;
}

static BOOL write_text(const HANDLE handle, const wchar_t *const text)
{
    BOOL result = FALSE;
    DWORD written;

    if (GetConsoleMode(handle, &written))
    {
        result = WriteConsoleW(handle, text, lstrlenW(text), &written, NULL);
    }
    else
    {
        char *const utf8_text = utf16_to_utf8(text);
        if (utf8_text)
        {
            result = WriteFile(handle, utf8_text, lstrlenA(utf8_text), &written, NULL);
            LocalFree(utf8_text);
        }
    }

    return result;
}

#define WRITE_TEXT(...) do \
{ \
    wchar_t* const _message = CONCAT(__VA_ARGS__); \
    if (_message) \
    { \
        write_text(hStdErr, _message); \
        LocalFree(_message); \
    } \
} \
while (0)

// --------------------------------------------------------------------------
// Condition variables
// --------------------------------------------------------------------------

static __forceinline void sleep_condvar_srw(const HANDLE hStdErr, const PCONDITION_VARIABLE condvar, const PSRWLOCK lock, const DWORD timeout, const BOOL sharedMode)
{
    const ULONG flags = sharedMode ? CONDITION_VARIABLE_LOCKMODE_SHARED : 0U;
    if (!SleepConditionVariableSRW(condvar, lock, timeout, flags))
    {
        if (GetLastError() != ERROR_TIMEOUT)
        {
            write_text(hStdErr, L"[tee] Operating system error: SleepConditionVariableSRW() has failed!\n");
            TerminateProcess(GetCurrentProcess(), 1U);
        }
    }
}

// --------------------------------------------------------------------------
// Writer thread
// --------------------------------------------------------------------------

#define INCREMENT_INDEX(INDEX, FLAG) do \
{ \
    if (++(INDEX) >= BUFFERS) \
    { \
        (INDEX) = 0U; \
        (FLAG) = (!(FLAG)); \
    } \
} \
while (0)

typedef struct _thread
{
    HANDLE hOutput, hError;
    BOOL flush;
    ansi_conv_t *conv;
    /* Grep filtering (PCRE2) */
    pcre2_code *grep_regex;             /* compiled regex, NULL = disabled */
    pcre2_match_data *grep_match_data;  /* per-thread match data */
    BYTE *grep_line_buf;                /* line accumulation buffer */
    DWORD grep_line_len;
    /* Log rotation */
    wchar_t *fileName;          /* output file path (for rotate) */
    ULONGLONG rotate_size;      /* 0 = disabled */
    DWORD keep_count;
}
thread_t;

static BYTE g_buffer[BUFFERS][BUFFER_SIZE];
static DWORD g_bytesTotal[BUFFERS] = { 0 };
static volatile LONG g_pending[BUFFERS] = { 0 };
static SRWLOCK g_rwLocks[BUFFERS];
static CONDITION_VARIABLE g_condIsReady[BUFFERS], g_condAllDone[BUFFERS];

/* Forward declarations */
static BOOL line_matches(const BYTE *line, DWORD line_len, const pcre2_code *regex, pcre2_match_data *match_data);
static DWORD grep_filter_buffer(thread_t *param, const BYTE *data, DWORD size, BYTE *out_buf, DWORD out_cap);
static void rotate_file(thread_t *param);

static DWORD WINAPI writer_thread_start_routine(const LPVOID lpThreadParameter)
{
    DWORD bytesWritten = 0U, myIndex = 0U;
    LONG pending = 0L;
    BOOL myFlag = TRUE, writeErrors = FALSE;
    PSRWLOCK rwLock = NULL;
    thread_t *const param = (thread_t*)lpThreadParameter;
    BYTE filteredBuf[BUFFER_SIZE];

    /* Write format header (e.g., HTML preamble) if converter is active */
    if (param->conv)
    {
        ansi_conv_write_header(param->conv);
    }

    for (;;)
    {
        ASSERT(myIndex < BUFFERS, param->hError, L"Current buffer index is out of range!");

        AcquireSRWLockShared(rwLock = &g_rwLocks[myIndex]);

        pending = ATOMIC_READ(&g_pending[myIndex]);

        while (!(myFlag ? (pending > 0L) : (pending < 0L)))
        {
            sleep_condvar_srw(param->hError, &g_condIsReady[myIndex], rwLock, INFINITE, TRUE);
            pending = ATOMIC_READ(&g_pending[myIndex]);
        }

        const DWORD bytesTotal = g_bytesTotal[myIndex];
        if (bytesTotal > BUFFER_SIZE)
        {
            /* Flush remaining grep line buffer before exit */
            if (param->grep_regex && param->grep_line_len > 0U)
            {
                if (line_matches(param->grep_line_buf, param->grep_line_len, param->grep_regex, param->grep_match_data))
                {
                    if (param->conv)
                        ansi_conv_write(param->conv, param->grep_line_buf, param->grep_line_len);
                    else
                        WriteFile(param->hOutput, param->grep_line_buf, param->grep_line_len, &bytesWritten, NULL);
                }
                param->grep_line_len = 0U;
            }

            ReleaseSRWLockShared(rwLock);
            /* Write format footer (e.g., HTML closing tags) before exiting */
            if (param->conv)
            {
                ansi_conv_write_footer(param->conv);
            }
            if (writeErrors)
            {
                write_text(param->hError, L"[tee] I/O error: Not all data could be written!\n");
            }
            return 0U;
        }

        /* Determine data to write (apply grep filter if active) */
        const BYTE *writeData = g_buffer[myIndex];
        DWORD writeLen = bytesTotal;

        if (param->grep_regex)
        {
            writeLen = grep_filter_buffer(param, g_buffer[myIndex], bytesTotal, filteredBuf, BUFFER_SIZE);
            writeData = filteredBuf;
        }

        if (writeLen > 0U)
        {
            if (param->conv)
            {
                if (!ansi_conv_write(param->conv, writeData, writeLen))
                {
                    writeErrors = TRUE;
                }
            }
            else
            {
                for (DWORD offset = 0U; offset < writeLen; offset += bytesWritten)
                {
                    const BOOL result = WriteFile(param->hOutput, writeData + offset, writeLen - offset, &bytesWritten, NULL);
                    if ((!result) || (!bytesWritten))
                    {
                        writeErrors = TRUE;
                        break;
                    }
                }
            }

            /* Check if log rotation is needed */
            if (param->rotate_size > 0ULL && param->fileName && VALID_HANDLE(param->hOutput))
            {
                LARGE_INTEGER fileSize;
                if (GetFileSizeEx(param->hOutput, &fileSize))
                {
                    if ((ULONGLONG)fileSize.QuadPart >= param->rotate_size)
                    {
                        rotate_file(param);
                    }
                }
            }
        }

        ASSERT(ATOMIC_READ(&g_pending[myIndex]) != 0L, param->hError, L"Pending threads counter must be a non-zero value!");

        pending = myFlag ? _InterlockedDecrement(&g_pending[myIndex]) : _InterlockedIncrement(&g_pending[myIndex]);

        ReleaseSRWLockShared(rwLock);

        if (!pending)
        {
            WakeConditionVariable(&g_condAllDone[myIndex]);
        }

        INCREMENT_INDEX(myIndex, myFlag);

        if (param->flush)
        {
            FlushFileBuffers(param->hOutput);
        }
    }
}

// --------------------------------------------------------------------------
// Size and integer parsing
// --------------------------------------------------------------------------

static BOOL parse_size(const char *str, ULONGLONG *result)
{
    ULONGLONG val = 0ULL;
    const char *p = str;

    while (*p >= '0' && *p <= '9')
    {
        val = val * 10ULL + (ULONGLONG)(*p - '0');
        p++;
    }

    if (p == str)
        return FALSE;

    char suffix = *p;
    if (suffix >= 'A' && suffix <= 'Z') suffix = (char)(suffix + 32);
    if (suffix == 'k')      { val *= 1024ULL; p++; }
    else if (suffix == 'm') { val *= 1024ULL * 1024ULL; p++; }
    else if (suffix == 'g') { val *= 1024ULL * 1024ULL * 1024ULL; p++; }
    else if (suffix != '\0') return FALSE;

    if (*p != '\0' || val == 0ULL)
        return FALSE;

    *result = val;
    return TRUE;
}

static DWORD parse_uint(const char *str)
{
    DWORD val = 0U;
    if (!str || *str == '\0')
        return 0U;
    while (*str >= '0' && *str <= '9')
    {
        val = val * 10U + (DWORD)(*str - '0');
        str++;
    }
    return (*str == '\0') ? val : 0U;
}

// --------------------------------------------------------------------------
// PCRE2 custom allocators (for /NODEFAULTLIB Release builds)
// --------------------------------------------------------------------------

static void *pcre2_local_alloc(PCRE2_SIZE size, void *data)
{
    (void)data;
    return LocalAlloc(LMEM_FIXED, size);
}

static void pcre2_local_free(void *ptr, void *data)
{
    (void)data;
    if (ptr) LocalFree(ptr);
}

static pcre2_general_context *g_pcre2_gctx = NULL;

// --------------------------------------------------------------------------
// Grep helpers (PCRE2-based)
// --------------------------------------------------------------------------

static BOOL line_matches(const BYTE *line, DWORD line_len, const pcre2_code *regex, pcre2_match_data *match_data)
{
    if (!regex || !match_data)
        return FALSE;
    return pcre2_match(regex, (PCRE2_SPTR)line, (PCRE2_SIZE)line_len, 0, 0, match_data, NULL) >= 0;
}

static DWORD grep_filter_buffer(thread_t *param, const BYTE *data, DWORD size, BYTE *out_buf, DWORD out_cap)
{
    DWORD out_len = 0U, i;

    for (i = 0U; i < size; i++)
    {
        if (param->grep_line_len < GREP_LINE_BUF_SIZE)
            param->grep_line_buf[param->grep_line_len++] = data[i];

        if (data[i] == '\n')
        {
            if (line_matches(param->grep_line_buf, param->grep_line_len, param->grep_regex, param->grep_match_data))
            {
                const DWORD copy_len = (param->grep_line_len <= out_cap - out_len) ? param->grep_line_len : 0U;
                if (copy_len > 0U)
                {
                    CopyMemory(out_buf + out_len, param->grep_line_buf, copy_len);
                    out_len += copy_len;
                }
            }
            param->grep_line_len = 0U;
        }
    }
    return out_len;
}

// --------------------------------------------------------------------------
// Log rotation helpers
// --------------------------------------------------------------------------

static wchar_t *format_rotated_name(const wchar_t *baseName, DWORD index)
{
    return format_string(L"%1!s!.%2!u!", baseName, index);
}

static void rotate_file(thread_t *param)
{
    DWORD i;

    /* Flush and finalize current file */
    if (param->conv)
    {
        ansi_conv_flush(param->conv);
        ansi_conv_write_footer(param->conv);
    }

    CloseHandle(param->hOutput);

    /* Delete the oldest rotated file */
    {
        wchar_t *const oldest = format_rotated_name(param->fileName, param->keep_count);
        if (oldest)
        {
            DeleteFileW(oldest);
            LocalFree(oldest);
        }
    }

    /* Shift rotated files: .N-1 -> .N, .N-2 -> .N-1, ..., base -> .1 */
    for (i = param->keep_count; i >= 1U; --i)
    {
        wchar_t *const dst = format_rotated_name(param->fileName, i);
        wchar_t *const src = (i > 1U) ? format_rotated_name(param->fileName, i - 1U) : NULL;
        const wchar_t *const srcName = (i == 1U) ? param->fileName : src;
        if (dst && srcName)
            MoveFileW(srcName, dst);
        if (src) LocalFree(src);
        if (dst) LocalFree(dst);
    }

    /* Reopen the base file */
    param->hOutput = CreateFileW(param->fileName, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, CREATE_ALWAYS, 0U, NULL);
    if (!VALID_HANDLE(param->hOutput))
    {
        write_text(param->hError, L"[tee] Error: Failed to reopen file after rotation!\n");
        return;
    }

    /* Reset converter for the new file */
    if (param->conv)
    {
        ansi_conv_reset_for_rotation(param->conv, param->hOutput);
        ansi_conv_write_header(param->conv);
    }
}

// --------------------------------------------------------------------------
// Options
// --------------------------------------------------------------------------

typedef struct
{
    BOOL append, buffer, delay, escape, flush, help, html, ignore, linenumber, strip, timestamp, version;
    ULONGLONG rotate_size;          /* 0 = disabled */
    DWORD keep_count;               /* rotated files to keep (default 5) */
    const char *grep_pattern;       /* UTF-8 pattern, NULL = disabled */
}
options_t;

static char to_lower_ascii(const char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static int stricmp_ascii(const char *a, const char *b)
{
    while (*a && *b)
    {
        const char la = to_lower_ascii(*a), lb = to_lower_ascii(*b);
        if (la != lb) return la - lb;
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

#define PARSE_OPTION(SHRT, NAME) do \
{ \
    if ((lc == SHRT) || (name && (stricmp_ascii(name, #NAME) == 0))) \
    { \
        options->NAME = TRUE; \
        return TRUE; \
    } \
} \
while (0)

static BOOL parse_option(options_t *const options, const char c, const char *const name)
{
    const char lc = to_lower_ascii(c);

    PARSE_OPTION('a', append);
    PARSE_OPTION('b', buffer);
    PARSE_OPTION('d', delay);
    PARSE_OPTION('e', escape);
    PARSE_OPTION('f', flush);
    PARSE_OPTION('h', help);
    PARSE_OPTION('\0', html);
    PARSE_OPTION('i', ignore);
    PARSE_OPTION('n', linenumber);
    PARSE_OPTION('s', strip);
    PARSE_OPTION('t', timestamp);
    PARSE_OPTION('v', version);

    return FALSE;
}

static BOOL parse_argument(options_t *const options, const char *const argument)
{
    if ((argument[0U] != '-') || (argument[1U] == '\0'))
    {
        return FALSE;
    }

    if (argument[1U] == '-')
    {
        return (argument[2U] != '\0') && parse_option(options, '\0', argument + 2U);
    }
    else
    {
        for (const char* ptr = argument + 1U; *ptr != '\0'; ++ptr)
        {
            if (!parse_option(options, *ptr, NULL))
            {
                return FALSE;
            }
        }
        return TRUE;
    }
}

// --------------------------------------------------------------------------
// Help screen
// --------------------------------------------------------------------------

static void print_helpscreen(const HANDLE hStdErr, const BOOL full)
{
    wchar_t* const versionString = format_string(L"tee for Windows v%1!u!.%2!u!.%3!u! [%4!s!] [%5!s!]\n", APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_VERSION_PATCH, PROCESSOR_ARCHITECTURE, TEXT(__DATE__));
    write_text(hStdErr, versionString ? versionString : L"tee for Windows\n");
    if (full)
    {
        write_text(hStdErr, L"\n"
            L"Copy standard input to output file(s), and also to standard output.\n\n"
            L"Usage:\n"
            L"  gizmo.exe [...] | tee.exe [options] <file_1> ... <file_n>\n\n"
            L"Options:\n"
            L"  -a --append      Append to the existing file, instead of truncating\n"
            L"  -b --buffer      Enable write combining, i.e. buffer small chunks\n"
            L"  -e --escape      Enable standard output ANSI escape code processing\n"
            L"  -f --flush       Flush output file after each write operation\n"
            L"  -i --ignore      Ignore the interrupt signal (SIGINT), e.g. CTRL+C\n"
            L"  -n --linenumber  Add line numbers to output file(s)\n"
            L"  -s --strip       Strip ANSI escape codes from output file(s)\n"
            L"  -t --timestamp   Add ISO 8601 UTC timestamps to output file(s)\n"
            L"     --html        Convert ANSI escape codes to HTML in output file(s)\n"
            L"  -d --delay       Add a small delay after each read operation\n"
            L"     --grep <pat>  Only write lines matching regex <pat> to output file(s)\n"
            L"     --rotate <sz> Rotate output file(s) when they reach <sz> (e.g., 50M)\n"
            L"     --keep <n>    Keep <n> rotated files (default: 5)\n\n");
    }
    if (versionString)
    {
        LocalFree(versionString);
    }
}

// --------------------------------------------------------------------------
// MAIN
// --------------------------------------------------------------------------

static wchar_t *utf8_to_utf16(const char *const input)
{
    const int buff_size = MultiByteToWideChar(CP_UTF8, 0, input, -1, NULL, 0);
    if (buff_size > 0)
    {
        wchar_t *const buffer = (wchar_t*)LocalAlloc(LPTR, sizeof(wchar_t) * buff_size);
        if (buffer)
        {
            const int result = MultiByteToWideChar(CP_UTF8, 0, input, -1, buffer, buff_size);
            if ((result > 0) && (result <= buff_size))
            {
                return buffer;
            }
            LocalFree(buffer);
        }
    }

    return NULL;
}

int tee_main(const int argc, char *const argv[])
{
    HANDLE hThreads[MAX_THREADS], hMyFiles[MAX_THREADS - 1U];
    int exitCode = 1, argOff = 1;
    BOOL myFlag = TRUE, readErrors = FALSE;
    DWORD fileCount = 0U, threadCount = 0U, myIndex = 0U, bytesRead = 0U, totalBytes = 0U, outputCount = 0U;
    PSRWLOCK rwLock = NULL;
    options_t options;
    static thread_t threadData[MAX_THREADS];
    pcre2_code *grep_regex = NULL;
    wchar_t *fileNamesW[MAX_THREADS - 1U];

    /* Initialize local variables */
    FILL_ARRAY(hMyFiles, INVALID_HANDLE_VALUE);
    FILL_ARRAY(hThreads, NULL);
    SecureZeroMemory(&options, sizeof(options));
    SecureZeroMemory(&threadData, sizeof(threadData));
    SecureZeroMemory(fileNamesW, sizeof(fileNamesW));

    /* Initialize standard streams */
    const HANDLE hStdIn = GetStdHandle(STD_INPUT_HANDLE), hStdOut = GetStdHandle(STD_OUTPUT_HANDLE), hStdErr = GetStdHandle(STD_ERROR_HANDLE);
    if (!(VALID_HANDLE(hStdIn) && VALID_HANDLE(hStdOut) && VALID_HANDLE(hStdErr)))
    {
        if (VALID_HANDLE(hStdErr))
        {
            write_text(hStdErr, L"[tee] Operating system error: GetStdHandle() has failed!\n");
        }
        return -1;
    }

    /* Initialize read/write locks and condition variables */
    for (DWORD index = 0; index < BUFFERS; ++index)
    {
        InitializeSRWLock(&g_rwLocks[index]);
        InitializeConditionVariable(&g_condIsReady[index]);
        InitializeConditionVariable(&g_condAllDone[index]);
    }

    /* Set up CTRL+C handler */
    if (!SetConsoleCtrlHandler(console_handler, TRUE))
    {
        write_text(hStdErr, L"[tee] Warning: Failed to set up console control handler!\n");
    }

    /* Initialize PCRE2 general context with custom allocators */
    g_pcre2_gctx = pcre2_general_context_create(pcre2_local_alloc, pcre2_local_free, NULL);

    /* Parse command-line options (argv is now UTF-8) */
    while ((argOff < argc) && (argv[argOff][0U] == '-') && (argv[argOff][1U] != '\0'))
    {
        const char *const argValue = argv[argOff++];
        if ((argValue[1U] == '-') && (argValue[2U] == '\0'))
        {
            break; /*stop!*/
        }
        else if (stricmp_ascii(argValue, "--rotate") == 0)
        {
            if (argOff >= argc)
            {
                write_text(hStdErr, L"[tee] Error: Option --rotate requires a size value (e.g., 50M)\n");
                return 1;
            }
            if (!parse_size(argv[argOff++], &options.rotate_size))
            {
                write_text(hStdErr, L"[tee] Error: Invalid size value for --rotate\n");
                return 1;
            }
        }
        else if (stricmp_ascii(argValue, "--keep") == 0)
        {
            if (argOff >= argc)
            {
                write_text(hStdErr, L"[tee] Error: Option --keep requires a numeric value\n");
                return 1;
            }
            options.keep_count = parse_uint(argv[argOff++]);
            if (options.keep_count == 0U)
            {
                write_text(hStdErr, L"[tee] Error: Invalid value for --keep (must be >= 1)\n");
                return 1;
            }
        }
        else if (stricmp_ascii(argValue, "--grep") == 0)
        {
            if (argOff >= argc)
            {
                write_text(hStdErr, L"[tee] Error: Option --grep requires a pattern\n");
                return 1;
            }
            options.grep_pattern = argv[argOff++];
        }
        else if (!parse_argument(&options, argValue))
        {
            wchar_t *const argW = utf8_to_utf16(argValue);
            WRITE_TEXT(L"[tee] Error: Invalid option \"", argW ? argW : L"?", L"\" encountered!\n");
            if (argW) LocalFree(argW);
            return 1;
        }
    }

    /* Print manual page */
    if (options.help || options.version)
    {
        print_helpscreen(hStdErr, options.help);
        return 0;
    }

    /* Validate mutually exclusive options */
    if (options.html && options.strip)
    {
        write_text(hStdErr, L"[tee] Error: Options --html and --strip are mutually exclusive!\n");
        return 1;
    }

    /* Default keep count */
    if (options.rotate_size > 0ULL && options.keep_count == 0U)
        options.keep_count = 5U;

    if (options.keep_count > 0U && options.rotate_size == 0ULL)
    {
        write_text(hStdErr, L"[tee] Warning: --keep is ignored without --rotate\n");
    }

    /* Determine output format for files */
    const output_format_t outputFormat = options.html ? FMT_HTML : (options.strip ? FMT_STRIP : FMT_RAW);

    /* Check output file name */
    if (argOff >= argc)
    {
        write_text(hStdErr, L"[tee] Error: Output file name is missing. Type \"tee --help\" for details!\n");
        return 1;
    }

    /* Determine input type */
    const DWORD inputType = GetFileType(hStdIn);
    if (inputType == FILE_TYPE_UNKNOWN)
    {
        if (GetLastError() != NO_ERROR)
        {
            write_text(hStdErr, L"[tee] Operating system error: GetFileType(hStdIn) has failed!\n");
            return -1;
        }
    }

    /* Enable ANSI escape code processing of stdout */
    if (options.escape)
    {
        DWORD stdOutMode = 0U;
        if (GetConsoleMode(hStdOut, &stdOutMode))
        {
            SetConsoleMode(hStdOut, stdOutMode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        }
    }

    /* Compile grep regex pattern with PCRE2 */
    if (options.grep_pattern)
    {
        int errcode;
        PCRE2_SIZE erroffset;
        grep_regex = pcre2_compile(
            (PCRE2_SPTR)options.grep_pattern,
            PCRE2_ZERO_TERMINATED,
            PCRE2_CASELESS,
            &errcode, &erroffset,
            NULL);
        if (!grep_regex)
        {
            PCRE2_UCHAR errbuf[256];
            pcre2_get_error_message(errcode, errbuf, sizeof(errbuf));
            wchar_t *const errW = utf8_to_utf16((const char*)errbuf);
            WRITE_TEXT(L"[tee] Error: Invalid regex pattern: ", errW ? errW : L"unknown error", L"\n");
            if (errW) LocalFree(errW);
            return 1;
        }
    }

    /* Open output file(s) -- convert UTF-8 filenames to UTF-16 for Win32 API */
    while ((argOff < argc) && (fileCount < ARRAYSIZE(hMyFiles)))
    {
        const char *const fileName = argv[argOff++];
        wchar_t *const fileNameW = utf8_to_utf16(fileName);
        if (!fileNameW)
        {
            write_text(hStdErr, L"[tee] Error: Failed to convert file name to UTF-16!\n");
            goto cleanUp;
        }
        if (!is_null_device(fileNameW))
        {
            const HANDLE hFile = CreateFileW(fileNameW, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, options.append ? OPEN_ALWAYS : CREATE_ALWAYS, 0U, NULL);
            fileNamesW[fileCount] = fileNameW;
            if ((hMyFiles[fileCount++] = hFile) == INVALID_HANDLE_VALUE)
            {
                WRITE_TEXT(L"[tee] Error: Failed to open the output file \"", fileNameW, L"\" for writing!\n");
                goto cleanUp;
            }
            else if (options.append)
            {
                LARGE_INTEGER offset = { .QuadPart = 0LL };
                if (!SetFilePointerEx(hFile, offset, NULL, FILE_END))
                {
                    write_text(hStdErr, L"[tee] Error: Failed to move the file pointer to the end of the file!\n");
                    goto cleanUp;
                }
            }
        }
        else
        {
            LocalFree(fileNameW);
        }
    }

    /* Check output file name */
    if (argOff < argc)
    {
        write_text(hStdErr, L"[tee] Warning: Too many input files, ignoring excess files!\n");
    }

    /* Determine number of outputs */
    outputCount = fileCount + 1U;

    /* Start threads */
    for (DWORD threadId = 0; threadId < outputCount; ++threadId)
    {
        threadData[threadId].hOutput = (threadId > 0U) ? hMyFiles[threadId - 1U] : hStdOut;
        threadData[threadId].hError = hStdErr;
        threadData[threadId].flush = options.flush && (!is_terminal(threadData[threadId].hOutput));
        threadData[threadId].conv = NULL;
        /* Set up grep filtering for file outputs only (not stdout) */
        if ((threadId > 0U) && grep_regex)
        {
            threadData[threadId].grep_regex = grep_regex;
            threadData[threadId].grep_match_data = pcre2_match_data_create_from_pattern(grep_regex, g_pcre2_gctx);
            threadData[threadId].grep_line_buf = (BYTE*)LocalAlloc(LPTR, GREP_LINE_BUF_SIZE);
            threadData[threadId].grep_line_len = 0U;
            if (!threadData[threadId].grep_line_buf || !threadData[threadId].grep_match_data)
            {
                write_text(hStdErr, L"[tee] Error: Failed to allocate grep resources!\n");
                goto cleanUp;
            }
        }
        /* Set up log rotation for file outputs only */
        if ((threadId > 0U) && options.rotate_size > 0ULL)
        {
            threadData[threadId].fileName = fileNamesW[threadId - 1U];
            threadData[threadId].rotate_size = options.rotate_size;
            threadData[threadId].keep_count = options.keep_count;
        }
        /* Create converter for file outputs (not stdout) when processing is needed */
        if ((threadId > 0U) && (outputFormat != FMT_RAW || options.timestamp || options.linenumber))
        {
            threadData[threadId].conv = ansi_conv_create(outputFormat, threadData[threadId].hOutput, options.timestamp, options.linenumber);
            if (!threadData[threadId].conv)
            {
                write_text(hStdErr, L"[tee] Error: Failed to create ANSI converter!\n");
                goto cleanUp;
            }
        }
        if (!(hThreads[threadCount++] = CreateThread(NULL, 0U, writer_thread_start_routine, (LPVOID)&threadData[threadId], 0U, NULL)))
        {
            write_text(hStdErr, L"[tee] Operating system error: CreateThread() has failed!\n");
            goto cleanUp;
        }
    }

    /* Determine minumum chunk size */
    const DWORD minimumLength = options.buffer ? (BUFFER_SIZE / 8U) : 1U;

    /* Process all input from STDIN stream */
    do
    {
        ASSERT(myIndex < BUFFERS, hStdErr, L"Current buffer index is out of range!");

        AcquireSRWLockExclusive(rwLock = &g_rwLocks[myIndex]);

        while (ATOMIC_READ(&g_pending[myIndex]))
        {
            sleep_condvar_srw(hStdErr, &g_condAllDone[myIndex], rwLock, INFINITE, FALSE);
        }

        BYTE *const ptrBuffer = g_buffer[myIndex];

        for (totalBytes = 0U; totalBytes < minimumLength; totalBytes += bytesRead)
        {
            if (!ReadFile(hStdIn, &ptrBuffer[totalBytes], BUFFER_SIZE - totalBytes, &bytesRead, NULL))
            {
                if (GetLastError() != ERROR_BROKEN_PIPE)
                {
                    readErrors = TRUE;
                }
                break;
            }
            if ((!bytesRead) && (inputType != FILE_TYPE_PIPE))
            {
                break; /*pipes may return zero bytes, even when more data can become available later!*/
            }
        }

        if (!totalBytes)
        {
            ReleaseSRWLockExclusive(rwLock);
            break;
        }

        g_bytesTotal[myIndex] = totalBytes;
        ATOMIC_WRITE(&g_pending[myIndex], myFlag ? ((LONG)threadCount) : (-((LONG)threadCount)));

        ReleaseSRWLockExclusive(&g_rwLocks[myIndex]);
        WakeAllConditionVariable(&g_condIsReady[myIndex]);

        INCREMENT_INDEX(myIndex, myFlag);

        if (readErrors)
        {
            break; /*abort on previous read errors*/
        }

        if (options.delay)
        {
            Sleep(1U);
        }
    }
    while ((!ATOMIC_READ(&g_stop)) || options.ignore);

    /* Check for read errors */
    if (readErrors)
    {
        write_text(hStdErr, L"[tee] I/O error: Failed to read input data!\n");
        goto cleanUp;
    }

    exitCode = 0;

cleanUp:

    /* Wait for the pending writes on the current buffer */
    AcquireSRWLockExclusive(&g_rwLocks[myIndex]);
    while (ATOMIC_READ(&g_pending[myIndex]))
    {
        sleep_condvar_srw(hStdErr, &g_condAllDone[myIndex], &g_rwLocks[myIndex], 25000U, FALSE);
    }
    ReleaseSRWLockExclusive(&g_rwLocks[myIndex]);

    /* Shut down all worker threads: signal termination on ALL buffers */
    for (DWORD bufIdx = 0U; bufIdx < BUFFERS; ++bufIdx)
    {
        BOOL bufFlag = myFlag;
        if (bufIdx != myIndex)
        {
            /* Compute the correct flag polarity for each buffer index */
            DWORD tmpIdx = myIndex;
            BOOL tmpFlag = myFlag;
            while (tmpIdx != bufIdx)
            {
                INCREMENT_INDEX(tmpIdx, tmpFlag);
            }
            bufFlag = tmpFlag;
        }
        AcquireSRWLockExclusive(&g_rwLocks[bufIdx]);
        g_bytesTotal[bufIdx] = MAXDWORD;
        ATOMIC_WRITE(&g_pending[bufIdx], bufFlag ? MAXLONG : MINLONG);
        ReleaseSRWLockExclusive(&g_rwLocks[bufIdx]);
        WakeAllConditionVariable(&g_condIsReady[bufIdx]);
    }

    /* Wait for worker threads to exit (cooperative shutdown) */
    const DWORD pendingThreads = count_handles(hThreads, ARRAYSIZE(hThreads));
    if (pendingThreads > 0U)
    {
        const DWORD result = WaitForMultipleObjects(pendingThreads, hThreads, TRUE, 10000U);
        if (!((result >= WAIT_OBJECT_0) && (result < WAIT_OBJECT_0 + pendingThreads)))
        {
            for (DWORD threadId = 0U; threadId < pendingThreads; ++threadId)
            {
                if (WaitForSingleObject(hThreads[threadId], 125U) != WAIT_OBJECT_0)
                {
                    write_text(hStdErr, L"[tee] Warning: Worker thread did not exit cleanly!\n");
                }
            }
        }
    }

    /* Flush the output file(s), using thread handles (may differ after rotation) */
    if (options.flush)
    {
        for (DWORD threadId = 1U; threadId < outputCount; ++threadId)
        {
            const HANDLE h = threadData[threadId].hOutput;
            if (VALID_HANDLE(h))
            {
                FlushFileBuffers(h);
            }
        }
    }

    /* Close worker threads, destroy converters, free grep buffers */
    for (DWORD threadId = 0U; threadId < ARRAYSIZE(hThreads); ++threadId)
    {
        CLOSE_HANDLE(hThreads[threadId]);
        if (threadData[threadId].conv)
        {
            ansi_conv_destroy(threadData[threadId].conv);
            threadData[threadId].conv = NULL;
        }
        if (threadData[threadId].grep_match_data)
        {
            pcre2_match_data_free(threadData[threadId].grep_match_data);
            threadData[threadId].grep_match_data = NULL;
        }
        if (threadData[threadId].grep_line_buf)
        {
            LocalFree(threadData[threadId].grep_line_buf);
            threadData[threadId].grep_line_buf = NULL;
        }
    }

    /* Update hMyFiles to match current thread handles (may differ after rotation) */
    for (DWORD threadId = 1U; threadId < ARRAYSIZE(hThreads); ++threadId)
    {
        if (threadData[threadId].rotate_size > 0ULL)
            hMyFiles[threadId - 1U] = threadData[threadId].hOutput;
    }

    /* Close the output file(s) */
    for (size_t fileIndex = 0U; fileIndex < ARRAYSIZE(hMyFiles); ++fileIndex)
    {
        CLOSE_HANDLE(hMyFiles[fileIndex]);
    }

    /* Free allocated wide file names */
    for (size_t fileIndex = 0U; fileIndex < ARRAYSIZE(fileNamesW); ++fileIndex)
    {
        if (fileNamesW[fileIndex])
        {
            LocalFree(fileNamesW[fileIndex]);
            fileNamesW[fileIndex] = NULL;
        }
    }

    /* Free PCRE2 regex */
    if (grep_regex)
    {
        pcre2_code_free(grep_regex);
        grep_regex = NULL;
    }

    /* Free PCRE2 general context */
    if (g_pcre2_gctx)
    {
        pcre2_general_context_free(g_pcre2_gctx);
        g_pcre2_gctx = NULL;
    }

    /* Exit */
    return exitCode;
}

// --------------------------------------------------------------------------
// CRT Startup
// --------------------------------------------------------------------------

#ifdef _DEBUG

/* Debug builds: CRT provides entry point that calls wmain; convert to UTF-8 */
int wmain(const int argc, const wchar_t *const argv[])
{
    int i;
    char **argv_utf8 = (char**)LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, (SIZE_T)argc * sizeof(char*));
    if (!argv_utf8) return -1;

    for (i = 0; i < argc; i++)
    {
        const int len = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, NULL, 0, NULL, NULL);
        if (len > 0)
        {
            argv_utf8[i] = (char*)LocalAlloc(LMEM_FIXED, (SIZE_T)len);
            if (argv_utf8[i])
                WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, argv_utf8[i], len, NULL, NULL);
        }
    }

    const int retval = tee_main(argc, argv_utf8);

    for (i = 0; i < argc; i++)
        if (argv_utf8[i]) LocalFree(argv_utf8[i]);
    LocalFree(argv_utf8);

    return retval;
}

#else /* !_DEBUG */

// --------------------------------------------------------------------------
// CRT intrinsic stubs (required for PCRE2 static lib with /NODEFAULTLIB)
// --------------------------------------------------------------------------

#pragma function(memset, memcpy, memmove, memcmp, memchr, strlen)

void *memset(void *dst, int c, size_t n)
{
    volatile unsigned char *p = (volatile unsigned char *)dst;
    while (n--) *p++ = (unsigned char)c;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d < s)
    {
        while (n--) *d++ = *s++;
    }
    else if (d > s)
    {
        d += n; s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

int memcmp(const void *s1, const void *s2, size_t n)
{
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;
    while (n--)
    {
        if (*p1 != *p2) return *p1 - *p2;
        p1++; p2++;
    }
    return 0;
}

void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = (const unsigned char *)s;
    while (n--)
    {
        if (*p == (unsigned char)c) return (void *)p;
        p++;
    }
    return NULL;
}

size_t strlen(const char *s)
{
    const char *p = s;
    while (*p) p++;
    return (size_t)(p - s);
}

char *strchr(const char *s, int c)
{
    while (*s)
    {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return (c == '\0') ? (char *)s : NULL;
}

void *malloc(size_t size)
{
    return HeapAlloc(GetProcessHeap(), 0, size);
}

void free(void *ptr)
{
    if (ptr) HeapFree(GetProcessHeap(), 0, ptr);
}

#pragma warning(disable: 4702)

#ifdef __cplusplus
extern "C"
#endif
int _startup(void)
{
    int i;
    SetErrorMode(SEM_FAILCRITICALERRORS);

    int nArgs;
    LPWSTR *const szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);
    if ((!szArglist) || (nArgs < 1))
    {
        OutputDebugStringA("[tee-win32] System error: Failed to initialize command-line arguments!\n");
        if (szArglist)
        {
            LocalFree(szArglist);
        }
        ExitProcess((UINT)-1);
    }

    /* Convert all argv from UTF-16 to UTF-8 */
    char **argv_utf8 = (char**)LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, (SIZE_T)nArgs * sizeof(char*));
    if (!argv_utf8)
    {
        LocalFree(szArglist);
        ExitProcess((UINT)-1);
    }

    for (i = 0; i < nArgs; i++)
    {
        const int len = WideCharToMultiByte(CP_UTF8, 0, szArglist[i], -1, NULL, 0, NULL, NULL);
        if (len > 0)
        {
            argv_utf8[i] = (char*)LocalAlloc(LMEM_FIXED, (SIZE_T)len);
            if (argv_utf8[i])
            {
                WideCharToMultiByte(CP_UTF8, 0, szArglist[i], -1, argv_utf8[i], len, NULL, NULL);
            }
        }
    }
    LocalFree(szArglist);

    const int retval = tee_main(nArgs, argv_utf8);

    for (i = 0; i < nArgs; i++)
    {
        if (argv_utf8[i]) LocalFree(argv_utf8[i]);
    }
    LocalFree(argv_utf8);

    ExitProcess((UINT)retval);
    return 0;
}

#endif
