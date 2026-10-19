/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/vetvdm_paths.h
 * PURPOSE:  Path resolution for vetvdm shared and per-user files.
 *
 * Copyright (C) 2026 VetTV
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#ifndef VETVDM_PATHS_H
#define VETVDM_PATHS_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Shared files live under <SystemDrive>\VetTV\...
 * The drive comes from GetEnvironmentVariableW(L"SystemDrive"), then from
 * the drive letter of GetSystemWindowsDirectoryW, then from C:.
 *
 * Per-user files live under
 *   SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, ...)
 *   \VetTV\DOS\...
 * If that folder cannot be used, the path falls back to <exe directory>\data
 * and the status is VETVDM_PATH_FALLBACK so the caller can show a notice.
 *
 * Profile environment variables are never read or expanded. A directory
 * whose name contains '%' is never created. Results that do not fit in
 * MAX_PATH are rejected instead of truncated.
 */

#define VETVDM_PATH_OK           0u
#define VETVDM_PATH_FALLBACK     1u
#define VETVDM_PATH_ERR_INVAL    2u
#define VETVDM_PATH_ERR_NOSPACE  3u
#define VETVDM_PATH_ERR_RANGE    4u
#define VETVDM_PATH_ERR_PERCENT  5u
#define VETVDM_PATH_ERR_CREATE   6u
#define VETVDM_PATH_ERR_NOEXE    7u

typedef enum vetvdm_path_kind
{
    VETVDM_PATH_SHARED_ROOT = 0, /* <drive>\VetTV */
    VETVDM_PATH_DOSBOXX,         /* <drive>\VetTV\dosbox-x\dosbox-x.exe */
    VETVDM_PATH_PROFILES,        /* <drive>\VetTV\profiles */
    VETVDM_PATH_CONF_TEMPLATE,   /* <drive>\VetTV\vetvdm\default.conf */
    VETVDM_PATH_SHARED_LOG,      /* <drive>\VetTV\logs\vetvdm.log */
    VETVDM_PATH_USER_ROOT,       /* <local app data>\VetTV\DOS  or  <exe>\data */
    VETVDM_PATH_USER_LOG         /* <user root>\logs\instant.log */
} vetvdm_path_kind;

typedef struct vetvdm_path_roots
{
    WCHAR system_drive[4];         /* L"C:" — letter, colon, NUL */
    WCHAR local_appdata[MAX_PATH]; /* no trailing slash; ignored unless ok */
    BOOL local_appdata_ok;
    WCHAR exe_dir[MAX_PATH];       /* directory of the host exe, no trailing slash */
    BOOL exe_dir_ok;
} vetvdm_path_roots;

/* Same shape as SHGetFolderPathW. A NULL callback means the real export. */
typedef HRESULT (WINAPI *vetvdm_shgetfolderpath_fn)(
    HWND hwndOwner,
    int nFolder,
    HANDLE hToken,
    DWORD dwFlags,
    LPWSTR pszPath);

/* mkdir_fn is CreateDirectoryW without the security-attributes argument.
 * Returning FALSE fails the create. ERROR_ALREADY_EXISTS is handled only
 * by the default implementation; a test double should return TRUE when the
 * directory is already there. NULL means that default. */
typedef BOOL (WINAPI *vetvdm_path_mkdir_fn)(LPCWSTR path);

/*
 * Choose a drive letter from a SystemDrive value and the system Windows
 * directory. Either pointer may be NULL. out is always a letter, a colon,
 * and NUL. The letter is C: when neither input is a usable rooted path.
 * A value that contains '%' is not usable.
 */
void
vetvdm_path_pick_drive(
    const WCHAR *system_drive_env,
    const WCHAR *system_windows_dir,
    WCHAR out[4]);

/* Fill roots from the OS. Always returns OK when roots is non-NULL.
 * Shell failure is recorded as local_appdata_ok == FALSE, not as an error. */
DWORD
vetvdm_path_query_roots(
    vetvdm_path_roots *roots);

/* shget NULL uses SHGetFolderPathW. module_path NULL uses
 * GetModuleFileNameW(NULL). module_path is the full path of the exe. */
DWORD
vetvdm_path_query_roots_ex(
    vetvdm_path_roots *roots,
    vetvdm_shgetfolderpath_fn shget,
    const WCHAR *module_path);

/*
 * Resolve kind, optionally appending a relative suffix.
 * suffix NULL and L"" both mean the kind's own path. A suffix is accepted
 * only for directory kinds (SHARED_ROOT, PROFILES, USER_ROOT). It must be
 * relative: no drive, no leading slash, no '.' or '..' component, no ':'.
 * A '%' anywhere in the suffix fails with VETVDM_PATH_ERR_PERCENT and
 * creates nothing.
 *
 * cch is the capacity of out in WCHARs, including the NUL. out may be NULL
 * only when cch is 0 (a size probe). On OK, FALLBACK, NOSPACE, RANGE, and
 * CREATE, *needed (if not NULL) receives the required capacity including NUL.
 * out is written on OK, FALLBACK, and CREATE. NOSPACE, RANGE, PERCENT,
 * INVAL, and NOEXE leave it unchanged. RANGE means the path does not fit
 * in MAX_PATH even if cch is larger.
 *
 * create == TRUE creates missing directories after the volume prefix.
 * The final component is created only for a directory kind whose suffix is
 * empty or ends in a slash. File kinds create their parent directories only.
 * Nothing is created on NOSPACE, RANGE, PERCENT, INVAL, or NOEXE.
 *
 * used_fallback, when not NULL, is set TRUE once a per-user kind has
 * rejected the shell folder (the data folder may still be unusable).
 * It stays FALSE when the call fails before that choice, for example a
 * bad suffix.
 */
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
    BOOL *used_fallback);

/* query_roots plus resolve. create uses the default directory creator. */
DWORD
vetvdm_path_resolve_os(
    vetvdm_path_kind kind,
    const WCHAR *suffix,
    WCHAR *out,
    DWORD cch,
    DWORD *needed,
    BOOL create,
    BOOL *used_fallback);

/* TRUE for OK and FALLBACK. */
BOOL
vetvdm_path_succeeded(
    DWORD status);

#ifdef __cplusplus
}
#endif

#endif /* VETVDM_PATHS_H */
