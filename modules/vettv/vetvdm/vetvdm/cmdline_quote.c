/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/vetvdm/cmdline_quote.c
 * PURPOSE:  Win32 CreateProcess argument quoting.
 *
 * Copyright (C) 2026 VetTV
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "cmdline.h"

#include "vcore_util.h"

static int
is_blank(WCHAR ch)
{
    return ch == L' ' || ch == L'\t';
}

DWORD
vetvdm_quote_win32(const WCHAR *arg, WCHAR *out, DWORD cch)
{
    unsigned used = 0;
    const WCHAR *s;
    int needs = 0;

    if (!out || cch == 0)
        return VETVDM_E_INTERNAL;
    out[0] = 0;
    if (!arg)
        arg = L"";
    for (s = arg; *s; s++)
    {
        if (is_blank(*s) || *s == L'"')
            needs = 1;
    }
    if (!needs && arg[0])
    {
        if (vcore_w_len(arg) + 1 > cch)
            return VETVDM_E_CMDLINE;
        vcore_w_copy(out, cch, arg);
        return VETVDM_OK;
    }

    if (used + 1 >= cch)
        return VETVDM_E_CMDLINE;
    out[used++] = L'"';
    for (s = arg; *s; s++)
    {
        unsigned backs = 0;
        while (*s == L'\\')
        {
            backs++;
            s++;
        }
        if (!*s)
        {
            backs *= 2;
            while (backs--)
            {
                if (used + 1 >= cch)
                    return VETVDM_E_CMDLINE;
                out[used++] = L'\\';
            }
            break;
        }
        if (*s == L'"')
            backs = backs * 2 + 1;
        while (backs--)
        {
            if (used + 1 >= cch)
                return VETVDM_E_CMDLINE;
            out[used++] = L'\\';
        }
        if (used + 1 >= cch)
            return VETVDM_E_CMDLINE;
        out[used++] = *s;
    }
    if (used + 2 > cch)
        return VETVDM_E_CMDLINE;
    out[used++] = L'"';
    out[used] = 0;
    return VETVDM_OK;
}
