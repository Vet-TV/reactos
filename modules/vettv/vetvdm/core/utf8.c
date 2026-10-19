/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/core/utf8.c
 * PURPOSE:  UTF-8 / UTF-16 conversions for conf text and profile strings.
 *
 * Copyright (C) 2026 VetTV
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "vetvdm_core.h"

DWORD
vcore_wide_to_utf8(const WCHAR *text, char *out, DWORD cap)
{
    DWORD used = 0;
    const WCHAR *cursor;

    if (!out || cap == 0)
        return VETVDM_E_INTERNAL;
    out[0] = 0;
    if (!text)
        return VETVDM_OK;

    for (cursor = text; *cursor; cursor++)
    {
        unsigned int cp = *cursor;
        unsigned int next;
        char enc[4];
        int nenc = 0;
        int i;

        if (cp >= 0xD800 && cp <= 0xDBFF && cursor[1])
        {
            next = cursor[1];
            if (next >= 0xDC00 && next <= 0xDFFF)
            {
                cp = 0x10000 + (((cp - 0xD800) << 10) | (next - 0xDC00));
                cursor++;
            }
        }

        if (cp < 0x80)
        {
            enc[0] = (char)cp;
            nenc = 1;
        }
        else if (cp < 0x800)
        {
            enc[0] = (char)(0xC0 | (cp >> 6));
            enc[1] = (char)(0x80 | (cp & 0x3F));
            nenc = 2;
        }
        else if (cp < 0x10000)
        {
            enc[0] = (char)(0xE0 | (cp >> 12));
            enc[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
            enc[2] = (char)(0x80 | (cp & 0x3F));
            nenc = 3;
        }
        else
        {
            enc[0] = (char)(0xF0 | (cp >> 18));
            enc[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
            enc[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
            enc[3] = (char)(0x80 | (cp & 0x3F));
            nenc = 4;
        }

        if (used + (DWORD)nenc + 1 > cap)
            return VETVDM_E_CMDLINE;
        for (i = 0; i < nenc; i++)
            out[used++] = enc[i];
        out[used] = 0;
    }
    return VETVDM_OK;
}

static int
utf8_decode(const char *text, unsigned int *cp, int *eaten)
{
    const unsigned char *s = (const unsigned char *)text;
    unsigned int c;

    if (!s || !s[0])
        return 0;
    c = s[0];
    if (c < 0x80)
    {
        *cp = c;
        *eaten = 1;
        return 1;
    }
    if ((c & 0xE0) == 0xC0 && s[1])
    {
        if ((s[1] & 0xC0) != 0x80)
            return 0;
        *cp = ((c & 0x1F) << 6) | (s[1] & 0x3F);
        *eaten = 2;
        if (*cp < 0x80)
            return 0;
        return 1;
    }
    if ((c & 0xF0) == 0xE0 && s[1] && s[2])
    {
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80)
            return 0;
        *cp = ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        *eaten = 3;
        if (*cp < 0x800)
            return 0;
        return 1;
    }
    if ((c & 0xF8) == 0xF0 && s[1] && s[2] && s[3])
    {
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 || (s[3] & 0xC0) != 0x80)
            return 0;
        *cp = ((c & 0x07) << 18) | ((s[1] & 0x3F) << 12) |
              ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        *eaten = 4;
        if (*cp < 0x10000 || *cp > 0x10FFFF)
            return 0;
        return 1;
    }
    return 0;
}

DWORD
vcore_utf8_to_wide(const char *text, WCHAR *out, DWORD cch)
{
    DWORD used = 0;
    const char *cursor;

    if (!out || cch == 0)
        return VETVDM_E_INTERNAL;
    out[0] = 0;
    if (!text)
        return VETVDM_OK;

    cursor = text;
    while (*cursor)
    {
        unsigned int cp = 0;
        int eaten = 0;

        if (!utf8_decode(cursor, &cp, &eaten))
            return VETVDM_E_PROFILE;
        cursor += eaten;
        if (cp > 0xFFFF)
        {
            unsigned int adj;
            if (used + 3 > cch)
                return VETVDM_E_CMDLINE;
            adj = cp - 0x10000;
            out[used++] = (WCHAR)(0xD800 + (adj >> 10));
            out[used++] = (WCHAR)(0xDC00 + (adj & 0x3FF));
        }
        else
        {
            if (used + 2 > cch)
                return VETVDM_E_CMDLINE;
            out[used++] = (WCHAR)cp;
        }
        out[used] = 0;
    }
    return VETVDM_OK;
}
