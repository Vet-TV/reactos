/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/core/json.c
 * PURPOSE:  JSON subset reader (objects, arrays, strings, numbers, bools).
 *
 * Copyright (C) 2026 VetTV
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * Written for vetvdm instead of vendoring a tokenizer. The grammar is the
 * JSON profile subset in VETVDM_SPEC section 4.
 */

#include "json.h"

#include <stdlib.h>
#include <string.h>

#define JSON_MAX_DEPTH 12
#define JSON_MAX_ITEMS 64
#define JSON_MAX_STRING 4096

typedef struct json_parser
{
    const char *p;
    const char *end;
    int oom;
    int depth;
} json_parser;

static void
skip_ws(json_parser *ps)
{
    while (ps->p < ps->end)
    {
        char ch = *ps->p;
        if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n')
            break;
        ps->p++;
    }
}

static json_val *
new_val(json_parser *ps, json_type type)
{
    json_val *val = (json_val *)calloc(1, sizeof(*val));
    if (!val)
    {
        ps->oom = 1;
        return NULL;
    }
    val->type = type;
    return val;
}

static int
hex_nibble(char ch)
{
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    return -1;
}

static int
append_utf8(char *dst, unsigned *used, unsigned cap, unsigned cp)
{
    char enc[3];
    int nenc;
    int i;

    if (cp < 0x80)
    {
        enc[0] = (char)cp;
        nenc = 1;
    }
    else if (cp < 0x800)
    {
        enc[0] = (char)(0xC0 | (cp >> 6));
        enc[1] = (char)(0x80 | (cp & 0x3F));
        nenc = 2;
    }
    else
    {
        enc[0] = (char)(0xE0 | (cp >> 12));
        enc[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        enc[2] = (char)(0x80 | (cp & 0x3F));
        nenc = 3;
    }
    if (*used + (unsigned)nenc >= cap)
        return 0;
    for (i = 0; i < nenc; i++)
        dst[(*used)++] = enc[i];
    dst[*used] = 0;
    return 1;
}

static char *
parse_string(json_parser *ps)
{
    char *buf;
    unsigned used = 0;

    if (ps->p >= ps->end || *ps->p != '"')
        return NULL;
    ps->p++;
    buf = (char *)malloc(JSON_MAX_STRING);
    if (!buf)
    {
        ps->oom = 1;
        return NULL;
    }
    buf[0] = 0;
    while (ps->p < ps->end)
    {
        unsigned char ch = (unsigned char)*ps->p;
        ps->p++;
        if (ch == '"')
        {
            char *shrunk = (char *)realloc(buf, used + 1);
            return shrunk ? shrunk : buf;
        }
        if (ch == '\\')
        {
            unsigned char esc;
            unsigned cp;
            int i;
            if (ps->p >= ps->end)
                break;
            esc = (unsigned char)*ps->p;
            ps->p++;
            if (esc == '"' || esc == '\\' || esc == '/')
                ch = esc;
            else if (esc == 'b')
                ch = '\b';
            else if (esc == 'f')
                ch = '\f';
            else if (esc == 'n')
                ch = '\n';
            else if (esc == 'r')
                ch = '\r';
            else if (esc == 't')
                ch = '\t';
            else if (esc == 'u')
            {
                cp = 0;
                for (i = 0; i < 4; i++)
                {
                    int nib;
                    if (ps->p >= ps->end)
                    {
                        free(buf);
                        return NULL;
                    }
                    nib = hex_nibble(*ps->p);
                    ps->p++;
                    if (nib < 0)
                    {
                        free(buf);
                        return NULL;
                    }
                    cp = (cp << 4) | (unsigned)nib;
                }
                if (!append_utf8(buf, &used, JSON_MAX_STRING, cp))
                {
                    free(buf);
                    return NULL;
                }
                continue;
            }
            else
            {
                free(buf);
                return NULL;
            }
        }
        else if (ch < 0x20)
        {
            free(buf);
            return NULL;
        }
        if (used + 1 >= JSON_MAX_STRING)
        {
            free(buf);
            return NULL;
        }
        buf[used++] = (char)ch;
        buf[used] = 0;
    }
    free(buf);
    return NULL;
}

static json_val *parse_value(json_parser *ps);

static json_val *
parse_array(json_parser *ps)
{
    json_val *arr = new_val(ps, JSON_ARR);
    if (!arr)
        return NULL;
    ps->p++; /* [ */
    skip_ws(ps);
    if (ps->p < ps->end && *ps->p == ']')
    {
        ps->p++;
        return arr;
    }
    while (ps->p < ps->end)
    {
        json_val *item;
        json_val **grown;
        if (arr->nitems >= JSON_MAX_ITEMS)
        {
            json_free(arr);
            return NULL;
        }
        item = parse_value(ps);
        if (!item)
        {
            json_free(arr);
            return NULL;
        }
        grown = (json_val **)realloc(arr->items,
                                    (size_t)(arr->nitems + 1) * sizeof(*grown));
        if (!grown)
        {
            ps->oom = 1;
            json_free(item);
            json_free(arr);
            return NULL;
        }
        arr->items = grown;
        arr->items[arr->nitems++] = item;
        skip_ws(ps);
        if (ps->p < ps->end && *ps->p == ',')
        {
            ps->p++;
            skip_ws(ps);
            continue;
        }
        if (ps->p < ps->end && *ps->p == ']')
        {
            ps->p++;
            return arr;
        }
        break;
    }
    json_free(arr);
    return NULL;
}

static json_val *
parse_object(json_parser *ps)
{
    json_val *obj = new_val(ps, JSON_OBJ);
    if (!obj)
        return NULL;
    ps->p++; /* { */
    skip_ws(ps);
    if (ps->p < ps->end && *ps->p == '}')
    {
        ps->p++;
        return obj;
    }
    while (ps->p < ps->end)
    {
        char *key;
        json_val *item;
        char **keys;
        json_val **vals;
        if (obj->nobj >= JSON_MAX_ITEMS)
        {
            json_free(obj);
            return NULL;
        }
        skip_ws(ps);
        key = parse_string(ps);
        if (!key)
        {
            json_free(obj);
            return NULL;
        }
        skip_ws(ps);
        if (ps->p >= ps->end || *ps->p != ':')
        {
            free(key);
            json_free(obj);
            return NULL;
        }
        ps->p++;
        item = parse_value(ps);
        if (!item)
        {
            free(key);
            json_free(obj);
            return NULL;
        }
        keys = (char **)realloc(obj->keys, (size_t)(obj->nobj + 1) * sizeof(*keys));
        if (!keys)
        {
            ps->oom = 1;
            free(key);
            json_free(item);
            json_free(obj);
            return NULL;
        }
        obj->keys = keys;
        vals = (json_val **)realloc(obj->vals, (size_t)(obj->nobj + 1) * sizeof(*vals));
        if (!vals)
        {
            ps->oom = 1;
            free(key);
            json_free(item);
            json_free(obj);
            return NULL;
        }
        obj->vals = vals;
        obj->keys[obj->nobj] = key;
        obj->vals[obj->nobj] = item;
        obj->nobj++;
        skip_ws(ps);
        if (ps->p < ps->end && *ps->p == ',')
        {
            ps->p++;
            continue;
        }
        if (ps->p < ps->end && *ps->p == '}')
        {
            ps->p++;
            return obj;
        }
        break;
    }
    json_free(obj);
    return NULL;
}

static json_val *
parse_number(json_parser *ps)
{
    const char *start = ps->p;
    json_val *val;
    size_t n;
    if (*ps->p == '-')
        ps->p++;
    if (ps->p >= ps->end || *ps->p < '0' || *ps->p > '9')
        return NULL;
    if (*ps->p == '0')
        ps->p++;
    else
    {
        while (ps->p < ps->end && *ps->p >= '0' && *ps->p <= '9')
            ps->p++;
    }
    if (ps->p < ps->end && *ps->p == '.')
    {
        ps->p++;
        if (ps->p >= ps->end || *ps->p < '0' || *ps->p > '9')
            return NULL;
        while (ps->p < ps->end && *ps->p >= '0' && *ps->p <= '9')
            ps->p++;
    }
    n = (size_t)(ps->p - start);
    if (n == 0 || n > 32)
        return NULL;
    val = new_val(ps, JSON_NUM);
    if (!val)
        return NULL;
    val->text = (char *)malloc(n + 1);
    if (!val->text)
    {
        ps->oom = 1;
        free(val);
        return NULL;
    }
    memcpy(val->text, start, n);
    val->text[n] = 0;
    return val;
}

static int
starts(json_parser *ps, const char *lit)
{
    size_t n = strlen(lit);
    if ((size_t)(ps->end - ps->p) < n)
        return 0;
    if (memcmp(ps->p, lit, n) != 0)
        return 0;
    ps->p += n;
    return 1;
}

static json_val *
parse_value(json_parser *ps)
{
    json_val *val;

    skip_ws(ps);
    if (ps->p >= ps->end)
        return NULL;
    if (ps->depth >= JSON_MAX_DEPTH)
        return NULL;
    if (*ps->p == '"')
    {
        char *text = parse_string(ps);
        if (!text)
            return NULL;
        val = new_val(ps, JSON_STR);
        if (!val)
        {
            free(text);
            return NULL;
        }
        val->text = text;
        return val;
    }
    if (*ps->p == '{')
    {
        ps->depth++;
        val = parse_object(ps);
        ps->depth--;
        return val;
    }
    if (*ps->p == '[')
    {
        ps->depth++;
        val = parse_array(ps);
        ps->depth--;
        return val;
    }
    if (*ps->p == 't')
    {
        if (!starts(ps, "true"))
            return NULL;
        val = new_val(ps, JSON_BOOL);
        if (!val)
            return NULL;
        val->bool_val = 1;
        return val;
    }
    if (*ps->p == 'f')
    {
        if (!starts(ps, "false"))
            return NULL;
        val = new_val(ps, JSON_BOOL);
        if (!val)
            return NULL;
        val->bool_val = 0;
        return val;
    }
    if (*ps->p == 'n')
    {
        if (!starts(ps, "null"))
            return NULL;
        return new_val(ps, JSON_NULL);
    }
    if (*ps->p == '-' || (*ps->p >= '0' && *ps->p <= '9'))
        return parse_number(ps);
    return NULL;
}

json_val *
json_parse(const char *text, unsigned len, int *oom)
{
    json_parser ps;
    json_val *val;

    if (oom)
        *oom = 0;
    if (!text)
        return NULL;
    if (len >= 3 &&
        (unsigned char)text[0] == 0xEF &&
        (unsigned char)text[1] == 0xBB &&
        (unsigned char)text[2] == 0xBF)
    {
        text += 3;
        len -= 3;
    }
    ps.p = text;
    ps.end = text + len;
    ps.oom = 0;
    ps.depth = 0;
    val = parse_value(&ps);
    if (ps.oom)
    {
        json_free(val);
        if (oom)
            *oom = 1;
        return NULL;
    }
    if (!val)
        return NULL;
    skip_ws(&ps);
    if (ps.p != ps.end)
    {
        json_free(val);
        return NULL;
    }
    return val;
}

void
json_free(json_val *val)
{
    int i;
    if (!val)
        return;
    free(val->text);
    for (i = 0; i < val->nitems; i++)
        json_free(val->items[i]);
    free(val->items);
    for (i = 0; i < val->nobj; i++)
    {
        free(val->keys[i]);
        json_free(val->vals[i]);
    }
    free(val->keys);
    free(val->vals);
    free(val);
}

const json_val *
json_obj_get(const json_val *obj, const char *key)
{
    int i;
    if (!obj || obj->type != JSON_OBJ || !key)
        return NULL;
    for (i = 0; i < obj->nobj; i++)
    {
        const char *have = obj->keys[i];
        const char *want = key;
        int match = 1;
        if (!have)
            continue;
        while (*have && *want)
        {
            char a = *have;
            char b = *want;
            if (a >= 'A' && a <= 'Z')
                a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z')
                b = (char)(b - 'A' + 'a');
            if (a != b)
            {
                match = 0;
                break;
            }
            have++;
            want++;
        }
        if (match && *have == 0 && *want == 0)
            return obj->vals[i];
    }
    return NULL;
}
