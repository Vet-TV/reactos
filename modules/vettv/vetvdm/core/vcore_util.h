/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/core/vcore_util.h
 * PURPOSE:  Small string helpers shared by vetvdm core code.
 *
 * Copyright (C) 2026 VetTV
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#ifndef VCORE_UTIL_H
#define VCORE_UTIL_H

#include "vcore_types.h"

#if defined(__GNUC__)
#define VCORE_UNUSED __attribute__((unused))
#else
#define VCORE_UNUSED
#endif

static int VCORE_UNUSED
vcore_ascii_lower(int ch)
{
    if (ch >= 'A' && ch <= 'Z')
        return ch - 'A' + 'a';
    return ch;
}

static int VCORE_UNUSED
vcore_cstr_icmp(const char *left, const char *right)
{
    if (!left || !right)
        return (left == right) ? 0 : (left ? 1 : -1);
    while (*left && *right)
    {
        int a = vcore_ascii_lower((unsigned char)*left);
        int b = vcore_ascii_lower((unsigned char)*right);
        if (a != b)
            return (a < b) ? -1 : 1;
        left++;
        right++;
    }
    if (*left)
        return 1;
    if (*right)
        return -1;
    return 0;
}

static int VCORE_UNUSED
vcore_cstr_ieq(const char *left, const char *right)
{
    return vcore_cstr_icmp(left, right) == 0;
}

static void VCORE_UNUSED
vcore_cstr_copy(char *dst, unsigned cap, const char *src)
{
    unsigned i = 0;

    if (!dst || cap == 0)
        return;
    if (!src)
    {
        dst[0] = 0;
        return;
    }
    while (src[i] && i + 1 < cap)
    {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static unsigned VCORE_UNUSED
vcore_cstr_len(const char *text)
{
    unsigned n = 0;
    if (!text)
        return 0;
    while (text[n])
        n++;
    return n;
}

static WCHAR VCORE_UNUSED
vcore_w_lower(WCHAR ch)
{
    if (ch >= L'A' && ch <= L'Z')
        return (WCHAR)(ch - L'A' + L'a');
    return ch;
}

static int VCORE_UNUSED
vcore_w_icmp(const WCHAR *left, const WCHAR *right)
{
    if (!left || !right)
        return (left == right) ? 0 : (left ? 1 : -1);
    while (*left && *right)
    {
        WCHAR a = vcore_w_lower(*left);
        WCHAR b = vcore_w_lower(*right);
        if (a != b)
            return (a < b) ? -1 : 1;
        left++;
        right++;
    }
    if (*left)
        return 1;
    if (*right)
        return -1;
    return 0;
}

static int VCORE_UNUSED
vcore_w_ieq(const WCHAR *left, const WCHAR *right)
{
    return vcore_w_icmp(left, right) == 0;
}

static unsigned VCORE_UNUSED
vcore_w_len(const WCHAR *text)
{
    unsigned n = 0;
    if (!text)
        return 0;
    while (text[n])
        n++;
    return n;
}

static void VCORE_UNUSED
vcore_w_copy(WCHAR *dst, unsigned cap, const WCHAR *src)
{
    unsigned i = 0;

    if (!dst || cap == 0)
        return;
    if (!src)
    {
        dst[0] = 0;
        return;
    }
    while (src[i] && i + 1 < cap)
    {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void VCORE_UNUSED
vcore_w_upper_ascii(WCHAR *text)
{
    if (!text)
        return;
    while (*text)
    {
        if (*text >= L'a' && *text <= L'z')
            *text = (WCHAR)(*text - L'a' + L'A');
        text++;
    }
}

static int VCORE_UNUSED
vcore_w_has_char(const WCHAR *text, WCHAR ch)
{
    if (!text)
        return 0;
    while (*text)
    {
        if (*text == ch)
            return 1;
        text++;
    }
    return 0;
}

#endif /* VCORE_UTIL_H */
