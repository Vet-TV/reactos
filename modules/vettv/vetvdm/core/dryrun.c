/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/core/dryrun.c
 * PURPOSE:  Stable --dry-run text so QA can diff routing without DOSBox-X.
 *
 * Copyright (C) 2026 VetTV
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "vetvdm_core.h"

static const char *
field(const char *text)
{
    return (text && text[0]) ? text : "-";
}

static int
one_line(const char *text)
{
    const char *cursor;
    if (!text)
        return 1;
    for (cursor = text; *cursor; cursor++)
    {
        if (*cursor == '\n' || *cursor == '\r')
            return 0;
    }
    return 1;
}

DWORD
vcore_dry_run_text(const vcore_dry_input *in, vcore_buf *out)
{
    if (!in || !out)
        return VETVDM_E_INTERNAL;
    vcore_buf_init(out);
    if (!one_line(in->layer) || !one_line(in->kind) || !one_line(in->caller) ||
        !one_line(in->profile_id) || !one_line(in->profile_src) ||
        !one_line(in->image) || !one_line(in->sha256) || !one_line(in->cwd) ||
        !one_line(in->cmdline) || !one_line(in->note))
        return VETVDM_E_CMDLINE;

    if (vcore_buf_printf(out,
                        "layer=%s\nkind=%s\ncaller=%s\nprofile=%s\n"
                        "profile_src=%s\nimage=%s\nsha256=%s\ncwd=%s\n"
                        "cmdline=%s\n",
                        field(in->layer), field(in->kind), field(in->caller),
                        field(in->profile_id), field(in->profile_src),
                        field(in->image), field(in->sha256), field(in->cwd),
                        field(in->cmdline)) != VETVDM_OK)
    {
        vcore_buf_free(out);
        return VETVDM_E_INTERNAL;
    }
    if (in->note && in->note[0])
    {
        if (vcore_buf_printf(out, "note=%s\n", in->note) != VETVDM_OK)
        {
            vcore_buf_free(out);
            return VETVDM_E_INTERNAL;
        }
    }
    if (in->conf && in->conf[0])
    {
        const char *end = in->conf;
        while (*end)
            end++;
        if (vcore_buf_puts(out, "--- conf ---\n") != VETVDM_OK ||
            vcore_buf_puts(out, in->conf) != VETVDM_OK)
        {
            vcore_buf_free(out);
            return VETVDM_E_INTERNAL;
        }
        if (end == in->conf || end[-1] != '\n')
        {
            if (vcore_buf_puts(out, "\n") != VETVDM_OK)
            {
                vcore_buf_free(out);
                return VETVDM_E_INTERNAL;
            }
        }
    }
    return VETVDM_OK;
}
