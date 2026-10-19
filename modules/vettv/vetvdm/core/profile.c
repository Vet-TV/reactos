/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/core/profile.c
 * PURPOSE:  Profile JSON parse and the spec section 4 lookup order.
 *
 * Copyright (C) 2026 VetTV
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * Lookup order: --profile, vettv.game.json, by-hash, by-name plus size,
 * then built-in defaults. File bytes come from a callback so tests can
 * feed a fake tree and the host can use CreateFileW.
 */

#include "vetvdm_core.h"

#include "vcore_util.h"

#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wconversion"
#endif
#define JSMN_STATIC
#include "jsmn.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#define PROF_TOKS 512

static int
skip_tok(const jsmntok_t *t, int i, int ntok)
{
    int k;
    int next;

    if (i < 0 || i >= ntok)
        return ntok;
    if (t[i].type == JSMN_OBJECT)
    {
        next = i + 1;
        for (k = 0; k < t[i].size; k++)
        {
            next = skip_tok(t, next, ntok);
            next = skip_tok(t, next, ntok);
        }
        return next;
    }
    if (t[i].type == JSMN_ARRAY)
    {
        next = i + 1;
        for (k = 0; k < t[i].size; k++)
            next = skip_tok(t, next, ntok);
        return next;
    }
    return i + 1;
}

static int
tok_ieq(const char *js, const jsmntok_t *t, const char *lit)
{
    int n = t->end - t->start;
    int i = 0;

    for (; lit[i]; i++)
    {
        char a;
        char b;
        if (i >= n)
            return 0;
        a = js[t->start + i];
        b = lit[i];
        if (a >= 'A' && a <= 'Z')
            a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z')
            b = (char)(b - 'A' + 'a');
        if (a != b)
            return 0;
    }
    return i == n;
}

static int
obj_val(const char *js, const jsmntok_t *t, int ntok, int obj, const char *key)
{
    int i;
    int k;

    if (obj < 0 || obj >= ntok || t[obj].type != JSMN_OBJECT)
        return -1;
    i = obj + 1;
    for (k = 0; k < t[obj].size; k++)
    {
        int val;
        if (i >= ntok)
            return -1;
        val = skip_tok(t, i, ntok);
        if (t[i].type == JSMN_STRING && tok_ieq(js, &t[i], key))
            return val;
        i = skip_tok(t, val, ntok);
    }
    return -1;
}

static int
arr_at(const jsmntok_t *t, int ntok, int arr, int index)
{
    int i;
    int k;

    if (arr < 0 || arr >= ntok || t[arr].type != JSMN_ARRAY)
        return -1;
    if (index < 0 || index >= t[arr].size)
        return -1;
    i = arr + 1;
    for (k = 0; k < index; k++)
        i = skip_tok(t, i, ntok);
    return i;
}

static int
unescape(const char *js, const jsmntok_t *t, char *dst, unsigned cap)
{
    int i = t->start;
    unsigned n = 0;

    if (t->type != JSMN_STRING)
        return 0;
    while (i < t->end)
    {
        char ch = js[i++];
        if (ch == '\\' && i < t->end)
        {
            char e = js[i++];
            if (e == 'n')
                ch = '\n';
            else if (e == 't')
                ch = '\t';
            else if (e == 'r')
                ch = '\r';
            else if (e == '"' || e == '\\' || e == '/')
                ch = e;
            else if (e == 'u')
            {
                unsigned cp = 0;
                int h;
                for (h = 0; h < 4 && i < t->end; h++)
                {
                    char d = js[i++];
                    cp <<= 4;
                    if (d >= '0' && d <= '9')
                        cp |= (unsigned)(d - '0');
                    else if (d >= 'a' && d <= 'f')
                        cp |= (unsigned)(d - 'a' + 10);
                    else if (d >= 'A' && d <= 'F')
                        cp |= (unsigned)(d - 'A' + 10);
                    else
                        return -1;
                }
                if (cp < 0x80)
                    ch = (char)cp;
                else if (cp < 0x800)
                {
                    if (n + 3 > cap)
                        return -1;
                    dst[n++] = (char)(0xC0 | (cp >> 6));
                    dst[n++] = (char)(0x80 | (cp & 0x3F));
                    continue;
                }
                else
                {
                    if (n + 4 > cap)
                        return -1;
                    dst[n++] = (char)(0xE0 | (cp >> 12));
                    dst[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                    dst[n++] = (char)(0x80 | (cp & 0x3F));
                    continue;
                }
            }
            else
                return -1;
        }
        if (n + 1 >= cap)
            return -1;
        dst[n++] = ch;
    }
    if (n >= cap)
        return -1;
    dst[n] = 0;
    return 1;
}

static int
copy_tok(const char *js, const jsmntok_t *t, int idx, char *dst, unsigned cap)
{
    int n;
    if (idx < 0)
        return 0;
    if (t[idx].type == JSMN_STRING)
        return unescape(js, &t[idx], dst, cap);
    if (t[idx].type != JSMN_PRIMITIVE)
        return 0;
    if (tok_ieq(js, &t[idx], "null"))
        return 0;
    n = t[idx].end - t[idx].start;
    if (n < 0 || (unsigned)n + 1u > cap)
        return -1;
    memcpy(dst, js + t[idx].start, (size_t)n);
    dst[n] = 0;
    return 1;
}

static int
parse_u32(const char *text, DWORD *out)
{
    unsigned long value = 0;
    if (!text || !text[0])
        return 0;
    while (*text)
    {
        if (*text < '0' || *text > '9')
            return 0;
        if (value > (0xFFFFFFFFul / 10ul))
            return 0;
        value = value * 10ul + (unsigned long)(*text - '0');
        text++;
    }
    *out = (DWORD)value;
    return 1;
}

static int
mount_type(const char *text)
{
    if (!text || vcore_cstr_ieq(text, "dir"))
        return VCORE_MOUNT_DIR;
    if (vcore_cstr_ieq(text, "iso") || vcore_cstr_ieq(text, "cdrom"))
        return VCORE_MOUNT_ISO;
    if (vcore_cstr_ieq(text, "img") || vcore_cstr_ieq(text, "floppy") ||
        vcore_cstr_ieq(text, "hdd"))
        return VCORE_MOUNT_IMG;
    return 0;
}

static DWORD
fill_dosbox(const char *js, const jsmntok_t *t, int ntok, int dos,
           vcore_profile *out)
{
    int mounts;
    int autoexec;
    int extra;
    int i;

    if (dos < 0)
        return VETVDM_OK;
    if (t[dos].type == JSMN_PRIMITIVE)
        return tok_ieq(js, &t[dos], "null") ? VETVDM_OK : VETVDM_E_PROFILE;
    if (t[dos].type != JSMN_OBJECT)
        return VETVDM_E_PROFILE;
    if (copy_tok(js, t, obj_val(js, t, ntok, dos, "machine"),
                out->machine, sizeof(out->machine)) < 0 ||
        copy_tok(js, t, obj_val(js, t, ntok, dos, "memsize"),
                out->memsize, sizeof(out->memsize)) < 0 ||
        copy_tok(js, t, obj_val(js, t, ntok, dos, "cycles"),
                out->cycles, sizeof(out->cycles)) < 0 ||
        copy_tok(js, t, obj_val(js, t, ntok, dos, "sbtype"),
                out->sbtype, sizeof(out->sbtype)) < 0 ||
        copy_tok(js, t, obj_val(js, t, ntok, dos, "gus"),
                out->gus, sizeof(out->gus)) < 0 ||
        copy_tok(js, t, obj_val(js, t, ntok, dos, "mpu401"),
                out->mpu401, sizeof(out->mpu401)) < 0 ||
        copy_tok(js, t, obj_val(js, t, ntok, dos, "output"),
                out->output, sizeof(out->output)) < 0 ||
        copy_tok(js, t, obj_val(js, t, ntok, dos, "fullscreen"),
                out->fullscreen, sizeof(out->fullscreen)) < 0)
        return VETVDM_E_PROFILE;

    mounts = obj_val(js, t, ntok, dos, "mounts");
    if (mounts >= 0 && t[mounts].type == JSMN_ARRAY)
    {
        if (t[mounts].size > 8)
            return VETVDM_E_PROFILE;
        for (i = 0; i < t[mounts].size; i++)
        {
            int item = arr_at(t, ntok, mounts, i);
            int drive;
            int path;
            int type;
            char drive_txt[8];
            char path_txt[MAX_PATH * 3];
            char type_txt[16];
            WCHAR wide[MAX_PATH];
            int kind;
            if (item < 0 || t[item].type != JSMN_OBJECT)
                return VETVDM_E_PROFILE;
            drive = obj_val(js, t, ntok, item, "drive");
            path = obj_val(js, t, ntok, item, "path");
            type = obj_val(js, t, ntok, item, "type");
            if (copy_tok(js, t, drive, drive_txt, sizeof(drive_txt)) <= 0 ||
                !drive_txt[0])
                return VETVDM_E_PROFILE;
            if (copy_tok(js, t, path, path_txt, sizeof(path_txt)) <= 0)
                return VETVDM_E_PROFILE;
            type_txt[0] = 0;
            if (type >= 0 && copy_tok(js, t, type, type_txt, sizeof(type_txt)) < 0)
                return VETVDM_E_PROFILE;
            kind = mount_type(type_txt[0] ? type_txt : "dir");
            if (!kind)
                return VETVDM_E_PROFILE;
            if (vcore_utf8_to_wide(path_txt, wide, MAX_PATH) != VETVDM_OK)
                return VETVDM_E_PROFILE;
            out->mounts[out->n_mounts].drive = (WCHAR)drive_txt[0];
            out->mounts[out->n_mounts].type = kind;
            vcore_w_copy(out->mounts[out->n_mounts].path, MAX_PATH, wide);
            out->n_mounts++;
        }
    }
    else if (mounts >= 0 && t[mounts].type != JSMN_PRIMITIVE)
        return VETVDM_E_PROFILE;

    autoexec = obj_val(js, t, ntok, dos, "autoexec");
    if (autoexec >= 0 && t[autoexec].type == JSMN_ARRAY)
    {
        if (t[autoexec].size > 8)
            return VETVDM_E_PROFILE;
        for (i = 0; i < t[autoexec].size; i++)
        {
            int line = arr_at(t, ntok, autoexec, i);
            if (copy_tok(js, t, line, out->autoexec[out->n_autoexec], 256) <= 0)
                return VETVDM_E_PROFILE;
            out->n_autoexec++;
        }
    }
    else if (autoexec >= 0 && t[autoexec].type != JSMN_PRIMITIVE)
        return VETVDM_E_PROFILE;

    extra = obj_val(js, t, ntok, dos, "extra");
    if (extra >= 0 && t[extra].type == JSMN_OBJECT)
    {
        int sec_i = extra + 1;
        int s;
        for (s = 0; s < t[extra].size; s++)
        {
            int sec = skip_tok(t, sec_i, ntok);
            int key_i;
            int k;
            char sec_name[32];
            if (t[sec_i].type != JSMN_STRING ||
                unescape(js, &t[sec_i], sec_name, sizeof(sec_name)) <= 0)
                return VETVDM_E_PROFILE;
            if (sec < 0 || t[sec].type != JSMN_OBJECT)
                return VETVDM_E_PROFILE;
            key_i = sec + 1;
            for (k = 0; k < t[sec].size; k++)
            {
                int leaf = skip_tok(t, key_i, ntok);
                if (out->n_extra >= 12)
                    return VETVDM_E_PROFILE;
                if (unescape(js, &t[key_i], out->extra_key[out->n_extra], 48) <= 0)
                    return VETVDM_E_PROFILE;
                if (copy_tok(js, t, leaf, out->extra_val[out->n_extra], 160) <= 0)
                    return VETVDM_E_PROFILE;
                vcore_cstr_copy(out->extra_sec[out->n_extra], 32, sec_name);
                out->n_extra++;
                key_i = skip_tok(t, leaf, ntok);
            }
            sec_i = skip_tok(t, sec, ntok);
        }
    }
    else if (extra >= 0 && t[extra].type != JSMN_PRIMITIVE)
        return VETVDM_E_PROFILE;
    return VETVDM_OK;
}

DWORD
vcore_profile_parse(const char *json, DWORD len, vcore_profile *out)
{
    jsmn_parser parser;
    jsmntok_t *toks;
    int ntok;
    int id;
    int match;
    int dos;

    if (!out)
        return VETVDM_E_INTERNAL;
    memset(out, 0, sizeof(*out));
    if (!json)
        return VETVDM_E_PROFILE;
    toks = (jsmntok_t *)malloc(sizeof(jsmntok_t) * PROF_TOKS);
    if (!toks)
        return VETVDM_E_INTERNAL;
    jsmn_init(&parser);
    ntok = jsmn_parse(&parser, json, len, toks, PROF_TOKS);
    if (ntok == JSMN_ERROR_NOMEM)
    {
        free(toks);
        toks = (jsmntok_t *)malloc(sizeof(jsmntok_t) * (PROF_TOKS * 4));
        if (!toks)
            return VETVDM_E_INTERNAL;
        jsmn_init(&parser);
        ntok = jsmn_parse(&parser, json, len, toks, PROF_TOKS * 4);
    }
    if (ntok < 1 || toks[0].type != JSMN_OBJECT)
    {
        free(toks);
        return VETVDM_E_PROFILE;
    }
    id = obj_val(json, toks, ntok, 0, "id");
    if (id < 0)
        id = obj_val(json, toks, ntok, 0, "profile");
    if (copy_tok(json, toks, id, out->id, sizeof(out->id)) < 0 ||
        copy_tok(json, toks, obj_val(json, toks, ntok, 0, "layer"),
                out->layer, sizeof(out->layer)) < 0 ||
        copy_tok(json, toks, obj_val(json, toks, ntok, 0, "exe"),
                out->guest_exe, sizeof(out->guest_exe)) < 0 ||
        copy_tok(json, toks, obj_val(json, toks, ntok, 0, "args"),
                out->guest_args, sizeof(out->guest_args)) < 0)
    {
        free(toks);
        return VETVDM_E_PROFILE;
    }
    match = obj_val(json, toks, ntok, 0, "match");
    if (match >= 0 && toks[match].type == JSMN_OBJECT)
    {
        int size = obj_val(json, toks, ntok, match, "size");
        char size_txt[16];
        if (copy_tok(json, toks, obj_val(json, toks, ntok, match, "sha256"),
                    out->match_sha, sizeof(out->match_sha)) < 0 ||
            copy_tok(json, toks, obj_val(json, toks, ntok, match, "name"),
                    out->match_name, sizeof(out->match_name)) < 0)
        {
            free(toks);
            return VETVDM_E_PROFILE;
        }
        if (size >= 0 && toks[size].type != JSMN_PRIMITIVE)
        {
            free(toks);
            return VETVDM_E_PROFILE;
        }
        if (size >= 0)
        {
            if (copy_tok(json, toks, size, size_txt, sizeof(size_txt)) <= 0 ||
                !parse_u32(size_txt, &out->match_size))
            {
                free(toks);
                return VETVDM_E_PROFILE;
            }
            out->match_size_set = 1;
        }
    }
    else if (match >= 0 && toks[match].type != JSMN_PRIMITIVE)
    {
        free(toks);
        return VETVDM_E_PROFILE;
    }
    dos = obj_val(json, toks, ntok, 0, "dosbox");
    {
        DWORD status = fill_dosbox(json, toks, ntok, dos, out);
        free(toks);
        return status;
    }
}

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
