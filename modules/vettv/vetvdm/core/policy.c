/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/core/policy.c
 * PURPOSE:  VDM policy schema, HKCU-over-HKLM merge, and safe expansion.
 *
 * Copyright (C) 2026 VetTV
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * Only %SystemDrive%, %SystemRoot%, and %ProgramFiles% expand. Profile
 * variables are never read and never expanded. The registry shim that
 * feeds vcore_policy_source lives in policy_reg.c.
 */

#include "vetvdm_core.h"

#include "vcore_util.h"

static int
name_eq(const WCHAR *text, const WCHAR *lit, unsigned lit_len)
{
    unsigned i;
    for (i = 0; i < lit_len; i++)
    {
        if (vcore_w_lower(text[i]) != vcore_w_lower(lit[i]))
            return 0;
    }
    return 1;
}

DWORD
vcore_expand_allowed(const vcore_expand_ctx *ctx,
                    const WCHAR *text, WCHAR *out, DWORD cch)
{
    DWORD used = 0;
    const WCHAR *cursor;

    if (!out || cch == 0)
        return VETVDM_E_INTERNAL;
    out[0] = 0;
    if (!text)
        return VETVDM_OK;

    cursor = text;
    while (*cursor)
    {
        const WCHAR *repl = NULL;
        unsigned skip = 0;

        if (*cursor == L'%')
        {
            if (name_eq(cursor, L"%SystemDrive%", 13))
            {
                repl = ctx ? ctx->system_drive : NULL;
                skip = 13;
            }
            else if (name_eq(cursor, L"%SystemRoot%", 12))
            {
                repl = ctx ? ctx->system_root : NULL;
                skip = 12;
            }
            else if (name_eq(cursor, L"%ProgramFiles%", 14))
            {
                repl = ctx ? ctx->program_files : NULL;
                skip = 14;
            }
            else
                return VETVDM_E_CMDLINE;

            if (!repl)
                return VETVDM_E_CMDLINE;
            while (*repl)
            {
                if (used + 2 > cch)
                    return VETVDM_E_CMDLINE;
                out[used++] = *repl++;
                out[used] = 0;
            }
            cursor += skip;
            continue;
        }

        if (used + 2 > cch)
            return VETVDM_E_CMDLINE;
        out[used++] = *cursor++;
        out[used] = 0;
    }
    return VETVDM_OK;
}

static void
copy_policy(vcore_policy *dst, const vcore_policy *src)
{
    int i;
    *dst = *src;
    for (i = dst->n_profile_dirs; i < VCORE_PROFILE_DIRS_MAX; i++)
        dst->profile_dirs[i][0] = 0;
    for (i = dst->n_pass; i < VCORE_PASSTHROUGH_MAX; i++)
        dst->pass_through[i][0] = 0;
}

static int
apply_multi(WCHAR dest[][MAX_PATH], int max_items, int *count,
           const WCHAR *multi, const vcore_expand_ctx *expand)
{
    int n = 0;
    const WCHAR *cursor = multi;

    if (!multi)
        return 1;
    while (*cursor && n < max_items)
    {
        WCHAR expanded[MAX_PATH];
        const WCHAR *item = cursor;
        unsigned len = vcore_w_len(cursor);
        if (len == 0)
            break;
        if (vcore_w_has_char(cursor, L'%'))
        {
            if (vcore_expand_allowed(expand, cursor, expanded, MAX_PATH) != VETVDM_OK)
                return 0;
            item = expanded;
            len = vcore_w_len(item);
        }
        if (len == 0 || len >= MAX_PATH || vcore_w_has_char(item, L'%'))
            return 0;
        vcore_w_copy(dest[n], MAX_PATH, item);
        n++;
        cursor += vcore_w_len(cursor) + 1;
    }
    if (*cursor)
        return 0;
    *count = n;
    return 1;
}

static int
string_ok(const WCHAR *text)
{
    if (!text)
        return 0;
    if (vcore_w_len(text) >= MAX_PATH)
        return 0;
    if (vcore_w_has_char(text, L'%'))
        return 0;
    return 1;
}

static void
apply_source(vcore_policy *pol, const vcore_policy_source *src,
            const vcore_expand_ctx *expand)
{
    DWORD dword;
    WCHAR text[MAX_PATH];
    WCHAR multi[4096];

    if (!src)
        return;

    if (src->get_dword && src->get_dword(src->ctx, L"Enabled", &dword))
        pol->enabled = dword;
    if (src->get_dword && src->get_dword(src->ctx, L"LogLevel", &dword))
        pol->log_level = dword;

    if (src->get_string && src->get_string(src->ctx, L"DosMode", text, 16))
    {
        if (text[0] && vcore_w_len(text) < 16 && !vcore_w_has_char(text, L'%'))
            vcore_w_copy(pol->dos_mode, 16, text);
    }
    if (src->get_string && src->get_string(src->ctx, L"Win16Mode", text, 16))
    {
        if (text[0] && vcore_w_len(text) < 16 && !vcore_w_has_char(text, L'%'))
            vcore_w_copy(pol->win16_mode, 16, text);
    }
    if (src->get_string &&
        src->get_string(src->ctx, L"DosBoxXPath", text, MAX_PATH) &&
        string_ok(text) && text[0])
        vcore_w_copy(pol->dosboxx_path, MAX_PATH, text);
    if (src->get_string &&
        src->get_string(src->ctx, L"WineVdmPath", text, MAX_PATH) &&
        string_ok(text) && text[0])
        vcore_w_copy(pol->winevdm_path, MAX_PATH, text);
    if (src->get_string &&
        src->get_string(src->ctx, L"Win31Path", text, MAX_PATH) &&
        string_ok(text))
        vcore_w_copy(pol->win31_path, MAX_PATH, text);
    if (src->get_string &&
        src->get_string(src->ctx, L"ConfTemplate", text, MAX_PATH) &&
        string_ok(text) && text[0])
        vcore_w_copy(pol->conf_template, MAX_PATH, text);

    if (src->get_multi &&
        src->get_multi(src->ctx, L"ProfileDirs", multi, 4096))
    {
        int count = pol->n_profile_dirs;
        if (apply_multi(pol->profile_dirs, VCORE_PROFILE_DIRS_MAX, &count, multi, expand))
            pol->n_profile_dirs = count;
    }
    if (src->get_multi &&
        src->get_multi(src->ctx, L"PassThrough", multi, 4096))
    {
        int count = pol->n_pass;
        if (apply_multi(pol->pass_through, VCORE_PASSTHROUGH_MAX, &count, multi, expand))
            pol->n_pass = count;
    }
}

DWORD
vcore_policy_load(const vcore_policy_source *machine,
                 const vcore_policy_source *user,
                 const vcore_policy *defaults,
                 const vcore_expand_ctx *expand,
                 vcore_policy *out)
{
    if (!out || !defaults)
        return VETVDM_E_INTERNAL;
    copy_policy(out, defaults);
    apply_source(out, machine, expand);
    apply_source(out, user, expand);
    return VETVDM_OK;
}
