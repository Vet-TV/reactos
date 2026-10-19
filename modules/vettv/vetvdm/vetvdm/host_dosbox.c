/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/vetvdm/host_dosbox.c
 * PURPOSE:  DOSBox-X conf generation, conf write, and errorlevel read.
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

#include <stdlib.h>
#include <string.h>

static void
dos_guest_name(const host *st, WCHAR *out, DWORD cch)
{
    const char *guest = st->profile.guest_exe;
    int plain = guest && guest[0];
    unsigned i;
    for (i = 0; guest && guest[i]; i++)
    {
        if (guest[i] == '\\' || guest[i] == '/' || guest[i] == ':')
            plain = 0;
    }
    if (plain && vcore_utf8_to_wide(guest, out, cch) == VETVDM_OK && out[0])
        return;
    file_part(st->image, out, cch);
}

static unsigned
strip_dir(WCHAR *path)
{
    unsigned n = vcore_w_len(path);
    while (n > 3 && (path[n - 1] == L'\\' || path[n - 1] == L'/'))
        path[--n] = 0;
    return n;
}

static int
prefix_rel(const WCHAR *root_in, const WCHAR *dir_in, WCHAR *rel, DWORD cch)
{
    WCHAR root[MAX_PATH];
    WCHAR dir[MAX_PATH];
    unsigned nr;
    unsigned i;

    vcore_w_copy(root, MAX_PATH, root_in ? root_in : L"");
    vcore_w_copy(dir, MAX_PATH, dir_in ? dir_in : L"");
    nr = strip_dir(root);
    strip_dir(dir);
    if (!nr || vcore_w_len(dir) < nr)
        return -1;
    for (i = 0; i < nr; i++)
    {
        WCHAR a = root[i];
        WCHAR b = dir[i];
        if (a == L'/')
            a = L'\\';
        if (b == L'/')
            b = L'\\';
        if (vcore_w_lower(a) != vcore_w_lower(b))
            return -1;
    }
    if (dir[nr] == 0)
    {
        if (rel && cch)
            rel[0] = 0;
        return (int)nr;
    }
    if (dir[nr] != L'\\' && dir[nr] != L'/')
        return -1;
    if (rel)
        vcore_w_copy(rel, cch, dir + nr + 1);
    return (int)nr;
}

static char
upper_drive(char drive)
{
    if (drive >= 'a' && drive <= 'z')
        return (char)(drive - 'a' + 'A');
    return drive;
}

static char
pick_result_drive(const vcore_mount_spec *mounts, int n)
{
    char letter;
    for (letter = 'V'; letter >= 'A'; letter--)
    {
        int i;
        int used = 0;
        for (i = 0; i < n; i++)
        {
            if (upper_drive(mounts[i].drive) == letter)
                used = 1;
        }
        if (!used)
            return letter;
    }
    return 0;
}

static DWORD
add_mount(vcore_mount_spec *mounts, int *n, int maxn, char drive, int type,
         const WCHAR *path)
{
    vcore_mount_spec *slot;
    if (*n >= maxn)
        return VETVDM_E_INTERNAL;
    slot = &mounts[*n];
    memset(slot, 0, sizeof(*slot));
    slot->drive = upper_drive(drive);
    slot->type = type;
    if (vcore_wide_to_utf8(path, slot->path, sizeof(slot->path)) != VETVDM_OK)
        return VETVDM_E_CMDLINE;
    (*n)++;
    return VETVDM_OK;
}

static DWORD
fill_args(const host *st, char *args_utf, DWORD cap)
{
    args_utf[0] = 0;
    if (st->cmd.args && st->cmd.args[0])
    {
        if (!oem_ok(st->cmd.args))
            return VETVDM_E_CMDLINE;
        return vcore_wide_to_utf8(st->cmd.args, args_utf, cap);
    }
    if (st->profile.guest_args[0])
    {
        WCHAR wide[200];
        if (vcore_utf8_to_wide(st->profile.guest_args, wide, 200) != VETVDM_OK)
            return VETVDM_E_PROFILE;
        if (!oem_ok(wide))
            return VETVDM_E_CMDLINE;
        vcore_cstr_copy(args_utf, cap, st->profile.guest_args);
    }
    return VETVDM_OK;
}

DWORD
build_conf(host *st, int win31, vcore_buf *conf, WCHAR *conf_path, DWORD cch_path)
{
    vcore_mount_spec mounts[10];
    int n_mounts = 0;
    WCHAR prog_dir[MAX_PATH];
    WCHAR rel[MAX_PATH];
    WCHAR guest[MAX_PATH];
    char guest_utf[MAX_PATH * 3];
    char args_utf[2048];
    char program_line[1024];
    char cd_rel[MAX_PATH * 3];
    char work[4];
    char result[4];
    char logfile[1024];
    char *template_text = NULL;
    vcore_conf_input input;
    vcore_lock_report locks;
    DWORD status;
    int best = -1;
    int best_i = -1;
    int i;
    int truncated = 0;
    char result_letter;

    memset(mounts, 0, sizeof(mounts));
    parent_dir(st->image, prog_dir, MAX_PATH);
    if (!prog_dir[0])
        return VETVDM_E_CMDLINE;
    if (!st->have_temp)
    {
        status = vcore_tempdir_create(st->temp);
        if (status != VETVDM_OK)
            return status;
        st->have_temp = 1;
    }
    if (!join_path(conf_path, cch_path, st->temp, L"dosbox-x.conf"))
        return VETVDM_E_INTERNAL;

    work[0] = 0;
    cd_rel[0] = 0;
    result[0] = 0;
    program_line[0] = 0;

    if (win31)
    {
        if (!st->win31_ok || !st->win31_root[0] || !st->wincom[0])
            return VETVDM_E_WIN31;
        status = add_mount(mounts, &n_mounts, 10, 'C', VCORE_MOUNT_DIR, st->win31_root);
        if (status != VETVDM_OK)
            return status;
        status = add_mount(mounts, &n_mounts, 10, 'D', VCORE_MOUNT_DIR, prog_dir);
        if (status != VETVDM_OK)
            return status;
    }
    else if (st->profile.n_mounts > 0)
    {
        for (i = 0; i < st->profile.n_mounts; i++)
        {
            WCHAR resolved[MAX_PATH];
            const vcore_mount *src = &st->profile.mounts[i];
            status = vcore_resolve_mount_path(st->profile.source_path, src->path,
                                            resolved, MAX_PATH);
            if (status != VETVDM_OK)
                return status;
            status = add_mount(mounts, &n_mounts, 10, (char)src->drive, src->type, resolved);
            if (status != VETVDM_OK)
                return status;
            if (src->type == VCORE_MOUNT_DIR)
            {
                int score = prefix_rel(resolved, prog_dir, rel, MAX_PATH);
                if (score > best)
                {
                    best = score;
                    best_i = n_mounts - 1;
                }
            }
        }
        if (best_i >= 0)
        {
            WCHAR resolved[MAX_PATH];
            work[0] = upper_drive(mounts[best_i].drive);
            work[1] = 0;
            status = vcore_resolve_mount_path(st->profile.source_path,
                                            st->profile.mounts[best_i].path,
                                            resolved, MAX_PATH);
            if (status != VETVDM_OK)
                return status;
            rel[0] = 0;
            prefix_rel(resolved, prog_dir, rel, MAX_PATH);
            if (rel[0])
            {
                if (!oem_ok(rel))
                {
                    st->why = "program name is not usable in DOS";
                    return VETVDM_E_CMDLINE;
                }
                if (vcore_wide_to_utf8(rel, cd_rel, sizeof(cd_rel)) != VETVDM_OK)
                    return VETVDM_E_CMDLINE;
            }
        }
        else
        {
            for (i = 0; i < n_mounts; i++)
            {
                if (mounts[i].type == VCORE_MOUNT_DIR)
                {
                    work[0] = upper_drive(mounts[i].drive);
                    work[1] = 0;
                    break;
                }
            }
        }
    }
    else
    {
        status = add_mount(mounts, &n_mounts, 10, 'C', VCORE_MOUNT_DIR, prog_dir);
        if (status != VETVDM_OK)
            return status;
        work[0] = 'C';
        work[1] = 0;
    }

    result_letter = pick_result_drive(mounts, n_mounts);
    if (!result_letter)
        return VETVDM_E_INTERNAL;
    status = add_mount(mounts, &n_mounts, 10, result_letter, VCORE_MOUNT_DIR, st->temp);
    if (status != VETVDM_OK)
        return status;
    result[0] = result_letter;
    result[1] = 0;

    status = fill_args(st, args_utf, sizeof(args_utf));
    if (status != VETVDM_OK)
    {
        if (status == VETVDM_E_CMDLINE)
            st->why = "program name is not usable in DOS";
        return status;
    }

    if (win31)
    {
        char game_utf[MAX_PATH * 3];
        char game_cmd[600];
        WCHAR game_wide[MAX_PATH];
        dos_guest_name(st, guest, MAX_PATH);
        if (!oem_ok(guest) || !oem_ok(st->wincom))
        {
            st->why = "program name is not usable in DOS";
            return VETVDM_E_CMDLINE;
        }
        if (vcore_w_len(guest) + 4 >= MAX_PATH)
            return VETVDM_E_CMDLINE;
        game_wide[0] = L'D';
        game_wide[1] = L':';
        game_wide[2] = L'\\';
        vcore_w_copy(game_wide + 3, MAX_PATH - 3, guest);
        if (vcore_wide_to_utf8(game_wide, game_utf, sizeof(game_utf)) != VETVDM_OK)
            return VETVDM_E_CMDLINE;
        status = vcore_dos_command(game_utf, args_utf, game_cmd, sizeof(game_cmd), &truncated);
        if (status != VETVDM_OK)
            return status;
        if (vcore_wide_to_utf8(st->wincom, guest_utf, sizeof(guest_utf)) != VETVDM_OK)
            return VETVDM_E_CMDLINE;
        if (vcore_cstr_len(guest_utf) + 1 + vcore_cstr_len(game_cmd) + 1 > sizeof(program_line))
            return VETVDM_E_CMDLINE;
        vcore_cstr_copy(program_line, sizeof(program_line), guest_utf);
        {
            unsigned used = vcore_cstr_len(program_line);
            program_line[used++] = ' ';
            vcore_cstr_copy(program_line + used, sizeof(program_line) - used, game_cmd);
        }
        work[0] = 0;
        cd_rel[0] = 0;
    }
    else
    {
        dos_guest_name(st, guest, MAX_PATH);
        if (!oem_ok(guest))
        {
            st->why = "program name is not usable in DOS";
            return VETVDM_E_CMDLINE;
        }
        if (vcore_wide_to_utf8(guest, guest_utf, sizeof(guest_utf)) != VETVDM_OK)
            return VETVDM_E_CMDLINE;
        status = vcore_dos_command(guest_utf, args_utf, program_line, sizeof(program_line),
                                  &truncated);
        if (status != VETVDM_OK)
            return status;
    }
    if (truncated)
        warn_add(st->warn, sizeof(st->warn), "truncated-tail");

    if (vcore_wide_to_utf8(st->temp, logfile, sizeof(logfile)) != VETVDM_OK)
        return VETVDM_E_CMDLINE;
    {
        unsigned used = vcore_cstr_len(logfile);
        const char *tail = "/dosbox.log";
        unsigned k;
        if (used && logfile[used - 1] == '\\')
            used--;
        if (used + 11 >= sizeof(logfile))
            return VETVDM_E_CMDLINE;
        for (k = 0; tail[k]; k++)
            logfile[used++] = (tail[k] == '/') ? '\\' : tail[k];
        logfile[used] = 0;
    }

    if (st->policy.conf_template[0])
    {
        char *buf = (char *)malloc(256u * 1024u);
        DWORD got = 0;
        int rc;
        if (!buf)
            return VETVDM_E_INTERNAL;
        rc = vetvdm_read_file(NULL, st->policy.conf_template, buf, 256u * 1024u, &got);
        if (rc == 1)
        {
            template_text = buf;
            buf[got] = 0;
        }
        else
        {
            free(buf);
            if (rc < 0)
                warn_add(st->warn, sizeof(st->warn), "template-unreadable");
        }
    }

    memset(&input, 0, sizeof(input));
    input.template_text = template_text;
    input.profile = &st->profile;
    input.mounts = mounts;
    input.n_mounts = n_mounts;
    input.workdir_drive = work[0] ? work : NULL;
    input.cd_rel = cd_rel[0] ? cd_rel : NULL;
    input.program_line = program_line;
    input.args_for_profile = args_utf;
    input.use_profile_autoexec = (!win31 && st->profile.n_autoexec > 0) ? 1 : 0;
    input.result_drive = result;
    input.logfile = logfile;
    status = vcore_conf_build(&input, conf, &locks);
    free(template_text);
    if (status != VETVDM_OK)
        return status;
    if (locks.forced_hostkey)
        warn_add(st->warn, sizeof(st->warn), "locked-hostkey");
    if (locks.stripped_mapperfile)
        warn_add(st->warn, sizeof(st->warn), "stripped-mapperfile");
    return VETVDM_OK;
}

DWORD
write_conf(const WCHAR *path, const char *text)
{
    HANDLE file;
    DWORD len = 0;
    DWORD wrote = 0;
    if (!text)
        return VETVDM_E_INTERNAL;
    while (text[len])
        len++;
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                      FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return VETVDM_E_INTERNAL;
    if (len && (!WriteFile(file, text, len, &wrote, NULL) || wrote != len))
    {
        CloseHandle(file);
        return VETVDM_E_INTERNAL;
    }
    CloseHandle(file);
    return VETVDM_OK;
}

DWORD
read_errorlevel(const WCHAR *dir, DWORD *code)
{
    WCHAR path[MAX_PATH];
    HANDLE file;
    char buf[64];
    DWORD got = 0;
    unsigned i = 0;
    unsigned value = 0;
    int any = 0;

    if (!join_path(path, MAX_PATH, dir, L"ERRORLVL.TXT"))
        return VETVDM_E_NORESULT;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                      NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return VETVDM_E_NORESULT;
    if (!ReadFile(file, buf, sizeof(buf) - 1, &got, NULL))
    {
        CloseHandle(file);
        return VETVDM_E_NORESULT;
    }
    CloseHandle(file);
    buf[got] = 0;
    while (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\r' || buf[i] == '\n')
        i++;
    while (buf[i] >= '0' && buf[i] <= '9')
    {
        any = 1;
        value = value * 10u + (unsigned)(buf[i] - '0');
        if (value > 255u)
            return VETVDM_E_NORESULT;
        i++;
    }
    if (!any)
        return VETVDM_E_NORESULT;
    *code = value;
    return VETVDM_OK;
}
