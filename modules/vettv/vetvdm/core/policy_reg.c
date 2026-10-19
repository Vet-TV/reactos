/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/core/policy_reg.c
 * PURPOSE:  Registry source for HKLM/HKCU Software\VetTV\RetroOS\VDM.
 *
 * Copyright (C) 2026 VetTV
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * EXPAND_SZ values expand only SystemDrive, SystemRoot, and ProgramFiles.
 * Profile environment variables are never read.
 */

#include "win32.h"

#include "vcore_util.h"

static const WCHAR g_subkey[] = L"Software\\VetTV\\RetroOS\\VDM";

DWORD
vetvdm_reg_open(HKEY root, HKEY *out)
{
    LONG rc;
    if (!out)
        return VETVDM_E_INTERNAL;
    *out = NULL;
    rc = RegOpenKeyExW(root, g_subkey, 0, KEY_QUERY_VALUE, out);
    if (rc == ERROR_FILE_NOT_FOUND || rc == ERROR_PATH_NOT_FOUND)
    {
        *out = NULL;
        return VETVDM_OK;
    }
    if (rc != ERROR_SUCCESS)
    {
        *out = NULL;
        return VETVDM_OK;
    }
    return VETVDM_OK;
}

void
vetvdm_reg_close(HKEY key)
{
    if (key)
        RegCloseKey(key);
}

static BOOL
get_dword(void *ctx, const WCHAR *name, DWORD *out)
{
    vetvdm_reg_ctx *reg = (vetvdm_reg_ctx *)ctx;
    DWORD type = 0;
    DWORD value = 0;
    DWORD cb = sizeof(value);
    LONG rc;

    if (!reg || !reg->key || !out)
        return FALSE;
    rc = RegQueryValueExW(reg->key, name, NULL, &type, (BYTE *)&value, &cb);
    if (rc != ERROR_SUCCESS || type != REG_DWORD || cb != sizeof(value))
        return FALSE;
    *out = value;
    return TRUE;
}

static BOOL
get_string(void *ctx, const WCHAR *name, WCHAR *out, DWORD cch)
{
    vetvdm_reg_ctx *reg = (vetvdm_reg_ctx *)ctx;
    DWORD type = 0;
    DWORD cb = 0;
    LONG rc;
    WCHAR *buf;
    DWORD nchars;
    DWORD status;

    if (!reg || !reg->key || !out || cch == 0)
        return FALSE;
    out[0] = 0;
    rc = RegQueryValueExW(reg->key, name, NULL, &type, NULL, &cb);
    if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ))
        return FALSE;
    if (cb > 32768)
        return FALSE;
    buf = (WCHAR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, cb + sizeof(WCHAR) * 2);
    if (!buf)
        return FALSE;
    rc = RegQueryValueExW(reg->key, name, NULL, &type, (BYTE *)buf, &cb);
    if (rc != ERROR_SUCCESS)
    {
        HeapFree(GetProcessHeap(), 0, buf);
        return FALSE;
    }
    nchars = cb / sizeof(WCHAR);
    if (nchars == 0)
        buf[0] = 0;
    else
        buf[nchars] = 0;
    if (type == REG_EXPAND_SZ)
    {
        WCHAR expanded[MAX_PATH];
        status = vcore_expand_allowed(reg->expand, buf, expanded, MAX_PATH);
        HeapFree(GetProcessHeap(), 0, buf);
        if (status != VETVDM_OK)
            return FALSE;
        if (vcore_w_len(expanded) + 1 > cch)
            return FALSE;
        vcore_w_copy(out, cch, expanded);
        return TRUE;
    }
    if (vcore_w_len(buf) + 1 > cch)
    {
        HeapFree(GetProcessHeap(), 0, buf);
        return FALSE;
    }
    vcore_w_copy(out, cch, buf);
    HeapFree(GetProcessHeap(), 0, buf);
    return TRUE;
}

static BOOL
get_multi(void *ctx, const WCHAR *name, WCHAR *out, DWORD cch)
{
    vetvdm_reg_ctx *reg = (vetvdm_reg_ctx *)ctx;
    DWORD type = 0;
    DWORD cb = 0;
    LONG rc;
    DWORD nchars;

    if (!reg || !reg->key || !out || cch < 2)
        return FALSE;
    out[0] = 0;
    out[1] = 0;
    rc = RegQueryValueExW(reg->key, name, NULL, &type, NULL, &cb);
    if (rc != ERROR_SUCCESS || (type != REG_MULTI_SZ && type != REG_SZ))
        return FALSE;
    if (cb / sizeof(WCHAR) + 2 > cch)
        return FALSE;
    rc = RegQueryValueExW(reg->key, name, NULL, &type, (BYTE *)out, &cb);
    if (rc != ERROR_SUCCESS)
        return FALSE;
    nchars = cb / sizeof(WCHAR);
    if (nchars < cch)
        out[nchars] = 0;
    if (nchars + 1 < cch)
        out[nchars + 1] = 0;
    if (type == REG_SZ)
    {
        /* A single string is one MULTI_SZ entry. */
        unsigned n = 0;
        while (n < cch && out[n])
            n++;
        if (n + 1 < cch)
            out[n + 1] = 0;
    }
    return TRUE;
}

void
vetvdm_reg_source(vcore_policy_source *src, vetvdm_reg_ctx *ctx)
{
    src->get_dword = get_dword;
    src->get_string = get_string;
    src->get_multi = get_multi;
    src->ctx = ctx;
}
