/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/core/json.h
 * PURPOSE:  Small JSON subset reader for vettv profiles.
 *
 * Copyright (C) 2026 VetTV
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#ifndef VETVDM_JSON_H
#define VETVDM_JSON_H

typedef enum json_type
{
    JSON_NULL = 0,
    JSON_BOOL,
    JSON_NUM,
    JSON_STR,
    JSON_ARR,
    JSON_OBJ
} json_type;

typedef struct json_val
{
    json_type type;
    int bool_val;
    char *text;
    struct json_val **items;
    int nitems;
    char **keys;
    struct json_val **vals;
    int nobj;
} json_val;

/* Returns NULL on a syntax error or out of memory. *oom is set when known. */
json_val *json_parse(const char *text, unsigned len, int *oom);
void json_free(json_val *val);
const json_val *json_obj_get(const json_val *obj, const char *key);

#endif /* VETVDM_JSON_H */
