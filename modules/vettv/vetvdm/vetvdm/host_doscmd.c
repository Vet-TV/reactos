/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/vetvdm/host_doscmd.c
 * PURPOSE:  DOSBox-X command line for vetvdm.exe.
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

WCHAR *
host_command_dosbox(const host *st, const WCHAR *conf_path)
{
    WCHAR *cmd;
    WCHAR quoted[MAX_PATH * 2];
    DWORD used = 0;
    const WCHAR *exe;

    cmd = (WCHAR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                            VETVDM_CMDLINE_CCH * sizeof(WCHAR));
    if (!cmd)
        return NULL;
    exe = st->dosbox[0] ? st->dosbox : L"dosbox-x.exe";
    if (vetvdm_quote_win32(exe, quoted, MAX_PATH * 2) != VETVDM_OK ||
        !wappend(cmd, VETVDM_CMDLINE_CCH, &used, quoted) ||
        !wappend(cmd, VETVDM_CMDLINE_CCH, &used, L" -conf ") ||
        !conf_path ||
        vetvdm_quote_win32(conf_path, quoted, MAX_PATH * 2) != VETVDM_OK ||
        !wappend(cmd, VETVDM_CMDLINE_CCH, &used, quoted) ||
        !wappend(cmd, VETVDM_CMDLINE_CCH, &used,
                L" -exit -nopromptfolder -fastlaunch"))
    {
        HeapFree(GetProcessHeap(), 0, cmd);
        return NULL;
    }
    return cmd;
}
