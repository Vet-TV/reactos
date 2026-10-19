/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/core/log.c
 * PURPOSE:  One-line launch log format from VETVDM_SPEC section 7.
 *
 * Copyright (C) 2026 VetTV
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * The host appends the line and rotates the file. Retro OS writes
 * <SystemDrive>\VetTV\logs\vetvdm.log via the path library.
 */

#include "vetvdm_core.h"

#include <stdio.h>

static const char *
clean(const char *text, char *tmp, unsigned cap)
{
    unsigned used = 0;
    const char *cursor;

    if (!text || !text[0])
        return "-";
    for (cursor = text; *cursor && used + 1 < cap; cursor++)
    {
        char ch = *cursor;
        if (ch == '\n' || ch == '\r' || ch == '"')
            ch = '\'';
        tmp[used++] = ch;
    }
    tmp[used] = 0;
    return tmp;
}

static DWORD
add_clean(vcore_buf *line_out, const char *text)
{
    char tmp[MAX_PATH * 3];
    return vcore_buf_puts(line_out, clean(text, tmp, sizeof(tmp)));
}

DWORD
vcore_log_format(const vcore_log_rec *rec, vcore_buf *line_out)
{
    char exit_buf[32];

    if (!rec || !line_out)
        return VETVDM_E_INTERNAL;
    vcore_buf_init(line_out);
    if (rec->exit_code > 255u)
    {
#if defined(_MSC_VER) || defined(__MINGW32__)
        _snprintf(exit_buf, sizeof(exit_buf), "0x%08lX", (unsigned long)rec->exit_code);
#else
        snprintf(exit_buf, sizeof(exit_buf), "0x%08lX", (unsigned long)rec->exit_code);
#endif
    }
    else
    {
#if defined(_MSC_VER) || defined(__MINGW32__)
        _snprintf(exit_buf, sizeof(exit_buf), "%lu", (unsigned long)rec->exit_code);
#else
        snprintf(exit_buf, sizeof(exit_buf), "%lu", (unsigned long)rec->exit_code);
#endif
    }
    exit_buf[sizeof(exit_buf) - 1] = 0;

    if (vcore_buf_printf(line_out, "%04d-%02d-%02d %02d:%02d:%02d pid=%lu caller=",
                        rec->year, rec->month, rec->day, rec->hour, rec->min,
                        rec->sec, (unsigned long)rec->pid) != VETVDM_OK ||
        add_clean(line_out, rec->caller) != VETVDM_OK ||
        vcore_buf_puts(line_out, " program=\"") != VETVDM_OK ||
        add_clean(line_out, rec->program) != VETVDM_OK ||
        vcore_buf_puts(line_out, "\" sha256=") != VETVDM_OK ||
        add_clean(line_out, rec->sha256) != VETVDM_OK ||
        vcore_buf_puts(line_out, " kind=") != VETVDM_OK ||
        add_clean(line_out, rec->kind) != VETVDM_OK ||
        vcore_buf_puts(line_out, " layer=") != VETVDM_OK ||
        add_clean(line_out, rec->layer) != VETVDM_OK ||
        vcore_buf_puts(line_out, " profile=") != VETVDM_OK ||
        add_clean(line_out, rec->profile_id) != VETVDM_OK ||
        vcore_buf_printf(line_out, " exit=%s duration_ms=%lu",
                        exit_buf, (unsigned long)rec->duration_ms) != VETVDM_OK)
    {
        vcore_buf_free(line_out);
        return VETVDM_E_INTERNAL;
    }

    /*
     * clean() reuses warn, so the warn field is formatted from the source
     * after the other fields have already been copied into the line.
     */
    if (rec->warn && rec->warn[0])
    {
        char warn_copy[128];
        unsigned i = 0;
        const char *cursor = rec->warn;
        while (*cursor && i + 1 < sizeof(warn_copy))
        {
            char ch = *cursor++;
            if (ch == ' ' || ch == '\n' || ch == '\r')
                ch = ',';
            warn_copy[i++] = ch;
        }
        warn_copy[i] = 0;
        if (vcore_buf_printf(line_out, " warn=%s", warn_copy) != VETVDM_OK)
        {
            vcore_buf_free(line_out);
            return VETVDM_E_INTERNAL;
        }
    }
    if (vcore_buf_puts(line_out, "\n") != VETVDM_OK)
    {
        vcore_buf_free(line_out);
        return VETVDM_E_INTERNAL;
    }
    return VETVDM_OK;
}
