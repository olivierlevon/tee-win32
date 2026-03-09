/*
 * tee for Windows - ANSI escape code converter
 * Copyright (c) 2026 Olivier Levon
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
#ifndef _INC_TEEW32_ANSICONV_H
#define _INC_TEEW32_ANSICONV_H

#define WIN32_LEAN_AND_MEAN 1
#include <Windows.h>

/* Output format for ANSI conversion */
typedef enum {
    FMT_RAW = 0,   /* No conversion (passthrough) */
    FMT_HTML,       /* Convert ANSI escape codes to HTML */
    FMT_STRIP       /* Strip all ANSI escape codes (plain text) */
} output_format_t;

/* Opaque converter state */
typedef struct _ansi_conv ansi_conv_t;

/* Create a new converter for the given format and output file handle.
 * timestamp: if TRUE, prepend ISO 8601 UTC timestamp to each line
 * linenumber: if TRUE, prepend line number to each line (64-bit counter) */
ansi_conv_t *ansi_conv_create(output_format_t format, HANDLE hFile, BOOL timestamp, BOOL linenumber);

/* Destroy a converter and free its memory */
void ansi_conv_destroy(ansi_conv_t *conv);

/* Write format-specific header (e.g., HTML preamble). Call once before data. */
BOOL ansi_conv_write_header(ansi_conv_t *conv);

/* Process input data and write converted output to the file */
BOOL ansi_conv_write(ansi_conv_t *conv, const BYTE *data, DWORD size);

/* Write format-specific footer (e.g., HTML closing tags). Call once after all data. */
BOOL ansi_conv_write_footer(ansi_conv_t *conv);

/* Flush internal output buffer to the file */
BOOL ansi_conv_flush(ansi_conv_t *conv);

#endif /* _INC_TEEW32_ANSICONV_H */
