/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/vetvdm/image.h
 * PURPOSE:  DOS / Win16 / PE header classification.
 *
 * Copyright (C) 2026 VetTV
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#ifndef VETVDM_IMAGE_H
#define VETVDM_IMAGE_H

#include "vetvdm_core.h"

#define VETVDM_IMG_UNKNOWN 0
#define VETVDM_IMG_DOS_MZ  1
#define VETVDM_IMG_DOS_LE  2
#define VETVDM_IMG_DOS_LX  3
#define VETVDM_IMG_WIN16   4
#define VETVDM_IMG_PE      5

/* Same values as SCS_DOS_BINARY and SCS_WOW_BINARY. */
#define VETVDM_BIN_DOS 1u
#define VETVDM_BIN_WOW 2u

int vetvdm_classify_image(const unsigned char *bytes, size_t len);
const char *vetvdm_image_class_name(int cls);

/* .COM with kind=auto is DOS. Anything else stays as requested. */
int vetvdm_kind_for_image(int kind, int cls, const WCHAR *path);

/*
 * GetBinaryTypeW result applied on top of the header class.
 * SCS_WOW_BINARY is Win16 even when --kind=dos was passed.
 * SCS_DOS_BINARY on an NE header stays DOS (Borland exetyp 5).
 * This function does not read an NE header.
 * have_type is 0 when GetBinaryTypeW was not called or failed.
 * *out_cls receives the class to route. Returns the kind.
 */
int vetvdm_resolve_kind(int requested, int cls, const WCHAR *path,
                       int have_type, unsigned long binary_type,
                       int *out_cls);

#endif /* VETVDM_IMAGE_H */
