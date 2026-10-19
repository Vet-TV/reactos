/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/core/conf_emit.c
 * PURPOSE:  Emit a DOSBox-X conf document.
 *
 * Copyright (C) 2026 VetTV
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "conf_int.h"

#include "vcore_util.h"

static const char *g_order[] = {
    "sdl", "dosbox", "render", "cpu", "mixer", "midi", "sblaster", "gus",
    "speaker", "joystick", "serial", "dos", "ipx", "ethernet", "ne2000",
    "log", "ttf", "keyboard", "capture", "vsync", NULL
};

static void
emit_section(vcore_buf *out, const conf_section *sec, int *first)
{
    int order[KEY_MAX];
    int i;
    int n = sec->nkeys;
    if (n <= 0)
        return;
    for (i = 0; i < n; i++)
        order[i] = i;
    for (i = 1; i < n; i++)
    {
        int key = order[i];
        int j = i;
        while (j > 0 &&
               vcore_cstr_icmp(sec->key[order[j - 1]], sec->key[key]) > 0)
        {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = key;
    }
    if (!*first)
        vcore_buf_puts(out, "\n");
    *first = 0;
    vcore_buf_puts(out, "[");
    vcore_buf_puts(out, sec->name);
    vcore_buf_puts(out, "]\n");
    for (i = 0; i < n; i++)
    {
        vcore_buf_puts(out, sec->key[order[i]]);
        vcore_buf_puts(out, "=");
        vcore_buf_puts(out, sec->val[order[i]]);
        vcore_buf_puts(out, "\n");
    }
}

void
emit_doc(const conf_doc *doc, vcore_buf *out)
{
    int emitted[SEC_MAX];
    int i;
    int first = 1;
    int unknown[SEC_MAX];
    int nunknown = 0;

    for (i = 0; i < doc->nsec; i++)
        emitted[i] = 0;
    for (i = 0; g_order[i]; i++)
    {
        int s;
        for (s = 0; s < doc->nsec; s++)
        {
            if (!emitted[s] && vcore_cstr_ieq(doc->sec[s].name, g_order[i]))
            {
                emit_section(out, &doc->sec[s], &first);
                emitted[s] = 1;
            }
        }
    }
    for (i = 0; i < doc->nsec; i++)
    {
        if (!emitted[i] && !vcore_cstr_ieq(doc->sec[i].name, "autoexec"))
            unknown[nunknown++] = i;
    }
    for (i = 1; i < nunknown; i++)
    {
        int key = unknown[i];
        int j = i;
        while (j > 0 &&
               vcore_cstr_icmp(doc->sec[unknown[j - 1]].name, doc->sec[key].name) > 0)
        {
            unknown[j] = unknown[j - 1];
            j--;
        }
        unknown[j] = key;
    }
    for (i = 0; i < nunknown; i++)
        emit_section(out, &doc->sec[unknown[i]], &first);

    if (doc->nlines > 0)
    {
        if (!first)
            vcore_buf_puts(out, "\n");
        vcore_buf_puts(out, "[autoexec]\n");
        for (i = 0; i < doc->nlines; i++)
        {
            vcore_buf_puts(out, doc->line[i]);
            vcore_buf_puts(out, "\n");
        }
    }
}
