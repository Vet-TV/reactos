/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/vetvdm/host_ntvdm.c
 * PURPOSE:  NTVDM fallback command line for vetvdm.exe.
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
host_command_ntvdm(const host *st)
{
    WCHAR *cmd;
    WCHAR quoted[MAX_PATH * 2];
    DWORD used = 0;
    const WCHAR *app;

    cmd = (WCHAR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                            VETVDM_CMDLINE_CCH * sizeof(WCHAR));
    if (!cmd)
        return NULL;
    if (st->cmd.tail && st->cmd.tail[0])
    {
        if (!wappend(cmd, VETVDM_CMDLINE_CCH, &used, st->cmd.tail))
        {
            HeapFree(GetProcessHeap(), 0, cmd);
            return NULL;
        }
        return cmd;
    }
    app = st->image;
    if (vetvdm_quote_win32(app, quoted, MAX_PATH * 2) != VETVDM_OK ||
        !wappend(cmd, VETVDM_CMDLINE_CCH, &used, quoted))
    {
        HeapFree(GetProcessHeap(), 0, cmd);
        return NULL;
    }
    return cmd;
}
