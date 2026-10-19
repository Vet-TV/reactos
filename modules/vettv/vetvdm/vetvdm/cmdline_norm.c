/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/vetvdm/cmdline_norm.c
 * PURPOSE:  Kind and layer names, and NT image-path normalisation.
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

const char *
vetvdm_kind_name(int kind)
{
    if (kind == VETVDM_KIND_DOS)
        return "dos";
    if (kind == VETVDM_KIND_WIN16)
        return "win16";
    return "auto";
}

const char *
vetvdm_layer_name(int layer)
{
    if (layer == VETVDM_LAYER_DOSBOXX)
        return "dosboxx";
    if (layer == VETVDM_LAYER_WINEVDM)
        return "winevdm";
    if (layer == VETVDM_LAYER_NTVDM)
        return "ntvdm";
    if (layer == VETVDM_LAYER_WIN31)
        return "win31";
    return "none";
}

int
vetvdm_layer_from_name(const WCHAR *text)
{
    if (!text || !text[0])
        return VETVDM_LAYER_NONE;
    if (vcore_w_ieq(text, L"dosboxx"))
        return VETVDM_LAYER_DOSBOXX;
    if (vcore_w_ieq(text, L"winevdm"))
        return VETVDM_LAYER_WINEVDM;
    if (vcore_w_ieq(text, L"ntvdm"))
        return VETVDM_LAYER_NTVDM;
    if (vcore_w_ieq(text, L"win31"))
        return VETVDM_LAYER_WIN31;
    return -1;
}

static int
starts_ci(const WCHAR *text, const WCHAR *prefix)
{
    while (*prefix)
    {
        if (vcore_w_lower(*text) != vcore_w_lower(*prefix))
            return 0;
        text++;
        prefix++;
    }
    return 1;
}

static int
is_slash(WCHAR ch)
{
    return ch == L'\\' || ch == L'/';
}

static void
slashes_to_back(WCHAR *text)
{
    while (text && *text)
    {
        if (*text == L'/')
            *text = L'\\';
        text++;
    }
}

DWORD
vetvdm_normalize_image_path(const WCHAR *in, WCHAR *out, DWORD cch)
{
    const WCHAR *body;
    unsigned len;

    if (!out || cch == 0)
        return VETVDM_E_INTERNAL;
    out[0] = 0;
    if (!in || !in[0])
        return VETVDM_E_CMDLINE;

    if (starts_ci(in, L"\\Device\\") || starts_ci(in, L"\\\\.\\"))
        return VETVDM_E_CMDLINE;

    body = NULL;
    if (starts_ci(in, L"\\??\\") || starts_ci(in, L"\\\\?\\"))
    {
        const WCHAR *rest = in + 4;
        if (starts_ci(rest, L"UNC\\") || starts_ci(rest, L"UNC/"))
        {
            WCHAR tmp[MAX_PATH];
            unsigned i;
            if (!rest[4])
                return VETVDM_E_CMDLINE;
            tmp[0] = L'\\';
            tmp[1] = L'\\';
            for (i = 0; rest[4 + i] && i + 3 < MAX_PATH; i++)
                tmp[2 + i] = rest[4 + i];
            if (rest[4 + i])
                return VETVDM_E_CMDLINE;
            tmp[2 + i] = 0;
            slashes_to_back(tmp);
            vcore_w_copy(out, cch, tmp);
            if (vcore_w_len(tmp) + 1 > cch)
                return VETVDM_E_CMDLINE;
            return VETVDM_OK;
        }
        if (((rest[0] >= L'A' && rest[0] <= L'Z') ||
             (rest[0] >= L'a' && rest[0] <= L'z')) &&
            rest[1] == L':' && is_slash(rest[2]))
            body = rest;
        else
            return VETVDM_E_CMDLINE;
    }
    else
        body = in;

    len = vcore_w_len(body);
    if (len + 1 > cch || len >= MAX_PATH)
        return VETVDM_E_CMDLINE;
    vcore_w_copy(out, cch, body);
    slashes_to_back(out);
    return VETVDM_OK;
}
