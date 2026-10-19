/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/vetvdm/host_wine.c
 * PURPOSE:  WineVDM command line for vetvdm.exe.
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
host_command_wine(const host *st)
{
    WCHAR *cmd;
    WCHAR quoted[MAX_PATH * 2];
    WCHAR image_q[MAX_PATH * 2];
    DWORD used = 0;
    const WCHAR *exe;

    cmd = (WCHAR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                            VETVDM_CMDLINE_CCH * sizeof(WCHAR));
    if (!cmd)
        return NULL;
    exe = st->wine[0] ? st->wine : L"otvdmw.exe";
    if (vetvdm_quote_win32(exe, quoted, MAX_PATH * 2) != VETVDM_OK ||
        vetvdm_quote_win32(st->image, image_q, MAX_PATH * 2) != VETVDM_OK ||
        !wappend(cmd, VETVDM_CMDLINE_CCH, &used, quoted) ||
        !wappend(cmd, VETVDM_CMDLINE_CCH, &used, L" ") ||
        !wappend(cmd, VETVDM_CMDLINE_CCH, &used, image_q))
    {
        HeapFree(GetProcessHeap(), 0, cmd);
        return NULL;
    }
    if (st->cmd.args && st->cmd.args[0])
    {
        if (!wappend(cmd, VETVDM_CMDLINE_CCH, &used, L" ") ||
            !wappend(cmd, VETVDM_CMDLINE_CCH, &used, st->cmd.args))
        {
            HeapFree(GetProcessHeap(), 0, cmd);
            return NULL;
        }
    }
    return cmd;
}
