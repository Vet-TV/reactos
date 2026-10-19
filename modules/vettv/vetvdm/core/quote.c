/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/core/quote.c
 * PURPOSE:  DOS autoexec quoting and the 127-character command tail.
 *
 * Copyright (C) 2026 VetTV
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "vetvdm_core.h"

#include "vcore_util.h"

DWORD
vcore_dos_command(const char *program, const char *args,
                 char *out, size_t cap, int *truncated)
{
    size_t plen;
    size_t alen;
    size_t need;
    int quote;
    size_t i;
    size_t used;
    const char *cursor;

    if (truncated)
        *truncated = 0;
    if (!out || cap == 0)
        return VETVDM_E_INTERNAL;
    out[0] = 0;
    if (!program || !program[0])
        return VETVDM_E_CMDLINE;

    plen = vcore_cstr_len(program);
    quote = 0;
    for (i = 0; i < plen; i++)
    {
        if (program[i] == '"')
            return VETVDM_E_CMDLINE;
        if (program[i] == ' ' || program[i] == '\t')
            quote = 1;
    }

    if (!args)
        args = "";
    while (*args == ' ' || *args == '\t')
        args++;
    alen = vcore_cstr_len(args);
    if (alen > VETVDM_DOS_TAIL_MAX)
    {
        alen = VETVDM_DOS_TAIL_MAX;
        if (truncated)
            *truncated = 1;
    }

    need = plen + (quote ? 2u : 0u) + (alen ? alen + 1u : 0u) + 1u;
    if (need > cap)
        return VETVDM_E_CMDLINE;

    used = 0;
    if (quote)
        out[used++] = '"';
    for (i = 0; i < plen; i++)
        out[used++] = program[i];
    if (quote)
        out[used++] = '"';
    if (alen)
    {
        out[used++] = ' ';
        cursor = args;
        for (i = 0; i < alen; i++)
            out[used++] = cursor[i];
    }
    out[used] = 0;
    return VETVDM_OK;
}
