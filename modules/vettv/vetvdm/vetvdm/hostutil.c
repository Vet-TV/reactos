/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/vetvdm/hostutil.c
 * PURPOSE:  Path, console, and error helpers for vetvdm.exe.
 *
 * Copyright (C) 2026 VetTV
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "host.h"

#include "win32.h"
#include "vcore_util.h"

#include <stdio.h>

static int g_console;

void
warn_add(char *buf, unsigned cap, const char *text)
{
    unsigned n = 0;
    if (!buf || cap == 0 || !text || !text[0])
        return;
    while (buf[n])
        n++;
    if (n && n + 1 < cap)
        buf[n++] = ' ';
    while (*text && n + 1 < cap)
        buf[n++] = *text++;
    buf[n] = 0;
}

int
file_exists(const WCHAR *path)
{
    DWORD attr;
    if (!path || !path[0])
        return 0;
    attr = GetFileAttributesW(path);
    if (attr == INVALID_FILE_ATTRIBUTES)
        return 0;
    if (attr & FILE_ATTRIBUTE_DIRECTORY)
        return 0;
    return 1;
}

int
dir_exists(const WCHAR *path)
{
    DWORD attr;
    if (!path || !path[0])
        return 0;
    attr = GetFileAttributesW(path);
    if (attr == INVALID_FILE_ATTRIBUTES)
        return 0;
    return (attr & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
}

void
file_part(const WCHAR *path, WCHAR *out, DWORD cch)
{
    const WCHAR *last = path ? path : L"";
    const WCHAR *cursor;
    for (cursor = last; *cursor; cursor++)
    {
        if (*cursor == L'\\' || *cursor == L'/')
            last = cursor + 1;
    }
    vcore_w_copy(out, cch, last);
}

void
parent_dir(const WCHAR *path, WCHAR *out, DWORD cch)
{
    unsigned n;
    unsigned i;
    unsigned cut = 0;
    int seen = 0;

    if (!out || cch == 0)
        return;
    out[0] = 0;
    if (!path)
        return;
    n = vcore_w_len(path);
    for (i = 0; i < n; i++)
    {
        if (path[i] == L'\\' || path[i] == L'/')
        {
            cut = i;
            seen = 1;
        }
    }
    if (!seen)
        return;
    if (cut == 2 && path[1] == L':')
        cut = 3;
    if (cut >= cch)
        return;
    for (i = 0; i < cut; i++)
        out[i] = path[i];
    out[cut] = 0;
}

int
join_path(WCHAR *out, DWORD cch, const WCHAR *dir, const WCHAR *tail)
{
    unsigned n;
    unsigned i;
    unsigned t;
    int slash;

    if (!out || cch < 2 || !dir || !tail)
        return 0;
    n = vcore_w_len(dir);
    t = vcore_w_len(tail);
    slash = (n > 0 && dir[n - 1] != L'\\' && dir[n - 1] != L'/');
    if (n + (slash ? 1u : 0u) + t + 1u > cch)
        return 0;
    for (i = 0; i < n; i++)
        out[i] = dir[i];
    if (slash)
        out[n++] = L'\\';
    for (i = 0; i < t; i++)
        out[n++] = tail[i];
    out[n] = 0;
    return 1;
}

int
replace_file(const WCHAR *full, const WCHAR *name, WCHAR *out, DWORD cch)
{
    WCHAR dir[MAX_PATH];
    parent_dir(full, dir, MAX_PATH);
    if (!dir[0])
        return 0;
    return join_path(out, cch, dir, name);
}

DWORD
full_path(const WCHAR *in, WCHAR *out, DWORD cch)
{
    DWORD n;
    if (!out || cch == 0)
        return VETVDM_E_INTERNAL;
    out[0] = 0;
    if (!in || !in[0])
        return VETVDM_OK;
    n = GetFullPathNameW(in, cch, out, NULL);
    if (n == 0 || n >= cch)
        return VETVDM_E_CMDLINE;
    return VETVDM_OK;
}

int
console_attach(int alloc_ok)
{
    HANDLE out;
    if (g_console)
        return 1;
    if (!AttachConsole(ATTACH_PARENT_PROCESS))
    {
        out = GetStdHandle(STD_OUTPUT_HANDLE);
        if (out && out != INVALID_HANDLE_VALUE && GetFileType(out) == FILE_TYPE_CHAR)
        {
            g_console = 1;
            return 1;
        }
        if (!alloc_ok || !AllocConsole())
            return 0;
    }
    out = CreateFileW(L"CONOUT$", GENERIC_WRITE,
                     FILE_SHARE_READ | FILE_SHARE_WRITE,
                     NULL, OPEN_EXISTING, 0, NULL);
    if (out == INVALID_HANDLE_VALUE)
        return 0;
    SetStdHandle(STD_OUTPUT_HANDLE, out);
    SetStdHandle(STD_ERROR_HANDLE, out);
    g_console = 1;
    return 1;
}

void
console_write(const char *text)
{
    DWORD bytes;
    DWORD wrote = 0;
    WCHAR *wide;
    HANDLE out;
    DWORD chars;

    if (!g_console || !text || !text[0])
        return;
    bytes = 0;
    while (text[bytes])
        bytes++;
    wide = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, ((size_t)bytes + 1u) * sizeof(WCHAR));
    if (!wide)
        return;
    if (vcore_utf8_to_wide(text, wide, bytes + 1u) != VETVDM_OK)
    {
        HeapFree(GetProcessHeap(), 0, wide);
        return;
    }
    out = GetStdHandle(STD_OUTPUT_HANDLE);
    chars = vcore_w_len(wide);
    if (!out || out == INVALID_HANDLE_VALUE ||
        !WriteConsoleW(out, wide, chars, &wrote, NULL))
    {
        if (out && out != INVALID_HANDLE_VALUE)
            WriteFile(out, text, bytes, &wrote, NULL);
    }
    HeapFree(GetProcessHeap(), 0, wide);
}

void
show_error(int from_hook, DWORD code, const char *why)
{
    char line[320];

    if (code < 0xE0000000u)
        return;
    _snprintf(line, sizeof(line), "vetvdm: %s (0x%08lX)\n",
             why ? why : "error", (unsigned long)code);
    line[sizeof(line) - 1] = 0;
    /* user32 stays in ask.c. A launcher with no console gets one here. */
    if (console_attach(from_hook ? 0 : 1))
        console_write(line);
}

const char *
why_for(DWORD code, const char *why)
{
    if (why && why[0])
        return why;
    switch (code)
    {
    case VETVDM_E_CMDLINE:
        return "bad command line";
    case VETVDM_E_NOPROG:
        return "program not found";
    case VETVDM_E_NOTDOS:
        return "not a DOS or Win16 image";
    case VETVDM_E_NOLAYER:
        return "layer executable missing";
    case VETVDM_E_START:
        return "could not start the layer";
    case VETVDM_E_WIN31:
        return "Windows 3.1 path missing";
    case VETVDM_E_PROFILE:
        return "profile error";
    case VETVDM_E_CANCEL:
        return "cancelled";
    case VETVDM_E_NORESULT:
        return "layer exited without a result";
    case VETVDM_E_INTERNAL:
        return "internal error";
    default:
        return "error";
    }
}

int
oem_ok(const WCHAR *text)
{
    BOOL used = FALSE;
    char scratch[4096];
    int wrote;
    if (!text || !text[0])
        return 1;
    wrote = WideCharToMultiByte(CP_OEMCP, WC_NO_BEST_FIT_CHARS,
                               text, -1, scratch, (int)sizeof(scratch),
                               NULL, &used);
    if (wrote <= 0 || used)
        return 0;
    return 1;
}

int
wappend(WCHAR *dst, DWORD cap, DWORD *used, const WCHAR *src)
{
    DWORD n = 0;
    DWORD i;
    if (!src)
        return 1;
    while (src[n])
        n++;
    if (*used + n + 1 > cap)
        return 0;
    for (i = 0; i < n; i++)
        dst[*used + i] = src[i];
    *used += n;
    dst[*used] = 0;
    return 1;
}
