/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/vetvdm/route.h
 * PURPOSE:  Choose DOSBox-X, WineVDM, Win3.1, or NTVDM.
 *
 * Copyright (C) 2026 VetTV
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#ifndef VETVDM_ROUTE_H
#define VETVDM_ROUTE_H

#include "cmdline.h"
#include "image.h"

typedef struct vetvdm_route_input
{
    int image;
    int kind;
    int forced_layer;
    const WCHAR *dos_mode;
    const WCHAR *win16_mode;
    const WCHAR *profile_layer;
    int passthrough;
} vetvdm_route_input;

typedef struct vetvdm_route_result
{
    int layer;
    DWORD error;
    int ask;
    const char *why;
} vetvdm_route_result;

DWORD vetvdm_decide_route(const vetvdm_route_input *in, vetvdm_route_result *out);

/*
 * Launch-time fallback when the chosen layer's executable is missing.
 * A forced layer does not fall back: it returns 0xE0000004 or 0xE0000006.
 * Dry-run uses vetvdm_decide_route only, so a missing DOSBox-X still prints
 * the DOSBox conf.
 */
int vetvdm_layer_for_launch(int desired, int forced,
                           int dosbox_ok, int wine_ok, int win31_ok,
                           DWORD *error);

#endif /* VETVDM_ROUTE_H */
