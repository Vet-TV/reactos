/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/vetvdm/route.c
 * PURPOSE:  Routing decision for DOS (MZ/LE/LX) and Win16 (NE).
 *
 * Copyright (C) 2026 VetTV
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * Order: reject PE, then --layer, then PassThrough, then the profile,
 * then policy. Win16Mode=dosboxx means the Win3.1 guest. A profile or
 * --layer value of dosboxx means DOSBox-X itself.
 */

#include "route.h"

#include "vcore_util.h"

#include <string.h>

static int
family_of(const vetvdm_route_input *in)
{
    if (in->image == VETVDM_IMG_PE)
        return -1;
    if (in->kind == VETVDM_KIND_DOS)
    {
        if (in->image == VETVDM_IMG_UNKNOWN ||
            in->image == VETVDM_IMG_DOS_MZ ||
            in->image == VETVDM_IMG_DOS_LE ||
            in->image == VETVDM_IMG_DOS_LX ||
            in->image == VETVDM_IMG_WIN16)
            return VETVDM_KIND_DOS;
        return -1;
    }
    if (in->kind == VETVDM_KIND_WIN16)
    {
        if (in->image == VETVDM_IMG_UNKNOWN)
            return -1;
        return VETVDM_KIND_WIN16;
    }
    if (in->image == VETVDM_IMG_WIN16)
        return VETVDM_KIND_WIN16;
    if (in->image == VETVDM_IMG_DOS_MZ ||
        in->image == VETVDM_IMG_DOS_LE ||
        in->image == VETVDM_IMG_DOS_LX)
        return VETVDM_KIND_DOS;
    return -1;
}

static int
policy_layer(int family, const WCHAR *mode)
{
    if (family == VETVDM_KIND_DOS)
    {
        if (!mode || !mode[0] || vcore_w_ieq(mode, L"dosboxx"))
            return VETVDM_LAYER_DOSBOXX;
        if (vcore_w_ieq(mode, L"ntvdm"))
            return VETVDM_LAYER_NTVDM;
        if (vcore_w_ieq(mode, L"ask"))
            return VETVDM_LAYER_DOSBOXX;
        return VETVDM_LAYER_DOSBOXX;
    }
    if (!mode || !mode[0] || vcore_w_ieq(mode, L"winevdm"))
        return VETVDM_LAYER_WINEVDM;
    /* Spec: Win16Mode=dosboxx is the Win3.1 guest, not a DOSBox DOS run. */
    if (vcore_w_ieq(mode, L"dosboxx") || vcore_w_ieq(mode, L"win31"))
        return VETVDM_LAYER_WIN31;
    if (vcore_w_ieq(mode, L"ask"))
        return VETVDM_LAYER_WINEVDM;
    return VETVDM_LAYER_WINEVDM;
}

static int
wants_ask(int family, const WCHAR *dos_mode, const WCHAR *win16_mode)
{
    const WCHAR *mode = (family == VETVDM_KIND_DOS) ? dos_mode : win16_mode;
    return mode && vcore_w_ieq(mode, L"ask");
}

DWORD
vetvdm_decide_route(const vetvdm_route_input *in, vetvdm_route_result *out)
{
    int family;
    int profile_layer;

    if (!out)
        return VETVDM_E_INTERNAL;
    memset(out, 0, sizeof(*out));
    out->layer = VETVDM_LAYER_NONE;
    out->why = "none";
    if (!in)
    {
        out->error = VETVDM_E_INTERNAL;
        return out->error;
    }

    family = family_of(in);
    if (family < 0)
    {
        out->error = VETVDM_E_NOTDOS;
        out->why = (in->image == VETVDM_IMG_PE) ? "pe" : "garbage";
        return out->error;
    }

    if (in->forced_layer != VETVDM_LAYER_NONE)
    {
        out->layer = in->forced_layer;
        out->why = "forced";
        return VETVDM_OK;
    }
    if (in->passthrough)
    {
        out->layer = VETVDM_LAYER_NTVDM;
        out->why = "passthrough";
        return VETVDM_OK;
    }
    profile_layer = vetvdm_layer_from_name(in->profile_layer);
    if (profile_layer > 0)
    {
        out->layer = profile_layer;
        out->why = "profile";
        return VETVDM_OK;
    }
    if (wants_ask(family, in->dos_mode, in->win16_mode))
    {
        out->ask = 1;
        out->layer = policy_layer(family,
                                 family == VETVDM_KIND_DOS ? in->dos_mode
                                                           : in->win16_mode);
        out->why = "ask";
        return VETVDM_OK;
    }
    out->layer = policy_layer(family,
                             family == VETVDM_KIND_DOS ? in->dos_mode
                                                       : in->win16_mode);
    out->why = (in->dos_mode || in->win16_mode) ? "policy" : "default";
    return VETVDM_OK;
}

const char *
vetvdm_wine_lookup_step(int configured_ok, int bundled_ok,
                       int same_path, int programfiles_ok)
{
    if (configured_ok)
        return "winevdm-path";
    if (bundled_ok && !same_path)
        return "bundled";
    if (programfiles_ok)
        return "programfiles";
    return "missing";
}

int
vetvdm_layer_for_launch(int desired, int forced,
                       int dosbox_ok, int wine_ok, int win31_ok,
                       DWORD *error)
{
    int layer = desired;
    if (error)
        *error = VETVDM_OK;
    if (layer == VETVDM_LAYER_WIN31 && !win31_ok)
    {
        if (forced)
        {
            if (error)
                *error = VETVDM_E_WIN31;
            return layer;
        }
        layer = VETVDM_LAYER_WINEVDM;
    }
    if (layer == VETVDM_LAYER_DOSBOXX && !dosbox_ok)
    {
        if (forced)
        {
            if (error)
                *error = VETVDM_E_NOLAYER;
            return layer;
        }
        layer = VETVDM_LAYER_NTVDM;
    }
    if (layer == VETVDM_LAYER_WINEVDM && !wine_ok)
    {
        if (forced)
        {
            if (error)
                *error = VETVDM_E_NOLAYER;
            return layer;
        }
        layer = VETVDM_LAYER_NTVDM;
    }
    return layer;
}
