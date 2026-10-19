/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/vetvdm/image.c
 * PURPOSE:  Classify MZ, NE, LE, LX, and PE images from a byte sample.
 *
 * Copyright (C) 2026 VetTV
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * The sample must include the new-header bytes at e_lfanew when that
 * pointer falls inside the file. A pointer past the sample is plain DOS.
 */

#include "image.h"

#include "cmdline.h"
#include "vcore_util.h"

static unsigned long
ru32(const unsigned char *p)
{
    return (unsigned long)p[0] |
           ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) |
           ((unsigned long)p[3] << 24);
}

int
vetvdm_classify_image(const unsigned char *bytes, size_t len)
{
    unsigned long lfanew;

    if (!bytes || len < 2)
        return VETVDM_IMG_UNKNOWN;
    if (!((bytes[0] == 'M' && bytes[1] == 'Z') ||
          (bytes[0] == 'Z' && bytes[1] == 'M')))
        return VETVDM_IMG_UNKNOWN;
    if (len < 0x40)
        return VETVDM_IMG_DOS_MZ;
    lfanew = ru32(bytes + 0x3C);
    if (lfanew < 0x40 || lfanew > 0x100000ul)
        return VETVDM_IMG_DOS_MZ;
    if ((size_t)lfanew + 2 > len)
        return VETVDM_IMG_DOS_MZ;
    if ((size_t)lfanew + 4 <= len &&
        bytes[lfanew] == 'P' && bytes[lfanew + 1] == 'E' &&
        bytes[lfanew + 2] == 0 && bytes[lfanew + 3] == 0)
        return VETVDM_IMG_PE;
    if (bytes[lfanew] == 'N' && bytes[lfanew + 1] == 'E')
        return VETVDM_IMG_WIN16;
    if (bytes[lfanew] == 'L' && bytes[lfanew + 1] == 'E')
        return VETVDM_IMG_DOS_LE;
    if (bytes[lfanew] == 'L' && bytes[lfanew + 1] == 'X')
        return VETVDM_IMG_DOS_LX;
    return VETVDM_IMG_DOS_MZ;
}

const char *
vetvdm_image_class_name(int cls)
{
    switch (cls)
    {
    case VETVDM_IMG_DOS_MZ:
        return "mz";
    case VETVDM_IMG_DOS_LE:
        return "le";
    case VETVDM_IMG_DOS_LX:
        return "lx";
    case VETVDM_IMG_WIN16:
        return "ne";
    case VETVDM_IMG_PE:
        return "pe";
    default:
        return "unknown";
    }
}

static const WCHAR *
extension_of(const WCHAR *path)
{
    const WCHAR *dot = NULL;
    const WCHAR *cursor;
    if (!path)
        return NULL;
    for (cursor = path; *cursor; cursor++)
    {
        if (*cursor == L'\\' || *cursor == L'/')
            dot = NULL;
        else if (*cursor == L'.')
            dot = cursor;
    }
    return dot;
}

int
vetvdm_kind_for_image(int kind, int cls, const WCHAR *path)
{
    const WCHAR *ext;
    if (kind != VETVDM_KIND_AUTO)
        return kind;
    if (cls != VETVDM_IMG_UNKNOWN)
        return VETVDM_KIND_AUTO;
    ext = extension_of(path);
    if (ext && vcore_w_ieq(ext, L".com"))
        return VETVDM_KIND_DOS;
    return VETVDM_KIND_AUTO;
}
