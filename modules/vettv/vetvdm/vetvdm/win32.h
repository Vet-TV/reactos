/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/vetvdm/win32.h
 * PURPOSE:  Win32-only helpers for the vetvdm.exe host.
 *
 * Copyright (C) 2026 VetTV
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#ifndef VETVDM_WIN32_H
#define VETVDM_WIN32_H

#include "vetvdm_core.h"

typedef struct vetvdm_reg_ctx
{
    HKEY key;
    const vcore_expand_ctx *expand;
} vetvdm_reg_ctx;

/* Missing key is success with *out == NULL. */
DWORD vetvdm_reg_open(HKEY root, HKEY *out);
void vetvdm_reg_close(HKEY key);
void vetvdm_reg_source(vcore_policy_source *src, vetvdm_reg_ctx *ctx);

typedef struct vcore_job
{
    HANDLE job;
    HANDLE process;
    HANDLE thread;
    DWORD pid;
} vcore_job;

DWORD vcore_launch_in_job(const WCHAR *app, WCHAR *cmdline, const WCHAR *cwd,
                         vcore_job *out);
DWORD vcore_job_wait(vcore_job *job, DWORD *exit_code);
void vcore_job_close(vcore_job *job);

DWORD vcore_tempdir_create(WCHAR out[MAX_PATH]);
DWORD vcore_tempdir_remove(const WCHAR *dir);

DWORD vetvdm_load_image_info(const WCHAR *path, unsigned char **header,
                            DWORD *header_len, char sha_hex[65],
                            DWORD *file_size);

int vetvdm_read_file(void *ctx, const WCHAR *path, char *buf, DWORD cap,
                    DWORD *len);

/* 0 cancel, 1 primary, 2 secondary, 3 no interactive desktop. */
int vetvdm_ask(int win16, int *remember);

#endif /* VETVDM_WIN32_H */
