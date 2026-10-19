/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/vetvdm_paths.c
 * PURPOSE:  Path resolution for vetvdm shared and per-user files.
 *
 * Copyright (C) 2026 VetTV
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "vetvdm_paths.h"

#include <shlobj.h>
#include <string.h>

/* Brand directory under the system drive, and under the per-user shell folder. */
static const WCHAR g_brand[] = L"VetTV";
/* Per-user tail under the shell folder. Kept out of any environment string. */
static const WCHAR g_user_tail[] = L"VetTV\\DOS";
/* Used only when the shell folder cannot be used. Sits next to the exe. */
static const WCHAR g_fallback_dir[] = L"data";

static DWORD
wlen_cap(const WCHAR *text, DWORD cap, BOOL *terminated)
{
    DWORD n = 0;

    if (terminated)
        *terminated = FALSE;
    if (!text)
        return 0;
    while (n < cap)
    {
        if (text[n] == 0)
        {
            if (terminated)
                *terminated = TRUE;
            return n;
        }
        n++;
    }
    return n;
}

static BOOL
contains_percent(const WCHAR *text, DWORD cap)
{
    DWORD n = 0;

    if (!text)
        return FALSE;
    while (n < cap && text[n])
    {
        if (text[n] == L'%')
            return TRUE;
        n++;
    }
    /* No terminator inside the cap: treat as unusable. */
    if (n >= cap)
        return TRUE;
    return FALSE;
}

static BOOL
is_drive_letter(WCHAR ch)
{
    return (ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z');
}

static void
set_drive_c(WCHAR out[4])
{
    out[0] = L'C';
    out[1] = L':';
    out[2] = 0;
}

static BOOL
try_system_drive_value(const WCHAR *src, WCHAR out[4])
{
    DWORD n;

    if (!src || !is_drive_letter(src[0]) || src[1] != L':')
        return FALSE;

    for (n = 0; n < 4; n++)
    {
        if (src[n] == 0)
            break;
        if (src[n] == L'%')
            return FALSE;
    }
    if (n == 2 || (n == 3 && (src[2] == L'\\' || src[2] == L'/')))
    {
        out[0] = src[0];
        out[1] = L':';
        out[2] = 0;
        return TRUE;
    }
    return FALSE;
}

static BOOL
try_windows_dir(const WCHAR *dir, WCHAR out[4])
{
    DWORD i;

    if (!dir || !is_drive_letter(dir[0]) || dir[1] != L':')
        return FALSE;
    if (dir[2] != 0 && dir[2] != L'\\' && dir[2] != L'/')
        return FALSE;

    for (i = 0; i < MAX_PATH; i++)
    {
        if (dir[i] == 0)
        {
            out[0] = dir[0];
            out[1] = L':';
            out[2] = 0;
            return TRUE;
        }
        if (dir[i] == L'%')
            return FALSE;
    }
    return FALSE;
}

void
vetvdm_path_pick_drive(
    const WCHAR *system_drive_env,
    const WCHAR *system_windows_dir,
    WCHAR out[4])
{
    if (!out)
        return;
    if (try_system_drive_value(system_drive_env, out))
        return;
    if (try_windows_dir(system_windows_dir, out))
        return;
    set_drive_c(out);
}

static BOOL
is_absolute_path(const WCHAR *path)
{
    if (!path || !path[0])
        return FALSE;
    if ((path[0] == L'\\' && path[1] == L'\\') ||
        (path[0] == L'/' && path[1] == L'/'))
        return TRUE;
    if (is_drive_letter(path[0]) && path[1] == L':')
        return path[2] == 0 || path[2] == L'\\' || path[2] == L'/';
    return FALSE;
}

static BOOL
shell_root_usable(const vetvdm_path_roots *roots)
{
    BOOL terminated = FALSE;

    if (!roots->local_appdata_ok || !roots->local_appdata[0])
        return FALSE;
    wlen_cap(roots->local_appdata, MAX_PATH, &terminated);
    if (!terminated)
        return FALSE;
    if (contains_percent(roots->local_appdata, MAX_PATH))
        return FALSE;
    if (!is_absolute_path(roots->local_appdata))
        return FALSE;
    return TRUE;
}

static BOOL
exe_dir_usable(const vetvdm_path_roots *roots)
{
    BOOL terminated = FALSE;

    if (!roots->exe_dir_ok || !roots->exe_dir[0])
        return FALSE;
    wlen_cap(roots->exe_dir, MAX_PATH, &terminated);
    if (!terminated)
        return FALSE;
    if (contains_percent(roots->exe_dir, MAX_PATH))
        return FALSE;
    if (!is_absolute_path(roots->exe_dir))
        return FALSE;
    return TRUE;
}

static BOOL
exe_dir_from_module(const WCHAR *module, WCHAR *out, DWORD cap)
{
    DWORD i;
    DWORD last_sep = (DWORD)-1;
    DWORD len;

    if (!module || !out || cap < 2)
        return FALSE;
    if (contains_percent(module, 32767))
        return FALSE;

    for (i = 0; i < 32767; i++)
    {
        WCHAR ch = module[i];
        if (ch == 0)
            break;
        if (ch == L'\\' || ch == L'/')
            last_sep = i;
    }
    if (i >= 32767 || last_sep == (DWORD)-1 || last_sep == 0)
        return FALSE;

    len = last_sep;
    if (len >= cap)
        return FALSE;
    for (i = 0; i < len; i++)
    {
        WCHAR ch = module[i];
        out[i] = (ch == L'/') ? L'\\' : ch;
    }
    out[len] = 0;
    return is_absolute_path(out);
}

static void
store_shell_folder(vetvdm_path_roots *roots, const WCHAR *folder)
{
    DWORD i;
    DWORD n;
    BOOL terminated = FALSE;

    roots->local_appdata[0] = 0;
    roots->local_appdata_ok = FALSE;
    if (!folder || !folder[0])
        return;
    n = wlen_cap(folder, MAX_PATH, &terminated);
    if (!terminated || n == 0 || n >= MAX_PATH)
        return;
    if (contains_percent(folder, MAX_PATH))
        return;

    for (i = 0; i < n; i++)
        roots->local_appdata[i] = (folder[i] == L'/') ? L'\\' : folder[i];
    roots->local_appdata[n] = 0;

    while (n > 0 && roots->local_appdata[n - 1] == L'\\')
    {
        if (n == 2 && roots->local_appdata[0] == L'\\' && roots->local_appdata[1] == L'\\')
            break;
        roots->local_appdata[--n] = 0;
    }

    if (!is_absolute_path(roots->local_appdata))
    {
        roots->local_appdata[0] = 0;
        return;
    }
    roots->local_appdata_ok = TRUE;
}

DWORD
vetvdm_path_query_roots_ex(
    vetvdm_path_roots *roots,
    vetvdm_shgetfolderpath_fn shget,
    const WCHAR *module_path)
{
    WCHAR env[8];
    WCHAR windir[MAX_PATH];
    WCHAR folder[MAX_PATH];
    WCHAR module[MAX_PATH];
    DWORD n;
    UINT wn;
    HRESULT hr;
    const WCHAR *env_ptr = NULL;
    const WCHAR *win_ptr = NULL;
    const WCHAR *mod_ptr;

    if (!roots)
        return VETVDM_PATH_ERR_INVAL;

    memset(roots, 0, sizeof(*roots));

    /*
     * SystemDrive only. Profile variables are intentionally not queried:
     * this tree has no per-user profile variable for local app data, and
     * the shell folder API works for both profile layouts.
     */
    n = GetEnvironmentVariableW(L"SystemDrive", env, 8);
    if (n > 0 && n < 8)
        env_ptr = env;

    wn = GetSystemWindowsDirectoryW(windir, MAX_PATH);
    if (wn > 0 && wn < MAX_PATH)
    {
        windir[MAX_PATH - 1] = 0;
        win_ptr = windir;
    }

    vetvdm_path_pick_drive(env_ptr, win_ptr, roots->system_drive);

    if (!shget)
        shget = SHGetFolderPathW;

    folder[0] = 0;
    hr = shget(NULL,
               CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE,
               NULL,
               SHGFP_TYPE_CURRENT,
               folder);
    folder[MAX_PATH - 1] = 0;
    if (SUCCEEDED(hr))
        store_shell_folder(roots, folder);

    mod_ptr = module_path;
    if (!mod_ptr)
    {
        n = GetModuleFileNameW(NULL, module, MAX_PATH);
        if (n > 0 && n < MAX_PATH)
        {
            module[MAX_PATH - 1] = 0;
            mod_ptr = module;
        }
    }
    if (mod_ptr && exe_dir_from_module(mod_ptr, roots->exe_dir, MAX_PATH))
        roots->exe_dir_ok = TRUE;

    return VETVDM_PATH_OK;
}

DWORD
vetvdm_path_query_roots(
    vetvdm_path_roots *roots)
{
    return vetvdm_path_query_roots_ex(roots, NULL, NULL);
}

typedef struct kind_info
{
    BOOL user;
    BOOL directory;
    const WCHAR *tail;
} kind_info;

static BOOL
kind_lookup(vetvdm_path_kind kind, kind_info *info)
{
    info->user = FALSE;
    info->directory = FALSE;
    info->tail = L"";

    switch (kind)
    {
    case VETVDM_PATH_SHARED_ROOT:
        info->directory = TRUE;
        return TRUE;
    case VETVDM_PATH_DOSBOXX:
        info->tail = L"dosbox-x\\dosbox-x.exe";
        return TRUE;
    case VETVDM_PATH_PROFILES:
        info->directory = TRUE;
        info->tail = L"profiles";
        return TRUE;
    case VETVDM_PATH_CONF_TEMPLATE:
        info->tail = L"vetvdm\\default.conf";
        return TRUE;
    case VETVDM_PATH_SHARED_LOG:
        info->tail = L"logs\\vetvdm.log";
        return TRUE;
    case VETVDM_PATH_USER_ROOT:
        info->user = TRUE;
        info->directory = TRUE;
        return TRUE;
    case VETVDM_PATH_USER_LOG:
        info->user = TRUE;
        info->tail = L"logs\\instant.log";
        return TRUE;
    default:
        return FALSE;
    }
}

static DWORD
check_suffix(const WCHAR *suffix, BOOL *ends_with_sep)
{
    const WCHAR *cursor;

    *ends_with_sep = FALSE;
    if (!suffix || !suffix[0])
        return VETVDM_PATH_OK;

    if (suffix[0] == L'\\' || suffix[0] == L'/')
        return VETVDM_PATH_ERR_INVAL;

    cursor = suffix;
    while (*cursor)
    {
        const WCHAR *start = cursor;
        DWORD n;

        if (*cursor == L'\\' || *cursor == L'/')
            return VETVDM_PATH_ERR_INVAL;

        while (*cursor && *cursor != L'\\' && *cursor != L'/')
        {
            if (*cursor == L'%')
                return VETVDM_PATH_ERR_PERCENT;
            if (*cursor == L':')
                return VETVDM_PATH_ERR_INVAL;
            cursor++;
        }

        n = (DWORD)(cursor - start);
        if (n == 0)
            return VETVDM_PATH_ERR_INVAL;
        if (n == 1 && start[0] == L'.')
            return VETVDM_PATH_ERR_INVAL;
        if (n == 2 && start[0] == L'.' && start[1] == L'.')
            return VETVDM_PATH_ERR_INVAL;

        if (*cursor == L'\\' || *cursor == L'/')
        {
            cursor++;
            if (*cursor == 0)
                *ends_with_sep = TRUE;
        }
    }
    return VETVDM_PATH_OK;
}

typedef struct path_build
{
    WCHAR data[MAX_PATH];
    DWORD used;
    DWORD needed;
    WCHAR last;
    BOOL overflow;
} path_build;

static void
build_init(path_build *build)
{
    build->data[0] = 0;
    build->used = 0;
    build->needed = 0;
    build->last = 0;
    build->overflow = FALSE;
}

static void
build_char(path_build *build, WCHAR ch)
{
    if (ch == L'/')
        ch = L'\\';
    if (build->needed == 0xFFFFFFFFu)
    {
        build->overflow = TRUE;
        return;
    }
    build->needed++;
    build->last = ch;
    if (build->used + 1 < MAX_PATH)
    {
        build->data[build->used] = ch;
        build->used++;
        build->data[build->used] = 0;
    }
}

static void
build_text(path_build *build, const WCHAR *text)
{
    if (!text)
        return;
    while (*text)
        build_char(build, *text++);
}

static void
build_sep(path_build *build)
{
    if (build->needed == 0 || build->last == L'\\')
        return;
    build_char(build, L'\\');
}

static void
build_text_no_trailing_sep(path_build *build, const WCHAR *text)
{
    const WCHAR *end;
    const WCHAR *cursor;
    DWORD n;

    if (!text || !text[0])
        return;
    n = 0;
    while (text[n])
        n++;
    end = text + n;
    while (end > text && (end[-1] == L'\\' || end[-1] == L'/'))
        end--;
    for (cursor = text; cursor < end; cursor++)
        build_char(build, *cursor);
}

static DWORD
volume_prefix(const WCHAR *path)
{
    DWORD i;

    if (is_drive_letter(path[0]) && path[1] == L':')
        return (path[2] == L'\\') ? 3u : 2u;

    if (path[0] == L'\\' && path[1] == L'\\')
    {
        i = 2;
        while (path[i] && path[i] != L'\\')
            i++;
        if (path[i] == L'\\')
        {
            i++;
            while (path[i] && path[i] != L'\\')
                i++;
            if (path[i] == L'\\')
                i++;
        }
        return i;
    }
    return 0;
}

static BOOL WINAPI
mkdir_real(LPCWSTR path)
{
    DWORD attr;

    if (!path || !path[0] || contains_percent(path, MAX_PATH))
        return FALSE;

    attr = GetFileAttributesW(path);
    if (attr != INVALID_FILE_ATTRIBUTES)
        return (attr & FILE_ATTRIBUTE_DIRECTORY) ? TRUE : FALSE;

    if (CreateDirectoryW(path, NULL))
        return TRUE;

    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        attr = GetFileAttributesW(path);
        return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
    }
    return FALSE;
}

static DWORD
create_dirs(const WCHAR *path, BOOL include_last, vetvdm_path_mkdir_fn mkdir_fn)
{
    WCHAR tmp[MAX_PATH];
    DWORD len;
    DWORD i;
    DWORD start;

    len = 0;
    while (path[len])
        len++;
    if (len == 0 || len >= MAX_PATH)
        return VETVDM_PATH_ERR_RANGE;

    memcpy(tmp, path, (len + 1) * sizeof(WCHAR));
    if (!mkdir_fn)
        mkdir_fn = mkdir_real;

    start = volume_prefix(tmp);
    i = start;
    while (i < len)
    {
        DWORD comp = i;
        BOOL last;
        DWORD k;
        WCHAR save;

        while (i < len && tmp[i] != L'\\')
            i++;
        last = (i >= len);
        if (i == comp)
            return VETVDM_PATH_ERR_INVAL;
        if (last && !include_last)
            break;

        for (k = comp; k < i; k++)
        {
            if (tmp[k] == L'%')
                return VETVDM_PATH_ERR_PERCENT;
        }

        save = tmp[i];
        tmp[i] = 0;
        if (!mkdir_fn(tmp))
        {
            tmp[i] = save;
            return VETVDM_PATH_ERR_CREATE;
        }
        tmp[i] = save;

        if (i < len && tmp[i] == L'\\')
            i++;
    }
    return VETVDM_PATH_OK;
}

static void
set_needed(DWORD *needed, DWORD value)
{
    if (needed)
        *needed = value;
}

static void
set_flag(BOOL *flag, BOOL value)
{
    if (flag)
        *flag = value;
}

BOOL
vetvdm_path_succeeded(DWORD status)
{
    return status == VETVDM_PATH_OK || status == VETVDM_PATH_FALLBACK;
}

DWORD
vetvdm_path_resolve(
    const vetvdm_path_roots *roots,
    vetvdm_path_kind kind,
    const WCHAR *suffix,
    WCHAR *out,
    DWORD cch,
    DWORD *needed,
    BOOL create,
    vetvdm_path_mkdir_fn mkdir_fn,
    BOOL *used_fallback)
{
    kind_info info;
    BOOL ends_with_sep = FALSE;
    BOOL fallback = FALSE;
    DWORD suffix_status;
    DWORD create_status;
    WCHAR drive[4];
    path_build build;
    DWORD required;
    BOOL include_last;

    set_needed(needed, 0);
    set_flag(used_fallback, FALSE);

    if (!roots)
        return VETVDM_PATH_ERR_INVAL;
    if (out == NULL && cch != 0)
        return VETVDM_PATH_ERR_INVAL;
    if (!kind_lookup(kind, &info))
        return VETVDM_PATH_ERR_INVAL;

    suffix_status = check_suffix(suffix, &ends_with_sep);
    if (suffix_status != VETVDM_PATH_OK)
        return suffix_status;
    if (!info.directory && suffix && suffix[0])
        return VETVDM_PATH_ERR_INVAL;

    build_init(&build);

    if (!info.user)
    {
        vetvdm_path_pick_drive(roots->system_drive, NULL, drive);
        build_text(&build, drive);
        build_sep(&build);
        build_text(&build, g_brand);
    }
    else if (shell_root_usable(roots))
    {
        build_text(&build, roots->local_appdata);
        build_sep(&build);
        build_text(&build, g_user_tail);
    }
    else
    {
        fallback = TRUE;
        set_flag(used_fallback, TRUE);
        if (roots->exe_dir[0] && contains_percent(roots->exe_dir, MAX_PATH))
            return VETVDM_PATH_ERR_PERCENT;
        if (!exe_dir_usable(roots))
            return VETVDM_PATH_ERR_NOEXE;
        build_text(&build, roots->exe_dir);
        build_sep(&build);
        build_text(&build, g_fallback_dir);
    }

    if (info.tail[0])
    {
        build_sep(&build);
        build_text(&build, info.tail);
    }
    if (suffix && suffix[0])
    {
        build_sep(&build);
        build_text_no_trailing_sep(&build, suffix);
    }

    if (build.overflow)
    {
        set_needed(needed, 0xFFFFFFFFu);
        return VETVDM_PATH_ERR_RANGE;
    }

    required = build.needed + 1; /* include NUL */
    set_needed(needed, required);

    if (build.needed >= MAX_PATH || build.used != build.needed)
        return VETVDM_PATH_ERR_RANGE;

    if (contains_percent(build.data, MAX_PATH))
        return VETVDM_PATH_ERR_PERCENT;

    if (cch < required || out == NULL)
        return VETVDM_PATH_ERR_NOSPACE;

    if (create)
    {
        include_last = info.directory &&
            (suffix == NULL || suffix[0] == 0 || ends_with_sep);
        create_status = create_dirs(build.data, include_last, mkdir_fn);
        if (create_status != VETVDM_PATH_OK)
        {
            /* The path is resolved; the directory walk is what failed. */
            memcpy(out, build.data, required * sizeof(WCHAR));
            set_flag(used_fallback, fallback);
            return create_status;
        }
    }

    memcpy(out, build.data, required * sizeof(WCHAR));
    set_flag(used_fallback, fallback);
    return fallback ? VETVDM_PATH_FALLBACK : VETVDM_PATH_OK;
}

DWORD
vetvdm_path_resolve_os(
    vetvdm_path_kind kind,
    const WCHAR *suffix,
    WCHAR *out,
    DWORD cch,
    DWORD *needed,
    BOOL create,
    BOOL *used_fallback)
{
    vetvdm_path_roots roots;
    DWORD status;

    status = vetvdm_path_query_roots(&roots);
    if (status != VETVDM_PATH_OK)
        return status;
    return vetvdm_path_resolve(&roots, kind, suffix, out, cch, needed,
                               create, NULL, used_fallback);
}
