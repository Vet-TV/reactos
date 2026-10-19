/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/vetvdm/host_route.c
 * PURPOSE:  Policy load, image resolution, and layer routing.
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
#include "image.h"
#include "route.h"
#include "vcore_util.h"

#include <shlobj.h>
#include <stdio.h>
#include <string.h>

static void
locate_sibling(const WCHAR *configured, const WCHAR *const *names,
              int nnames, WCHAR *out, DWORD cch, int *found)
{
    int i;
    *found = 0;
    out[0] = 0;
    if (!configured || !configured[0])
        return;
    vcore_w_copy(out, cch, configured);
    if (file_exists(configured))
    {
        *found = 1;
        return;
    }
    for (i = 0; i < nnames; i++)
    {
        WCHAR sibling[MAX_PATH];
        if (!replace_file(configured, names[i], sibling, MAX_PATH))
            continue;
        if (file_exists(sibling))
        {
            vcore_w_copy(out, cch, sibling);
            *found = 1;
            return;
        }
    }
}

/* Installer copies the pinned SDL2 build to dosbox-x.exe. Launch that name. */
static void
locate_dosbox(const WCHAR *configured, WCHAR *out, DWORD cch, int *found)
{
    *found = 0;
    out[0] = 0;
    if (!configured || !configured[0])
        return;
    vcore_w_copy(out, cch, configured);
    *found = file_exists(configured);
}

/*
 * WineVdmPath, when set, overrides the default. The default is
 * %SystemDrive%\VetTV\winevdm\otvdmw.exe. If that file is missing, the
 * second fallback is %ProgramFiles%\otvdm\otvdmw.exe (then otvdm.exe).
 */
static void
locate_wine(const WCHAR *configured, const WCHAR *program_files,
           WCHAR *out, DWORD cch, int *found)
{
    WCHAR fallback[MAX_PATH];
    static const WCHAR *names[] = {
        L"otvdmw.exe",
        L"otvdm.exe"
    };

    *found = 0;
    out[0] = 0;
    if (configured && configured[0] && file_exists(configured))
    {
        vcore_w_copy(out, cch, configured);
        *found = 1;
        return;
    }
    fallback[0] = 0;
    if (program_files && program_files[0])
        join_path(fallback, MAX_PATH, program_files, L"otvdm\\otvdmw.exe");
    if (fallback[0])
    {
        locate_sibling(fallback, names, 2, out, cch, found);
        if (*found)
            return;
    }
    if (configured && configured[0])
        vcore_w_copy(out, cch, configured);
}

static int
win31_probe(const WCHAR *root, WCHAR *guest, DWORD cch)
{
    WCHAR path[MAX_PATH];
    if (guest && cch)
        guest[0] = 0;
    if (!root || !root[0] || !dir_exists(root))
        return 0;
    if (join_path(path, MAX_PATH, root, L"WINDOWS\\WIN.COM") && file_exists(path))
    {
        vcore_w_copy(guest, cch, L"C:\\WINDOWS\\WIN.COM");
        return 1;
    }
    if (join_path(path, MAX_PATH, root, L"WIN.COM") && file_exists(path))
    {
        vcore_w_copy(guest, cch, L"C:\\WIN.COM");
        return 1;
    }
    return 0;
}

static int
passthrough_hit(const vcore_policy *pol, const WCHAR *image, const char *sha)
{
    WCHAR name[MAX_PATH];
    int i;
    file_part(image, name, MAX_PATH);
    vcore_w_upper_ascii(name);
    for (i = 0; i < pol->n_pass; i++)
    {
        WCHAR entry[MAX_PATH];
        char utf[128];
        vcore_w_copy(entry, MAX_PATH, pol->pass_through[i]);
        vcore_w_upper_ascii(entry);
        if (entry[0] && vcore_w_ieq(entry, name))
            return 1;
        if (sha && sha[0] &&
            vcore_wide_to_utf8(pol->pass_through[i], utf, sizeof(utf)) == VETVDM_OK &&
            vcore_cstr_ieq(utf, sha))
            return 1;
    }
    return 0;
}

static void
fill_defaults(host *st)
{
    WCHAR path[MAX_PATH];
    vcore_policy *pol = &st->policy;

    memset(pol, 0, sizeof(*pol));
    pol->enabled = 1;
    pol->log_level = 1;
    vcore_w_copy(pol->dos_mode, 16, L"dosboxx");
    vcore_w_copy(pol->win16_mode, 16, L"winevdm");
    if (st->have_roots &&
        vetvdm_path_succeeded(vetvdm_path_resolve(
            &st->roots, VETVDM_PATH_DOSBOXX, NULL, path, MAX_PATH,
            NULL, FALSE, NULL, NULL)))
        vcore_w_copy(pol->dosboxx_path, MAX_PATH, path);
    if (st->have_roots &&
        vetvdm_path_succeeded(vetvdm_path_resolve(
            &st->roots, VETVDM_PATH_PROFILES, NULL, path, MAX_PATH,
            NULL, FALSE, NULL, NULL)))
    {
        vcore_w_copy(pol->profile_dirs[0], MAX_PATH, path);
        pol->n_profile_dirs = 1;
    }
    if (st->have_roots &&
        vetvdm_path_succeeded(vetvdm_path_resolve(
            &st->roots, VETVDM_PATH_CONF_TEMPLATE, NULL, path, MAX_PATH,
            NULL, FALSE, NULL, NULL)))
        vcore_w_copy(pol->conf_template, MAX_PATH, path);
    if (st->have_roots && st->roots.system_drive[0] &&
        join_path(path, MAX_PATH, st->roots.system_drive, L"VetTV\\winevdm\\otvdmw.exe"))
        vcore_w_copy(pol->winevdm_path, MAX_PATH, path);
}

DWORD
load_policy(host *st)
{
    HKEY machine = NULL;
    HKEY user = NULL;
    vetvdm_reg_ctx machine_ctx;
    vetvdm_reg_ctx user_ctx;
    vcore_policy_source machine_src;
    vcore_policy_source user_src;
    vcore_policy defaults;
    UINT nroot;
    HRESULT hr;

    if (vetvdm_path_query_roots(&st->roots) == VETVDM_PATH_OK)
        st->have_roots = 1;
    st->expand.system_drive = st->have_roots ? st->roots.system_drive : L"C:";
    nroot = GetSystemWindowsDirectoryW(st->system_root, MAX_PATH);
    if (nroot > 0 && nroot < MAX_PATH)
        st->expand.system_root = st->system_root;
    hr = SHGetFolderPathW(NULL, CSIDL_PROGRAM_FILES, NULL, SHGFP_TYPE_CURRENT,
                         st->program_files);
    if (FAILED(hr))
        st->program_files[0] = 0;
    else
        st->expand.program_files = st->program_files;

    fill_defaults(st);
    defaults = st->policy;
    vetvdm_reg_open(HKEY_LOCAL_MACHINE, &machine);
    vetvdm_reg_open(HKEY_CURRENT_USER, &user);
    memset(&machine_ctx, 0, sizeof(machine_ctx));
    memset(&user_ctx, 0, sizeof(user_ctx));
    machine_ctx.key = machine;
    machine_ctx.expand = &st->expand;
    user_ctx.key = user;
    user_ctx.expand = &st->expand;
    vetvdm_reg_source(&machine_src, &machine_ctx);
    vetvdm_reg_source(&user_src, &user_ctx);
    if (!machine)
        memset(&machine_src, 0, sizeof(machine_src));
    if (!user)
        memset(&user_src, 0, sizeof(user_src));
    vcore_policy_load(machine ? &machine_src : NULL,
                     user ? &user_src : NULL,
                     &defaults, &st->expand, &st->policy);
    vetvdm_reg_close(machine);
    vetvdm_reg_close(user);
    st->log_level = st->policy.log_level;
    return VETVDM_OK;
}

static void
remember_choice(host *st)
{
    WCHAR dir[MAX_PATH];
    WCHAR file[MAX_PATH];
    WCHAR hash[80];
    char body[256];
    HANDLE out;
    DWORD wrote = 0;
    unsigned n;

    if (!st->sha[0] || st->policy.n_profile_dirs < 1)
    {
        warn_add(st->warn, sizeof(st->warn), "remember-skipped");
        return;
    }
    if (vcore_utf8_to_wide(st->sha, hash, 70) != VETVDM_OK)
        return;
    n = vcore_w_len(hash);
    if (n + 6 >= 80)
        return;
    hash[n++] = L'.';
    hash[n++] = L'j';
    hash[n++] = L's';
    hash[n++] = L'o';
    hash[n++] = L'n';
    hash[n] = 0;
    if (!join_path(dir, MAX_PATH, st->policy.profile_dirs[0], L"by-hash"))
        return;
    CreateDirectoryW(st->policy.profile_dirs[0], NULL);
    CreateDirectoryW(dir, NULL);
    if (!join_path(file, MAX_PATH, dir, hash))
        return;
    _snprintf(body, sizeof(body),
             "{\n  \"id\": \"remember\",\n  \"layer\": \"%s\",\n"
             "  \"match\": { \"sha256\": \"%s\" }\n}\n",
             vetvdm_layer_name(st->layer), st->sha);
    body[sizeof(body) - 1] = 0;
    out = CreateFileW(file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                     FILE_ATTRIBUTE_NORMAL, NULL);
    if (out == INVALID_HANDLE_VALUE)
    {
        warn_add(st->warn, sizeof(st->warn), "remember-failed");
        return;
    }
    n = 0;
    while (body[n])
        n++;
    if (!WriteFile(out, body, n, &wrote, NULL) || wrote != n)
        warn_add(st->warn, sizeof(st->warn), "remember-failed");
    CloseHandle(out);
}

DWORD
resolve_image(host *st)
{
    DWORD status;
    DWORD attr;
    DWORD header_len = 0;
    int kind;

    status = full_path(st->cmd.image, st->image, MAX_PATH);
    if (status != VETVDM_OK)
    {
        st->why = "bad image path";
        return VETVDM_E_NOPROG;
    }
    attr = GetFileAttributesW(st->image);
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY))
    {
        st->why = "program not found";
        return VETVDM_E_NOPROG;
    }
    status = vetvdm_load_image_info(st->image, &st->header, &header_len,
                                  st->sha, &st->file_size);
    if (status != VETVDM_OK)
    {
        st->why = "program not found";
        return status;
    }
    st->cls = vetvdm_classify_image(st->header, header_len);
    kind = vetvdm_kind_for_image(st->cmd.kind, st->cls, st->image);
    if (kind == VETVDM_KIND_AUTO)
    {
        if (st->cls == VETVDM_IMG_WIN16)
            kind = VETVDM_KIND_WIN16;
        else if (st->cls == VETVDM_IMG_DOS_MZ || st->cls == VETVDM_IMG_DOS_LE ||
                 st->cls == VETVDM_IMG_DOS_LX)
            kind = VETVDM_KIND_DOS;
    }
    st->kind = kind;

    if (st->cmd.cwd[0])
    {
        status = full_path(st->cmd.cwd, st->cwd, MAX_PATH);
        if (status != VETVDM_OK)
        {
            st->why = "bad cwd";
            return VETVDM_E_CMDLINE;
        }
    }
    else if (st->cmd.from_hook)
    {
        DWORD n = GetCurrentDirectoryW(MAX_PATH, st->cwd);
        if (n == 0 || n >= MAX_PATH)
            st->cwd[0] = 0;
    }
    else
        parent_dir(st->image, st->cwd, MAX_PATH);
    return VETVDM_OK;
}

DWORD
route_image(host *st)
{
    const WCHAR *dirs[VCORE_PROFILE_DIRS_MAX];
    WCHAR profile_layer[16];
    vetvdm_route_input in;
    vetvdm_route_result out;
    DWORD status;
    int i;
    int remember = 0;

    for (i = 0; i < st->policy.n_profile_dirs && i < VCORE_PROFILE_DIRS_MAX; i++)
        dirs[i] = st->policy.profile_dirs[i];
    status = vcore_profile_lookup(dirs, st->policy.n_profile_dirs, st->image,
                                st->cmd.profile[0] ? st->cmd.profile : NULL,
                                st->file_size, st->sha, vetvdm_read_file, NULL,
                                &st->profile);
    if (status != VETVDM_OK)
    {
        st->why = "profile error";
        return status;
    }
    profile_layer[0] = 0;
    if (st->profile.layer[0])
        vcore_utf8_to_wide(st->profile.layer, profile_layer, 16);

    memset(&in, 0, sizeof(in));
    in.image = st->cls;
    in.kind = st->kind;
    in.forced_layer = st->cmd.layer;
    in.dos_mode = st->policy.dos_mode;
    in.win16_mode = st->policy.win16_mode;
    in.profile_layer = profile_layer;
    in.passthrough = passthrough_hit(&st->policy, st->image, st->sha);
    status = vetvdm_decide_route(&in, &out);
    if (status != VETVDM_OK)
    {
        st->why = (st->cls == VETVDM_IMG_PE) ? "not a DOS or Win16 image" : "not a DOS or Win16 image";
        st->layer = VETVDM_LAYER_NONE;
        return status;
    }
    st->layer = out.layer;
    st->forced = (st->cmd.layer != VETVDM_LAYER_NONE) ? 1 : 0;

    if (out.ask && !st->cmd.dry_run)
    {
        int choice = vetvdm_ask(st->kind == VETVDM_KIND_WIN16, &remember);
        if (choice == 0)
        {
            st->why = "cancelled";
            return VETVDM_E_CANCEL;
        }
        if (choice == 3)
            warn_add(st->warn, sizeof(st->warn), "ask-no-desktop");
        else
        {
            if (st->kind == VETVDM_KIND_WIN16)
                st->layer = (choice == 1) ? VETVDM_LAYER_WINEVDM : VETVDM_LAYER_WIN31;
            else
                st->layer = (choice == 1) ? VETVDM_LAYER_DOSBOXX : VETVDM_LAYER_NTVDM;
            st->forced = 1;
            if (remember)
                remember_choice(st);
        }
    }
    else if (out.ask)
        warn_add(st->warn, sizeof(st->warn), "ask");
    return VETVDM_OK;
}

void
locate_layers(host *st)
{
    locate_dosbox(st->policy.dosboxx_path, st->dosbox, MAX_PATH, &st->dosbox_ok);
    locate_wine(st->policy.winevdm_path, st->program_files, st->wine, MAX_PATH,
               &st->wine_ok);
    vcore_w_copy(st->win31_root, MAX_PATH, st->policy.win31_path);
    st->win31_ok = win31_probe(st->win31_root, st->wincom, 64);
}
