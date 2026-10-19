/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/core/profile.c
 * PURPOSE:  Profile lookup order from spec section 4.
 *
 * Copyright (C) 2026 VetTV
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "vetvdm_core.h"

#include "vcore_util.h"

#include <stdlib.h>
#include <string.h>

const char *
vcore_profile_src_name(int src)
{
    switch (src)
    {
    case VCORE_SRC_EXPLICIT:
        return "explicit";
    case VCORE_SRC_GAMEJSON:
        return "gamejson";
    case VCORE_SRC_BYHASH:
        return "by-hash";
    case VCORE_SRC_BYNAME:
        return "by-name";
    case VCORE_SRC_BUILTIN:
        return "builtin";
    default:
        return "none";
    }
}

static void
builtin_profile(vcore_profile *out)
{
    memset(out, 0, sizeof(*out));
    vcore_cstr_copy(out->id, sizeof(out->id), "builtin");
    out->src = VCORE_SRC_BUILTIN;
}

static int
join_path(WCHAR *out, unsigned cap, const WCHAR *dir, const WCHAR *tail)
{
    unsigned n;
    unsigned i;
    int need_sep;

    if (!dir || !tail || !out || cap < 2)
        return 0;
    n = vcore_w_len(dir);
    if (n + 1 >= cap)
        return 0;
    for (i = 0; i < n; i++)
        out[i] = dir[i];
    out[n] = 0;
    need_sep = (n > 0 && out[n - 1] != L'\\' && out[n - 1] != L'/');
    i = vcore_w_len(tail);
    if (n + (need_sep ? 1u : 0u) + i + 1u > cap)
        return 0;
    if (need_sep)
        out[n++] = L'\\';
    for (i = 0; tail[i]; i++)
        out[n++] = tail[i];
    out[n] = 0;
    return 1;
}

static DWORD
load_at(vcore_read_fn readfn, void *ctx, const WCHAR *path,
       vcore_profile *out, int src)
{
    char *buf;
    DWORD len = 0;
    int rc;
    DWORD status;
    const DWORD cap = 256u * 1024u;

    buf = (char *)malloc(cap);
    if (!buf)
        return VETVDM_E_INTERNAL;
    rc = readfn(ctx, path, buf, cap, &len);
    if (rc == 0)
    {
        free(buf);
        return VETVDM_E_NOPROG;
    }
    if (rc < 0)
    {
        free(buf);
        return VETVDM_E_PROFILE;
    }
    if (len >= cap)
        len = cap - 1;
    buf[len] = 0;
    status = vcore_profile_parse(buf, len, out);
    free(buf);
    if (status != VETVDM_OK)
        return status;
    out->src = src;
    vcore_w_copy(out->source_path, MAX_PATH, path);
    return VETVDM_OK;
}

static int
is_pathlike(const WCHAR *text)
{
    const WCHAR *cursor;
    unsigned n;
    if (!text || !text[0])
        return 0;
    for (cursor = text; *cursor; cursor++)
    {
        if (*cursor == L'\\' || *cursor == L'/' || *cursor == L':')
            return 1;
    }
    n = vcore_w_len(text);
    if (n >= 5)
    {
        const WCHAR *ext = text + n - 5;
        if (vcore_w_ieq(ext, L".json"))
            return 1;
    }
    return 0;
}

static void
file_name_of(const WCHAR *path, WCHAR *out, unsigned cap)
{
    const WCHAR *last = path;
    const WCHAR *cursor;
    if (!path)
    {
        if (out && cap)
            out[0] = 0;
        return;
    }
    for (cursor = path; *cursor; cursor++)
    {
        if (*cursor == L'\\' || *cursor == L'/')
            last = cursor + 1;
    }
    vcore_w_copy(out, cap, last);
}

static void
parent_of(const WCHAR *path, WCHAR *out, unsigned cap)
{
    unsigned n;
    unsigned i;
    unsigned cut = 0;
    int seen = 0;

    if (!out || cap == 0)
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
    if (!seen || cut == 0)
        return;
    if (cut >= cap)
        return;
    for (i = 0; i < cut; i++)
        out[i] = path[i];
    out[cut] = 0;
}

static DWORD
try_file(vcore_read_fn readfn, void *ctx, const WCHAR *path,
        vcore_profile *out, int src, int *found)
{
    DWORD status = load_at(readfn, ctx, path, out, src);
    if (status == VETVDM_E_NOPROG)
        return VETVDM_OK;
    *found = 1;
    return status;
}

static int
sha_ok(const vcore_profile *prof, const char *sha)
{
    if (!prof->match_sha[0] || !sha || !sha[0])
        return 1;
    return vcore_cstr_ieq(prof->match_sha, sha);
}

DWORD
vcore_profile_lookup(const WCHAR *const *dirs, int n_dirs,
                    const WCHAR *program,
                    const WCHAR *explicit_profile,
                    DWORD file_size,
                    const char *sha256_hex,
                    vcore_read_fn readfn, void *ctx,
                    vcore_profile *out)
{
    int i;
    int found;
    DWORD status;
    WCHAR path[MAX_PATH];
    WCHAR name[MAX_PATH];
    WCHAR parent[MAX_PATH];
    WCHAR upper[MAX_PATH];

    if (!out || !readfn)
        return VETVDM_E_INTERNAL;
    builtin_profile(out);

    if (explicit_profile && explicit_profile[0])
    {
        if (is_pathlike(explicit_profile))
        {
            status = load_at(readfn, ctx, explicit_profile, out, VCORE_SRC_EXPLICIT);
            if (status == VETVDM_E_NOPROG)
                return VETVDM_E_PROFILE;
            return status;
        }
        for (i = 0; i < n_dirs; i++)
        {
            WCHAR file[MAX_PATH];
            unsigned nlen;
            vcore_w_copy(file, MAX_PATH, explicit_profile);
            nlen = vcore_w_len(file);
            if (nlen < 5 || !vcore_w_ieq(file + nlen - 5, L".json"))
            {
                if (nlen + 6 >= MAX_PATH)
                    return VETVDM_E_PROFILE;
                file[nlen++] = L'.';
                file[nlen++] = L'j';
                file[nlen++] = L's';
                file[nlen++] = L'o';
                file[nlen++] = L'n';
                file[nlen] = 0;
            }
            if (!join_path(path, MAX_PATH, dirs[i], file))
                continue;
            found = 0;
            status = try_file(readfn, ctx, path, out, VCORE_SRC_EXPLICIT, &found);
            if (found)
                return status;

            vcore_w_copy(upper, MAX_PATH, explicit_profile);
            vcore_w_upper_ascii(upper);
            nlen = vcore_w_len(upper);
            if (nlen < 5 || !vcore_w_ieq(upper + nlen - 5, L".json"))
            {
                if (nlen + 6 >= MAX_PATH)
                    return VETVDM_E_PROFILE;
                upper[nlen++] = L'.';
                upper[nlen++] = L'j';
                upper[nlen++] = L's';
                upper[nlen++] = L'o';
                upper[nlen++] = L'n';
                upper[nlen] = 0;
            }
            if (!join_path(path, MAX_PATH, dirs[i], L"by-name"))
                continue;
            if (!join_path(name, MAX_PATH, path, upper))
                continue;
            found = 0;
            status = try_file(readfn, ctx, name, out, VCORE_SRC_EXPLICIT, &found);
            if (found)
                return status;
        }
        return VETVDM_E_PROFILE;
    }

    if (program && program[0])
    {
        parent_of(program, parent, MAX_PATH);
        if (parent[0] && join_path(path, MAX_PATH, parent, L"vettv.game.json"))
        {
            found = 0;
            status = try_file(readfn, ctx, path, out, VCORE_SRC_GAMEJSON, &found);
            if (found)
                return status;
        }
        parent_of(parent, name, MAX_PATH);
        if (name[0] && join_path(path, MAX_PATH, name, L"vettv.game.json"))
        {
            found = 0;
            status = try_file(readfn, ctx, path, out, VCORE_SRC_GAMEJSON, &found);
            if (found)
                return status;
        }
    }

    if (sha256_hex && sha256_hex[0])
    {
        WCHAR hash[80];
        if (vcore_utf8_to_wide(sha256_hex, hash, 80) != VETVDM_OK)
            return VETVDM_E_INTERNAL;
        {
            unsigned n = vcore_w_len(hash);
            if (n + 6 < 80)
            {
                hash[n++] = L'.';
                hash[n++] = L'j';
                hash[n++] = L's';
                hash[n++] = L'o';
                hash[n++] = L'n';
                hash[n] = 0;
            }
        }
        for (i = 0; i < n_dirs; i++)
        {
            if (!join_path(path, MAX_PATH, dirs[i], L"by-hash"))
                continue;
            if (!join_path(name, MAX_PATH, path, hash))
                continue;
            status = load_at(readfn, ctx, name, out, VCORE_SRC_BYHASH);
            if (status == VETVDM_E_NOPROG)
                continue;
            if (status != VETVDM_OK)
                return status;
            if (!sha_ok(out, sha256_hex))
            {
                builtin_profile(out);
                continue;
            }
            return VETVDM_OK;
        }
    }

    if (program && program[0])
    {
        file_name_of(program, upper, MAX_PATH);
        vcore_w_upper_ascii(upper);
        {
            unsigned n = vcore_w_len(upper);
            if (n + 6 < MAX_PATH)
            {
                upper[n++] = L'.';
                upper[n++] = L'j';
                upper[n++] = L's';
                upper[n++] = L'o';
                upper[n++] = L'n';
                upper[n] = 0;
            }
        }
        for (i = 0; i < n_dirs; i++)
        {
            if (!join_path(path, MAX_PATH, dirs[i], L"by-name"))
                continue;
            if (!join_path(name, MAX_PATH, path, upper))
                continue;
            status = load_at(readfn, ctx, name, out, VCORE_SRC_BYNAME);
            if (status == VETVDM_E_NOPROG)
                continue;
            if (status != VETVDM_OK)
                return status;
            if (!out->match_size_set || out->match_size != file_size)
            {
                builtin_profile(out);
                continue;
            }
            return VETVDM_OK;
        }
    }

    builtin_profile(out);
    return VETVDM_OK;
}

static int
is_sep(WCHAR ch)
{
    return ch == L'\\' || ch == L'/';
}

static int
is_abs(const WCHAR *path)
{
    if (!path || !path[0])
        return 0;
    if (is_sep(path[0]) && is_sep(path[1]))
        return 1;
    if (((path[0] >= L'A' && path[0] <= L'Z') ||
         (path[0] >= L'a' && path[0] <= L'z')) &&
        path[1] == L':')
        return 1;
    return 0;
}

DWORD
vcore_resolve_mount_path(const WCHAR *profile_file,
                        const WCHAR *mount_path,
                        WCHAR *out, DWORD cch)
{
    WCHAR combined[MAX_PATH];
    WCHAR parts[32][MAX_PATH];
    int nparts = 0;
    int i;
    unsigned prefix = 0;
    const WCHAR *cursor;
    unsigned used;

    if (!out || cch == 0)
        return VETVDM_E_INTERNAL;
    out[0] = 0;
    if (!mount_path || !mount_path[0])
        return VETVDM_E_PROFILE;

    if (is_abs(mount_path))
        vcore_w_copy(combined, MAX_PATH, mount_path);
    else
    {
        WCHAR dir[MAX_PATH];
        parent_of(profile_file, dir, MAX_PATH);
        if (!dir[0])
            return VETVDM_E_PROFILE;
        if (!join_path(combined, MAX_PATH, dir, mount_path))
            return VETVDM_E_CMDLINE;
    }

    if (is_sep(combined[0]) && is_sep(combined[1]))
    {
        unsigned n = 2;
        int slashes = 0;
        while (combined[n] && slashes < 2)
        {
            if (is_sep(combined[n]))
                slashes++;
            if (slashes == 2)
                break;
            n++;
        }
        prefix = n;
    }
    else if (combined[1] == L':')
    {
        prefix = (is_sep(combined[2])) ? 3u : 2u;
    }

    cursor = combined + prefix;
    while (*cursor)
    {
        const WCHAR *start;
        unsigned len;
        while (is_sep(*cursor))
            cursor++;
        if (!*cursor)
            break;
        start = cursor;
        while (*cursor && !is_sep(*cursor))
            cursor++;
        len = (unsigned)(cursor - start);
        if (len == 1 && start[0] == L'.')
            continue;
        if (len == 2 && start[0] == L'.' && start[1] == L'.')
        {
            if (nparts == 0)
                return VETVDM_E_PROFILE;
            nparts--;
            continue;
        }
        if (nparts >= 32 || len >= MAX_PATH)
            return VETVDM_E_CMDLINE;
        for (i = 0; i < (int)len; i++)
            parts[nparts][i] = start[i];
        parts[nparts][len] = 0;
        nparts++;
    }

    used = 0;
    if (prefix >= cch)
        return VETVDM_E_CMDLINE;
    for (i = 0; i < (int)prefix; i++)
    {
        WCHAR ch = combined[i];
        out[used++] = is_sep(ch) ? L'\\' : ch;
    }
    if (prefix == 2 && combined[1] == L':')
    {
        if (used + 1 >= cch)
            return VETVDM_E_CMDLINE;
        out[used++] = L'\\';
    }
    out[used] = 0;
    for (i = 0; i < nparts; i++)
    {
        unsigned len = vcore_w_len(parts[i]);
        int put_sep = !(used > 0 && out[used - 1] == L'\\');
        if (used + (put_sep ? 1u : 0u) + len + 1u > cch)
            return VETVDM_E_CMDLINE;
        if (put_sep)
            out[used++] = L'\\';
        {
            unsigned k;
            for (k = 0; k < len; k++)
                out[used++] = parts[i][k];
        }
        out[used] = 0;
    }
    if (used == 0)
        return VETVDM_E_PROFILE;
    return VETVDM_OK;
}
