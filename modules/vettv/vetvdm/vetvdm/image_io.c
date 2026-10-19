/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/vetvdm/image_io.c
 * PURPOSE:  Read an image header, hash the file, and load profile text.
 *
 * Copyright (C) 2026 VetTV
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "win32.h"

#include <stdlib.h>

int
vetvdm_read_file(void *ctx, const WCHAR *path, char *buf, DWORD cap, DWORD *len)
{
    HANDLE file;
    DWORD size;
    DWORD high = 0;
    DWORD got = 0;
    (void)ctx;

    if (len)
        *len = 0;
    if (!path || !buf || cap < 2)
        return -1;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
            return 0;
        return -1;
    }
    size = GetFileSize(file, &high);
    if (size == INVALID_FILE_SIZE && GetLastError() != NO_ERROR)
    {
        CloseHandle(file);
        return -1;
    }
    if (high != 0 || size >= cap)
    {
        CloseHandle(file);
        return -1;
    }
    if (!ReadFile(file, buf, size, &got, NULL) || got != size)
    {
        CloseHandle(file);
        return -1;
    }
    CloseHandle(file);
    buf[got] = 0;
    if (len)
        *len = got;
    return 1;
}

DWORD
vetvdm_load_image_info(const WCHAR *path, unsigned char **header,
                      DWORD *header_len, char sha_hex[65], DWORD *file_size)
{
    HANDLE file;
    vetvdm_sha256_ctx sha;
    unsigned char chunk[8192];
    unsigned char *keep = NULL;
    DWORD kept = 0;
    DWORD keep_cap = 64;
    DWORD keep_limit = 64;
    DWORD total = 0;
    int saturated = 0;
    unsigned char digest[32];
    DWORD got;
    unsigned long lfanew = 0;
    int saw_lfanew = 0;

    if (header)
        *header = NULL;
    if (header_len)
        *header_len = 0;
    if (sha_hex)
        sha_hex[0] = 0;
    if (file_size)
        *file_size = 0;
    if (!path || !header || !header_len || !sha_hex)
        return VETVDM_E_INTERNAL;

    file = CreateFileW(path, GENERIC_READ,
                      FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return VETVDM_E_NOPROG;

    vetvdm_sha256_init(&sha);
    keep = (unsigned char *)malloc(keep_cap);
    if (!keep)
    {
        CloseHandle(file);
        return VETVDM_E_INTERNAL;
    }

    for (;;)
    {
        DWORD i;
        if (!ReadFile(file, chunk, sizeof(chunk), &got, NULL))
        {
            free(keep);
            CloseHandle(file);
            return VETVDM_E_NOPROG;
        }
        if (got == 0)
            break;
        vetvdm_sha256_update(&sha, chunk, got);
        if (!saturated)
        {
            if (total + got < total)
            {
                total = 0xFFFFFFFFu;
                saturated = 1;
            }
            else
                total += got;
        }
        for (i = 0; i < got && kept < keep_limit && kept < 0x100000u; i++)
        {
            if (kept + 1 > keep_cap)
            {
                DWORD cap = keep_cap * 2;
                unsigned char *grown;
                if (cap < keep_limit)
                    cap = keep_limit;
                if (cap > 0x100000u)
                    cap = 0x100000u;
                grown = (unsigned char *)realloc(keep, cap);
                if (!grown)
                {
                    free(keep);
                    CloseHandle(file);
                    return VETVDM_E_INTERNAL;
                }
                keep = grown;
                keep_cap = cap;
            }
            keep[kept++] = chunk[i];
            if (!saw_lfanew && kept >= 0x40)
            {
                if ((keep[0] == 'M' && keep[1] == 'Z') ||
                    (keep[0] == 'Z' && keep[1] == 'M'))
                {
                    lfanew = (unsigned long)keep[0x3C] |
                             ((unsigned long)keep[0x3D] << 8) |
                             ((unsigned long)keep[0x3E] << 16) |
                             ((unsigned long)keep[0x3F] << 24);
                    if (lfanew >= 0x40 && lfanew < 0x100000ul)
                        keep_limit = (DWORD)lfanew + 4;
                }
                saw_lfanew = 1;
            }
        }
    }
    CloseHandle(file);
    vetvdm_sha256_final(&sha, digest);
    vetvdm_sha256_hex(digest, sha_hex);
    *header = keep;
    *header_len = kept;
    if (file_size)
        *file_size = total;
    return VETVDM_OK;
}
