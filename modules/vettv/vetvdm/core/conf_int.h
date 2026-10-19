/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/core/conf_int.h
 * PURPOSE:  Private DOSBox-X conf document.
 *
 * Copyright (C) 2026 VetTV
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#ifndef VETVDM_CONF_INT_H
#define VETVDM_CONF_INT_H

#include "vetvdm_core.h"

#define SEC_MAX 20
#define KEY_MAX 24
#define NAME_MAX 32
#define KEYN_MAX 48
#define VAL_MAX 800
#define LINE_MAX 24
#define LINE_LEN 1024

typedef struct conf_section
{
    char name[NAME_MAX];
    char key[KEY_MAX][KEYN_MAX];
    char val[KEY_MAX][VAL_MAX];
    int nkeys;
} conf_section;

typedef struct conf_doc
{
    conf_section sec[SEC_MAX];
    int nsec;
    char line[LINE_MAX][LINE_LEN];
    int nlines;
    unsigned dropped_autoexec;
    DWORD status;
} conf_doc;

void emit_doc(const conf_doc *doc, vcore_buf *out);

#endif /* VETVDM_CONF_INT_H */
