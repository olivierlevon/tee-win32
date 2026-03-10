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
#define WIN32_LEAN_AND_MEAN 1
#include <Windows.h>
#include "include/ansiconv.h"

/* ========================================================================== */
/* Constants                                                                  */
/* ========================================================================== */

#define CONV_OUTBUF_SIZE  32768U
#define CONV_FLUSH_MARGIN 512U
#define CSI_PARAMS_MAX    128

/* Parser states */
#define STATE_NORMAL    0
#define STATE_ESC       1
#define STATE_CSI       2
#define STATE_OSC       3
#define STATE_OSC_ESC   4

/* Color type: how fg_color/bg_color is interpreted */
#define COLOR_DEFAULT   0   /* Terminal default */
#define COLOR_STANDARD  1   /* Standard 0-7 */
#define COLOR_BRIGHT    2   /* Bright 8-15 */
#define COLOR_XTERM256  3   /* Xterm 256-color index */
#define COLOR_TRUECOLOR 4   /* 24-bit RGB */

/* ========================================================================== */
/* Standard ANSI 16-color palette (CGA-style RGB values)                      */
/* ========================================================================== */

static const DWORD g_ansi16[16] = {
    0x000000U, /* 0  black          */
    0xAA0000U, /* 1  red            */
    0x00AA00U, /* 2  green          */
    0xAA5500U, /* 3  yellow/brown   */
    0x0000AAU, /* 4  blue           */
    0xAA00AAU, /* 5  magenta        */
    0x00AAAAU, /* 6  cyan           */
    0xAAAAAAU, /* 7  white          */
    0x555555U, /* 8  bright black   */
    0xFF5555U, /* 9  bright red     */
    0x55FF55U, /* 10 bright green   */
    0xFFFF55U, /* 11 bright yellow  */
    0x5555FFU, /* 12 bright blue    */
    0xFF55FFU, /* 13 bright magenta */
    0x55FFFFU, /* 14 bright cyan    */
    0xFFFFFFU  /* 15 bright white   */
};

/* 6x6x6 color cube component values (indices 16-231) */
static const BYTE g_cube6[6] = { 0x00, 0x5F, 0x87, 0xAF, 0xD7, 0xFF };

/* ========================================================================== */
/* Converter state structure                                                  */
/* ========================================================================== */

typedef struct {
    BOOL bold, faint, italic, underline, blink, reverse, concealed;
    int fg_type, bg_type;
    DWORD fg_color, bg_color;
} text_style_t;

struct _ansi_conv {
    output_format_t format;
    HANDLE hFile;
    int state;
    int param_len;
    BOOL span_open;
    BOOL has_error;
    BOOL timestamp;
    BOOL linenumber;
    BOOL has_prefix;
    BOOL at_line_start;
    ULONGLONG line_count;
    text_style_t style;
    DWORD out_pos;
    char params[CSI_PARAMS_MAX];
    BYTE out_buf[CONV_OUTBUF_SIZE];
};

/* ========================================================================== */
/* Output buffer helpers                                                      */
/* ========================================================================== */

BOOL ansi_conv_flush(ansi_conv_t *conv)
{
    DWORD written, offset;
    if (!conv || conv->has_error)
        return FALSE;
    for (offset = 0U; offset < conv->out_pos; offset += written)
    {
        if (!WriteFile(conv->hFile, conv->out_buf + offset, conv->out_pos - offset, &written, NULL) || !written)
        {
            conv->has_error = TRUE;
            return FALSE;
        }
    }
    conv->out_pos = 0U;
    return TRUE;
}

static BOOL ensure_space(ansi_conv_t *conv)
{
    if (conv->has_error)
        return FALSE;
    if (conv->out_pos + CONV_FLUSH_MARGIN > CONV_OUTBUF_SIZE)
        return ansi_conv_flush(conv);
    return TRUE;
}

static __forceinline void out_byte(ansi_conv_t *conv, BYTE b)
{
#ifndef NDEBUG
    if (conv->out_pos >= CONV_OUTBUF_SIZE)
        __debugbreak(); /* Output buffer overflow -- ensure_space margin exceeded */
#endif
    conv->out_buf[conv->out_pos++] = b;
}

static void out_str(ansi_conv_t *conv, const char *s)
{
    while (*s)
        out_byte(conv, (BYTE)*s++);
}

/* ========================================================================== */
/* Color conversion                                                           */
/* ========================================================================== */

static DWORD xterm256_to_rgb(DWORD index)
{
    DWORD r, g, b;
    if (index < 16U)
        return g_ansi16[index];
    if (index < 232U)
    {
        /* 6x6x6 color cube */
        index -= 16U;
        b = index % 6U;
        g = (index / 6U) % 6U;
        r = index / 36U;
        return ((DWORD)g_cube6[r] << 16) | ((DWORD)g_cube6[g] << 8) | (DWORD)g_cube6[b];
    }
    if (index < 256U)
    {
        /* Grayscale ramp: 232-255 -> 8, 18, 28, ..., 238 */
        DWORD v = 8U + (index - 232U) * 10U;
        return (v << 16) | (v << 8) | v;
    }
    return 0xAAAAAAU; /* fallback: default white */
}

static DWORD resolve_color(int type, DWORD color, BOOL bold_as_bright)
{
    switch (type)
    {
    case COLOR_STANDARD:
        if (bold_as_bright && (color < 8U))
            return g_ansi16[color + 8U];
        return g_ansi16[color & 7U];
    case COLOR_BRIGHT:
        return g_ansi16[(color & 7U) + 8U];
    case COLOR_XTERM256:
        return xterm256_to_rgb(color & 255U);
    case COLOR_TRUECOLOR:
        return color;
    default:
        return 0xFFFFFFFFU; /* sentinel: default color */
    }
}

/* ========================================================================== */
/* HTML output helpers                                                        */
/* ========================================================================== */

static const char g_hex[] = "0123456789abcdef";

static void out_hex_color(ansi_conv_t *conv, DWORD rgb)
{
    BYTE r = (BYTE)(rgb >> 16), g = (BYTE)(rgb >> 8), b = (BYTE)rgb;
    out_byte(conv, '#');
    out_byte(conv, g_hex[r >> 4]); out_byte(conv, g_hex[r & 0xF]);
    out_byte(conv, g_hex[g >> 4]); out_byte(conv, g_hex[g & 0xF]);
    out_byte(conv, g_hex[b >> 4]); out_byte(conv, g_hex[b & 0xF]);
}

static BOOL style_is_default(const text_style_t *s)
{
    return (!s->bold) && (!s->faint) && (!s->italic) && (!s->underline)
        && (!s->blink) && (!s->reverse) && (!s->concealed)
        && (s->fg_type == COLOR_DEFAULT) && (s->bg_type == COLOR_DEFAULT);
}

static void html_close_span(ansi_conv_t *conv)
{
    if (conv->span_open)
    {
        out_str(conv, "</span>");
        conv->span_open = FALSE;
    }
}

static void html_open_span(ansi_conv_t *conv)
{
    const text_style_t *s = &conv->style;
    DWORD fg_rgb, bg_rgb;
    BOOL has_attr = FALSE;

    if (style_is_default(s))
        return;

    /* Resolve colors, handling reverse video */
    fg_rgb = resolve_color(s->fg_type, s->fg_color, s->bold);
    bg_rgb = resolve_color(s->bg_type, s->bg_color, FALSE);

    if (s->reverse)
    {
        DWORD tmp = fg_rgb;
        fg_rgb = (bg_rgb != 0xFFFFFFFFU) ? bg_rgb : 0x000000U;
        bg_rgb = (tmp != 0xFFFFFFFFU) ? tmp : 0xAAAAAAU;
    }

    out_str(conv, "<span style=\"");

    /* Foreground color */
    if (fg_rgb != 0xFFFFFFFFU)
    {
        out_str(conv, "color:");
        out_hex_color(conv, fg_rgb);
        has_attr = TRUE;
    }

    /* Background color */
    if (bg_rgb != 0xFFFFFFFFU)
    {
        if (has_attr) out_byte(conv, ';');
        out_str(conv, "background-color:");
        out_hex_color(conv, bg_rgb);
        has_attr = TRUE;
    }

    /* Bold */
    if (s->bold)
    {
        if (has_attr) out_byte(conv, ';');
        out_str(conv, "font-weight:bold");
        has_attr = TRUE;
    }

    /* Faint / dim */
    if (s->faint)
    {
        if (has_attr) out_byte(conv, ';');
        out_str(conv, "opacity:0.5");
        has_attr = TRUE;
    }

    /* Italic */
    if (s->italic)
    {
        if (has_attr) out_byte(conv, ';');
        out_str(conv, "font-style:italic");
        has_attr = TRUE;
    }

    /* Underline */
    if (s->underline)
    {
        if (has_attr) out_byte(conv, ';');
        out_str(conv, "text-decoration:underline");
        has_attr = TRUE;
    }

    /* Concealed */
    if (s->concealed)
    {
        if (has_attr) out_byte(conv, ';');
        out_str(conv, "visibility:hidden");
        has_attr = TRUE;
    }

    out_str(conv, "\">");
    conv->span_open = TRUE;
}

static void html_update_style(ansi_conv_t *conv)
{
    html_close_span(conv);
    html_open_span(conv);
}

static void html_output_char(ansi_conv_t *conv, BYTE c)
{
    switch (c)
    {
    case '<':  out_str(conv, "&lt;");   break;
    case '>':  out_str(conv, "&gt;");   break;
    case '&':  out_str(conv, "&amp;");  break;
    case '"':  out_str(conv, "&quot;"); break;
    case '\r': break; /* skip CR, keep LF */
    default:   out_byte(conv, c);       break;
    }
}

/* ========================================================================== */
/* Line prefix helpers (timestamp + line number)                              */
/* ========================================================================== */

static void out_u64(ansi_conv_t *conv, ULONGLONG val)
{
    char tmp[21]; /* max 20 digits for ULONGLONG + sentinel */
    int len = 0;
    if (val == 0ULL)
    {
        out_byte(conv, '0');
        return;
    }
    while (val > 0ULL)
    {
        tmp[len++] = (char)('0' + (val % 10ULL));
        val /= 10ULL;
    }
    while (len > 0)
        out_byte(conv, (BYTE)tmp[--len]);
}

static void out_2digits(ansi_conv_t *conv, WORD val)
{
    out_byte(conv, (BYTE)('0' + (val / 10)));
    out_byte(conv, (BYTE)('0' + (val % 10)));
}

static void out_3digits(ansi_conv_t *conv, WORD val)
{
    out_byte(conv, (BYTE)('0' + (val / 100)));
    out_byte(conv, (BYTE)('0' + ((val / 10) % 10)));
    out_byte(conv, (BYTE)('0' + (val % 10)));
}

static void out_4digits(ansi_conv_t *conv, WORD val)
{
    out_byte(conv, (BYTE)('0' + (val / 1000)));
    out_byte(conv, (BYTE)('0' + ((val / 100) % 10)));
    out_byte(conv, (BYTE)('0' + ((val / 10) % 10)));
    out_byte(conv, (BYTE)('0' + (val % 10)));
}

/* Emit [YYYY-MM-DDThh:mm:ss.mmmZ] */
static void emit_timestamp(ansi_conv_t *conv)
{
    SYSTEMTIME st;
    GetSystemTime(&st);
    out_byte(conv, '[');
    out_4digits(conv, st.wYear);
    out_byte(conv, '-');
    out_2digits(conv, st.wMonth);
    out_byte(conv, '-');
    out_2digits(conv, st.wDay);
    out_byte(conv, 'T');
    out_2digits(conv, st.wHour);
    out_byte(conv, ':');
    out_2digits(conv, st.wMinute);
    out_byte(conv, ':');
    out_2digits(conv, st.wSecond);
    out_byte(conv, '.');
    out_3digits(conv, st.wMilliseconds);
    out_byte(conv, 'Z');
    out_byte(conv, ']');
    out_byte(conv, ' ');
}

static void emit_line_prefix(ansi_conv_t *conv)
{
    conv->at_line_start = FALSE;
    conv->line_count++;
    if (conv->timestamp)
        emit_timestamp(conv);
    if (conv->linenumber)
    {
        out_u64(conv, conv->line_count);
        out_byte(conv, ':');
        out_byte(conv, ' ');
    }
}

/* ========================================================================== */
/* SGR (Select Graphic Rendition) parser                                      */
/* ========================================================================== */

static void reset_style(text_style_t *s)
{
    s->bold = s->faint = s->italic = s->underline = FALSE;
    s->blink = s->reverse = s->concealed = FALSE;
    s->fg_type = COLOR_DEFAULT;
    s->bg_type = COLOR_DEFAULT;
    s->fg_color = 0U;
    s->bg_color = 0U;
}

static void process_sgr(ansi_conv_t *conv)
{
    int sgr[64];
    int count = 0, i;
    unsigned int val = 0U;
    BOOL has_val = FALSE;

    /* Parse semicolon/colon-separated parameter list */
    for (i = 0; i <= conv->param_len; ++i)
    {
        const char c = (i < conv->param_len) ? conv->params[i] : ';';
        if (c >= '0' && c <= '9')
        {
            val = val * 10U + (unsigned int)(c - '0');
            if (val > 100000U) val = 100000U; /* Clamp to prevent overflow on malformed input */
            has_val = TRUE;
        }
        else if (c == ';' || c == ':')
        {
            if (count < 64)
                sgr[count++] = has_val ? (int)val : 0;
            val = 0U;
            has_val = FALSE;
        }
    }

    /* Apply SGR codes to current style */
    for (i = 0; i < count; ++i)
    {
        switch (sgr[i])
        {
        case 0:
            reset_style(&conv->style);
            break;
        case 1:  conv->style.bold = TRUE; break;
        case 2:  conv->style.faint = TRUE; break;
        case 3:  conv->style.italic = TRUE; break;
        case 4:  conv->style.underline = TRUE; break;
        case 21: conv->style.underline = TRUE; break; /* double underline */
        case 5:  /* fallthrough */
        case 6:  conv->style.blink = TRUE; break;
        case 7:  conv->style.reverse = TRUE; break;
        case 8:  conv->style.concealed = TRUE; break;

        case 22: conv->style.bold = FALSE; conv->style.faint = FALSE; break;
        case 23: conv->style.italic = FALSE; break;
        case 24: conv->style.underline = FALSE; break;
        case 25: conv->style.blink = FALSE; break;
        case 27: conv->style.reverse = FALSE; break;
        case 28: conv->style.concealed = FALSE; break;

        /* Standard foreground colors 30-37 */
        case 30: case 31: case 32: case 33:
        case 34: case 35: case 36: case 37:
            conv->style.fg_type = COLOR_STANDARD;
            conv->style.fg_color = (DWORD)(sgr[i] - 30);
            break;

        /* Extended foreground: 38;5;N (256-color) or 38;2;R;G;B (truecolor) */
        case 38:
            if (i + 2 < count && sgr[i + 1] == 5)
            {
                conv->style.fg_type = COLOR_XTERM256;
                conv->style.fg_color = (DWORD)sgr[i + 2];
                i += 2;
            }
            else if (i + 4 < count && sgr[i + 1] == 2)
            {
                conv->style.fg_type = COLOR_TRUECOLOR;
                conv->style.fg_color = ((DWORD)(sgr[i + 2] & 0xFF) << 16)
                                     | ((DWORD)(sgr[i + 3] & 0xFF) << 8)
                                     |  (DWORD)(sgr[i + 4] & 0xFF);
                i += 4;
            }
            break;

        case 39: /* Default foreground */
            conv->style.fg_type = COLOR_DEFAULT;
            break;

        /* Standard background colors 40-47 */
        case 40: case 41: case 42: case 43:
        case 44: case 45: case 46: case 47:
            conv->style.bg_type = COLOR_STANDARD;
            conv->style.bg_color = (DWORD)(sgr[i] - 40);
            break;

        /* Extended background: 48;5;N (256-color) or 48;2;R;G;B (truecolor) */
        case 48:
            if (i + 2 < count && sgr[i + 1] == 5)
            {
                conv->style.bg_type = COLOR_XTERM256;
                conv->style.bg_color = (DWORD)sgr[i + 2];
                i += 2;
            }
            else if (i + 4 < count && sgr[i + 1] == 2)
            {
                conv->style.bg_type = COLOR_TRUECOLOR;
                conv->style.bg_color = ((DWORD)(sgr[i + 2] & 0xFF) << 16)
                                     | ((DWORD)(sgr[i + 3] & 0xFF) << 8)
                                     |  (DWORD)(sgr[i + 4] & 0xFF);
                i += 4;
            }
            break;

        case 49: /* Default background */
            conv->style.bg_type = COLOR_DEFAULT;
            break;

        /* Bright foreground colors 90-97 (aixterm) */
        case 90: case 91: case 92: case 93:
        case 94: case 95: case 96: case 97:
            conv->style.fg_type = COLOR_BRIGHT;
            conv->style.fg_color = (DWORD)(sgr[i] - 90);
            break;

        /* Bright background colors 100-107 (aixterm) */
        case 100: case 101: case 102: case 103:
        case 104: case 105: case 106: case 107:
            conv->style.bg_type = COLOR_BRIGHT;
            conv->style.bg_color = (DWORD)(sgr[i] - 100);
            break;
        }
    }

    /* Update HTML output to reflect style changes */
    if (conv->format == FMT_HTML)
        html_update_style(conv);
}

/* ========================================================================== */
/* Block-copy fast path for FMT_RAW                                           */
/* ========================================================================== */

/*
 * Copy a contiguous block of raw bytes into the output buffer, flushing as
 * needed.  No ANSI parsing is performed.  Returns FALSE on write error.
 */
static BOOL out_raw_block(ansi_conv_t *conv, const BYTE *src, DWORD len)
{
    while (len > 0U)
    {
        DWORD avail = CONV_OUTBUF_SIZE - conv->out_pos;
        DWORD chunk = (len < avail) ? len : avail;
        CopyMemory(conv->out_buf + conv->out_pos, src, chunk);
        conv->out_pos += chunk;
        src += chunk;
        len -= chunk;
        if (conv->out_pos == CONV_OUTBUF_SIZE)
        {
            if (!ansi_conv_flush(conv))
                return FALSE;
        }
    }
    return TRUE;
}

/*
 * Process a buffer of raw bytes with optional line-prefix injection.
 * Scans for newline boundaries and copies intervening blocks in bulk
 * via CopyMemory instead of the byte-by-byte process_byte path.
 * Returns FALSE on write error.
 */
static BOOL process_raw_block(ansi_conv_t *conv, const BYTE *data, DWORD size)
{
    DWORD pos = 0U;
    const BOOL has_prefix = conv->has_prefix;

    while (pos < size)
    {
        DWORD scan, block_len;

        /* Emit line prefix at the start of a new line (skip \r) */
        if (has_prefix && conv->at_line_start && data[pos] != '\r')
        {
            if (!ensure_space(conv))
                return FALSE;
            emit_line_prefix(conv);
        }

        /* Scan forward for the next newline */
        for (scan = pos; scan < size; ++scan)
        {
            if (data[scan] == '\n')
                break;
        }

        /* Copy the block before the newline (or end of data) */
        block_len = scan - pos;
        if (block_len > 0U)
        {
            if (!out_raw_block(conv, data + pos, block_len))
                return FALSE;
        }

        /* If we stopped on a newline, output it and mark next line start */
        if (scan < size)
        {
            if (conv->out_pos >= CONV_OUTBUF_SIZE)
            {
                if (!ansi_conv_flush(conv))
                    return FALSE;
            }
            conv->out_buf[conv->out_pos++] = '\n';
            if (has_prefix)
                conv->at_line_start = TRUE;
            pos = scan + 1U;
        }
        else
        {
            pos = scan;
        }
    }
    return TRUE;
}

/* ========================================================================== */
/* ANSI escape sequence state machine                                         */
/* ========================================================================== */

static void process_byte(ansi_conv_t *conv, BYTE b)
{
    const BOOL has_prefix = conv->has_prefix;

    switch (conv->state)
    {
    case STATE_NORMAL:
        if (b == 0x1B)
        {
            conv->state = STATE_ESC;
            return;
        }
        if (b == 0x9B)
        {
            /* Single-byte CSI (C1 control code) */
            conv->state = STATE_CSI;
            conv->param_len = 0;
            return;
        }
        /* Regular character: handle line prefix before output */
        if (b == '\n')
        {
            if (conv->format == FMT_HTML)
                html_output_char(conv, b);
            else
                out_byte(conv, b);
            if (has_prefix)
                conv->at_line_start = TRUE;
            return;
        }
        if (has_prefix && conv->at_line_start && b != '\r')
            emit_line_prefix(conv);
        if (conv->format == FMT_HTML)
            html_output_char(conv, b);
        else if (conv->format == FMT_STRIP)
            out_byte(conv, b);
        return;

    case STATE_ESC:
        if (b == '[')
        {
            /* CSI introducer */
            conv->state = STATE_CSI;
            conv->param_len = 0;
            return;
        }
        if (b == ']')
        {
            /* OSC introducer */
            conv->state = STATE_OSC;
            return;
        }
        /* Two-byte escape sequence (e.g., ESC M, ESC 7, ESC 8) -- consume and discard */
        conv->state = STATE_NORMAL;
        return;

    case STATE_CSI:
        /* Collect parameter bytes (0x30-0x3F: digits, semicolons, colons, etc.) */
        if (b >= 0x30 && b <= 0x3F)
        {
            if (conv->param_len < CSI_PARAMS_MAX)
                conv->params[conv->param_len++] = (char)b;
            return;
        }
        /* Intermediate bytes (0x20-0x2F) -- ignore */
        if (b >= 0x20 && b <= 0x2F)
            return;
        /* Final byte (0x40-0x7E) -- process and return to normal */
        if (b == 'm')
            process_sgr(conv);
        /* All other CSI sequences (cursor movement, etc.) are silently discarded */
        conv->state = STATE_NORMAL;
        return;

    case STATE_OSC:
        if (b == 0x07)
        {
            /* BEL terminates OSC */
            conv->state = STATE_NORMAL;
            return;
        }
        if (b == 0x1B)
        {
            /* Possible ST (ESC \) */
            conv->state = STATE_OSC_ESC;
            return;
        }
        /* Skip OSC content */
        return;

    case STATE_OSC_ESC:
        if (b == '\\')
        {
            /* ST terminates OSC */
            conv->state = STATE_NORMAL;
            return;
        }
        /* Not ST -- continue in OSC (the ESC was part of content) */
        conv->state = STATE_OSC;
        return;
    }
}

/* ========================================================================== */
/* Public API                                                                 */
/* ========================================================================== */

ansi_conv_t *ansi_conv_create(output_format_t format, HANDLE hFile, BOOL timestamp, BOOL linenumber)
{
    ansi_conv_t *conv = (ansi_conv_t *)LocalAlloc(LPTR, sizeof(ansi_conv_t));
    if (conv)
    {
        conv->format = format;
        conv->hFile = hFile;
        conv->state = STATE_NORMAL;
        conv->param_len = 0;
        conv->span_open = FALSE;
        conv->has_error = FALSE;
        conv->timestamp = timestamp;
        conv->linenumber = linenumber;
        conv->has_prefix = (timestamp || linenumber) ? TRUE : FALSE;
        conv->at_line_start = conv->has_prefix;
        conv->line_count = 0ULL;
        conv->out_pos = 0U;
        reset_style(&conv->style);
    }
    return conv;
}

void ansi_conv_destroy(ansi_conv_t *conv)
{
    if (conv)
        LocalFree(conv);
}

BOOL ansi_conv_write_header(ansi_conv_t *conv)
{
    if (!conv || conv->has_error)
        return FALSE;
    if (conv->format == FMT_HTML)
    {
        out_str(conv, "<!DOCTYPE html>\n<html>\n<head>\n"
            "<meta charset=\"utf-8\">\n"
            "<title>tee output</title>\n"
            "<style>\n"
            "body { margin:0; padding:1em; background:#000; color:#aaa; }\n"
            "pre  { font-family:Consolas,'Courier New',monospace; font-size:14px;\n"
            "       white-space:pre-wrap; word-wrap:break-word; }\n"
            "</style>\n"
            "</head>\n<body>\n<pre>\n");
        return ansi_conv_flush(conv);
    }
    return TRUE;
}

BOOL ansi_conv_write(ansi_conv_t *conv, const BYTE *data, DWORD size)
{
    DWORD i;
    if (!conv || conv->has_error)
        return FALSE;

    /* Fast path: FMT_RAW uses block-copy instead of byte-by-byte parsing */
    if (conv->format == FMT_RAW)
    {
        if (!process_raw_block(conv, data, size))
            return FALSE;
        return ansi_conv_flush(conv);
    }

    for (i = 0U; i < size; ++i)
    {
        if (!ensure_space(conv))
            return FALSE;
        process_byte(conv, data[i]);
    }
    return ansi_conv_flush(conv);
}

BOOL ansi_conv_write_footer(ansi_conv_t *conv)
{
    if (!conv || conv->has_error)
        return FALSE;
    if (conv->format == FMT_HTML)
    {
        html_close_span(conv);
        out_str(conv, "\n</pre>\n</body>\n</html>\n");
        return ansi_conv_flush(conv);
    }
    return TRUE;
}

void ansi_conv_reset_for_rotation(ansi_conv_t *conv, HANDLE hFile)
{
    if (!conv) return;
    ansi_conv_flush(conv);
    conv->hFile = hFile;
    conv->line_count = 0ULL;
    conv->at_line_start = conv->has_prefix;
    conv->state = STATE_NORMAL;
    conv->param_len = 0;
    conv->has_error = FALSE;
    conv->out_pos = 0U;
    if (conv->format == FMT_HTML)
    {
        conv->span_open = FALSE;
        reset_style(&conv->style);
    }
}
