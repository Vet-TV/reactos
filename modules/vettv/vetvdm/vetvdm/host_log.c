/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/vetvdm/host_log.c
 * PURPOSE:  Shared vetvdm.log writer.
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

static void
mkdir_parents(const WCHAR *file)
{
    WCHAR dir[MAX_PATH];
    unsigned n;
    unsigned i;
    parent_dir(file, dir, MAX_PATH);
    n = vcore_w_len(dir);
    for (i = 0; i < n; i++)
    {
        if (dir[i] != L'\\')
            continue;
        if (i > 0 && dir[i - 1] == L':')
            continue;
        if (i == 0 || (i == 1 && dir[0] == L'\\'))
            continue;
        dir[i] = 0;
        CreateDirectoryW(dir, NULL);
        dir[i] = L'\\';
    }
    if (dir[0])
        CreateDirectoryW(dir, NULL);
}

void
write_log(host *st, DWORD code)
{
    vcore_log_rec rec;
    vcore_buf line;
    SYSTEMTIME now;
    char program[1024];
    char dosbox_utf[1024];
    char wine_utf[1024];
    char kind_buf[16];
    const char *caller;
    WCHAR path[MAX_PATH];
    HANDLE file;
    DWORD wrote = 0;
    DWORD end;
    WIN32_FILE_ATTRIBUTE_DATA info;

    if (st->skip_log || st->log_level == 0)
        return;
    if (st->cmd.dry_run && st->log_level < 2)
        return;
    if (st->cmd.log_path[0])
        vcore_w_copy(path, MAX_PATH, st->cmd.log_path);
    else if (st->have_roots &&
             vetvdm_path_succeeded(vetvdm_path_resolve(
                 &st->roots, VETVDM_PATH_SHARED_LOG, NULL, path, MAX_PATH,
                 NULL, TRUE, NULL, NULL)))
        ;
    else
        return;

    if (st->image[0])
    {
        if (vcore_wide_to_utf8(st->image, program, sizeof(program)) != VETVDM_OK)
            vcore_cstr_copy(program, sizeof(program), "-");
    }
    else if (st->cmd.image[0])
    {
        if (vcore_wide_to_utf8(st->cmd.image, program, sizeof(program)) != VETVDM_OK)
            vcore_cstr_copy(program, sizeof(program), "-");
    }
    else
        vcore_cstr_copy(program, sizeof(program), "-");

    if (st->cmd.from_hook)
        caller = "hook";
    else if (st->cmd.profile[0])
        caller = "shell";
    else
        caller = "cli";

    vcore_cstr_copy(kind_buf, sizeof(kind_buf), vetvdm_kind_name(st->kind));
    if (st->dosbox[0])
    {
        if (vcore_wide_to_utf8(st->dosbox, dosbox_utf, sizeof(dosbox_utf)) != VETVDM_OK)
            vcore_cstr_copy(dosbox_utf, sizeof(dosbox_utf), "-");
    }
    else
        vcore_cstr_copy(dosbox_utf, sizeof(dosbox_utf), "-");
    if (st->wine[0])
    {
        if (vcore_wide_to_utf8(st->wine, wine_utf, sizeof(wine_utf)) != VETVDM_OK)
            vcore_cstr_copy(wine_utf, sizeof(wine_utf), "-");
    }
    else
        vcore_cstr_copy(wine_utf, sizeof(wine_utf), "-");
    GetLocalTime(&now);
    end = GetTickCount();
    memset(&rec, 0, sizeof(rec));
    rec.year = now.wYear;
    rec.month = now.wMonth;
    rec.day = now.wDay;
    rec.hour = now.wHour;
    rec.min = now.wMinute;
    rec.sec = now.wSecond;
    rec.pid = GetCurrentProcessId();
    rec.caller = caller;
    rec.program = program;
    rec.sha256 = st->sha[0] ? st->sha : NULL;
    rec.kind = kind_buf;
    rec.layer = vetvdm_layer_name(st->layer);
    rec.profile_id = st->profile.id[0] ? st->profile.id : NULL;
    rec.exit_code = code;
    rec.duration_ms = end - st->started;
    rec.dosbox = dosbox_utf;
    rec.wine = wine_utf;
    rec.wine_from = st->wine_from[0] ? st->wine_from : NULL;
    rec.warn = st->warn[0] ? st->warn : NULL;
    if (vcore_log_format(&rec, &line) != VETVDM_OK)
        return;

    if (GetFileAttributesExW(path, GetFileExInfoStandard, &info))
    {
        if (info.nFileSizeHigh != 0 || info.nFileSizeLow >= VETVDM_LOG_ROTATE)
        {
            WCHAR rotated[MAX_PATH];
            unsigned n = vcore_w_len(path);
            if (n + 3 < MAX_PATH)
            {
                unsigned k;
                for (k = 0; k < n; k++)
                    rotated[k] = path[k];
                rotated[n] = L'.';
                rotated[n + 1] = L'1';
                rotated[n + 2] = 0;
                MoveFileExW(path, rotated, MOVEFILE_REPLACE_EXISTING);
            }
        }
    }
    mkdir_parents(path);
    file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                      OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        vcore_buf_free(&line);
        return;
    }
    WriteFile(file, line.data, (DWORD)line.len, &wrote, NULL);
    CloseHandle(file);
    vcore_buf_free(&line);
}
