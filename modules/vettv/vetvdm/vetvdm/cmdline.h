/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/vetvdm/cmdline.h
 * PURPOSE:  vetvdm.exe command line and NT path normalisation.
 *
 * Copyright (C) 2026 VetTV
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#ifndef VETVDM_CMDLINE_H
#define VETVDM_CMDLINE_H

#include "vetvdm_core.h"

#define VETVDM_KIND_AUTO  0
#define VETVDM_KIND_DOS   1
#define VETVDM_KIND_WIN16 2

#define VETVDM_LAYER_NONE    0
#define VETVDM_LAYER_DOSBOXX 1
#define VETVDM_LAYER_WINEVDM 2
#define VETVDM_LAYER_NTVDM   3
#define VETVDM_LAYER_WIN31   4

typedef struct vetvdm_cmdline
{
    DWORD status;
    int from_hook;
    int keep_conf;
    int dry_run;
    int help;
    int version;
    int kind;
    int layer;
    WCHAR image[MAX_PATH];
    WCHAR profile[MAX_PATH];
    WCHAR cwd[MAX_PATH];
    WCHAR log_path[MAX_PATH];
    WCHAR program[MAX_PATH];
    WCHAR *tail;
    WCHAR *args;
    const char *error;
} vetvdm_cmdline;

DWORD vetvdm_normalize_image_path(const WCHAR *in, WCHAR *out, DWORD cch);
DWORD vetvdm_cmdline_parse(const WCHAR *raw, vetvdm_cmdline *out);
void vetvdm_cmdline_free(vetvdm_cmdline *cmd);

const char *vetvdm_kind_name(int kind);
const char *vetvdm_layer_name(int layer);
int vetvdm_layer_from_name(const WCHAR *text);

/* Win32 CreateProcess quoting. out receives a quoted argument. */
DWORD vetvdm_quote_win32(const WCHAR *arg, WCHAR *out, DWORD cch);

#endif /* VETVDM_CMDLINE_H */
