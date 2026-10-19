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

int vetvdm_classify_image(const unsigned char *bytes, size_t len);
const char *vetvdm_image_class_name(int cls);

/* .COM with kind=auto is DOS. Anything else stays as requested. */
int vetvdm_kind_for_image(int kind, int cls, const WCHAR *path);

#endif /* VETVDM_IMAGE_H */
