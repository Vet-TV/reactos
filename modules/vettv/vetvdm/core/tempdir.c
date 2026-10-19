/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/core/tempdir.c
 * PURPOSE:  Per-launch %TEMP%\vetvdm\<pid>-<tick> workspace.
 *
 * Copyright (C) 2026 VetTV
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "win32.h"

#include <stdio.h>
#include <wchar.h>

DWORD
vcore_tempdir_create(WCHAR out[MAX_PATH])
{
    WCHAR temp[MAX_PATH];
    WCHAR parent[MAX_PATH];
    DWORD n;
    DWORD pid;
    DWORD tick;

    if (!out)
        return VETVDM_E_INTERNAL;
    out[0] = 0;
    n = GetTempPathW(MAX_PATH, temp);
    if (n == 0 || n >= MAX_PATH)
        return VETVDM_E_INTERNAL;
    if (n + 8 >= MAX_PATH)
        return VETVDM_E_INTERNAL;
    lstrcpyW(parent, temp);
    if (parent[lstrlenW(parent) - 1] != L'\\')
        lstrcatW(parent, L"\\");
    lstrcatW(parent, L"vetvdm");
    if (!CreateDirectoryW(parent, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        return VETVDM_E_INTERNAL;
    pid = GetCurrentProcessId();
    tick = GetTickCount();
    if (_snwprintf(out, MAX_PATH, L"%s\\%lu-%lu", parent,
                  (unsigned long)pid, (unsigned long)tick) < 0)
        return VETVDM_E_INTERNAL;
    out[MAX_PATH - 1] = 0;
    if (!CreateDirectoryW(out, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        return VETVDM_E_INTERNAL;
    return VETVDM_OK;
}

DWORD
vcore_tempdir_remove(const WCHAR *dir)
{
    WCHAR pattern[MAX_PATH];
    WIN32_FIND_DATAW found;
    HANDLE search;
    DWORD n;

    if (!dir || !dir[0])
        return VETVDM_OK;
    n = lstrlenW(dir);
    if (n + 3 >= MAX_PATH)
        return VETVDM_E_INTERNAL;
    lstrcpyW(pattern, dir);
    lstrcatW(pattern, L"\\*");
    search = FindFirstFileW(pattern, &found);
    if (search != INVALID_HANDLE_VALUE)
    {
        do
        {
            WCHAR child[MAX_PATH];
            if (lstrcmpW(found.cFileName, L".") == 0 ||
                lstrcmpW(found.cFileName, L"..") == 0)
                continue;
            if (n + 1 + lstrlenW(found.cFileName) + 1 >= MAX_PATH)
                continue;
            lstrcpyW(child, dir);
            lstrcatW(child, L"\\");
            lstrcatW(child, found.cFileName);
            if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            DeleteFileW(child);
        } while (FindNextFileW(search, &found));
        FindClose(search);
    }
    RemoveDirectoryW(dir);
    return VETVDM_OK;
}
