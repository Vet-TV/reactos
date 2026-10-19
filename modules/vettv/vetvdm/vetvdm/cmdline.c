/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/vetvdm/cmdline.c
 * PURPOSE:  Parse the kernel32 hook command line.
 *
 * Copyright (C) 2026 VetTV
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "cmdline.h"

#include "vcore_util.h"

#include <stdlib.h>
#include <string.h>

void
vetvdm_cmdline_free(vetvdm_cmdline *cmd)
{
    if (!cmd)
        return;
    free(cmd->tail);
    free(cmd->args);
    cmd->tail = NULL;
    cmd->args = NULL;
}

static WCHAR *
w_dup(const WCHAR *text)
{
    unsigned n;
    WCHAR *copy;
    unsigned i;
    if (!text)
        text = L"";
    n = vcore_w_len(text);
    copy = (WCHAR *)malloc(((size_t)n + 1u) * sizeof(WCHAR));
    if (!copy)
        return NULL;
    for (i = 0; i < n; i++)
        copy[i] = text[i];
    copy[n] = 0;
    return copy;
}

static int
is_blank(WCHAR ch)
{
    return ch == L' ' || ch == L'\t';
}

/*
 * Find the first unquoted " -- " separator. Quote state follows the usual
 * backslash-escaped quote rule so --image="C:\foo -- bar.exe" stays intact.
 * *prefix_end is the start of the whitespace before --. *tail is the first
 * character after the whitespace that follows --.
 */
static int
find_separator(const WCHAR *raw, const WCHAR **prefix_end, const WCHAR **tail)
{
    const WCHAR *s = raw;
    int quote = 0;
    int bcount = 0;

    while (*s)
    {
        if (*s == L'\\')
        {
            bcount++;
            s++;
            continue;
        }
        if (*s == L'"')
        {
            if ((bcount & 1) == 0)
                quote = !quote;
            bcount = 0;
            s++;
            continue;
        }
        bcount = 0;
        if (!quote && is_blank(*s))
        {
            const WCHAR *mark = s;
            const WCHAR *t = s;
            while (is_blank(*t))
                t++;
            if (t[0] == L'-' && t[1] == L'-' &&
                (t[2] == 0 || is_blank(t[2])))
            {
                const WCHAR *after = t + 2;
                while (is_blank(*after))
                    after++;
                *prefix_end = mark;
                *tail = after;
                return 1;
            }
        }
        s++;
    }
    return 0;
}

typedef struct parsed_arg
{
    WCHAR *value;
    const WCHAR *raw;
} parsed_arg;

static void
free_args(parsed_arg *args, int n)
{
    int i;
    for (i = 0; i < n; i++)
        free(args[i].value);
}

static int
push_arg(parsed_arg *args, int *n, int maxn, WCHAR *value, const WCHAR *raw)
{
    if (*n >= maxn)
    {
        free(value);
        return 0;
    }
    args[*n].value = value;
    args[*n].raw = raw;
    (*n)++;
    return 1;
}

/* Copy argv0 with the special CommandLineToArgvW rules. */
static WCHAR *
take_argv0(const WCHAR **ps, const WCHAR *end, const WCHAR **raw_at)
{
    const WCHAR *s = *ps;
    WCHAR *buf;
    unsigned cap;
    unsigned n = 0;

    while (s < end && is_blank(*s))
        s++;
    if (s >= end)
        return NULL;
    *raw_at = s;
    cap = (unsigned)(end - s) + 1u;
    buf = (WCHAR *)malloc((size_t)cap * sizeof(WCHAR));
    if (!buf)
        return NULL;
    if (*s == L'"')
    {
        s++;
        while (s < end && *s != L'"')
            buf[n++] = *s++;
        if (s < end && *s == L'"')
            s++;
    }
    else
    {
        while (s < end && !is_blank(*s))
            buf[n++] = *s++;
    }
    buf[n] = 0;
    *ps = s;
    return buf;
}

static WCHAR *
take_arg(const WCHAR **ps, const WCHAR *end, const WCHAR **raw_at)
{
    const WCHAR *s = *ps;
    WCHAR *buf;
    unsigned cap;
    unsigned n = 0;
    int qcount = 0;
    int bcount = 0;

    while (s < end && is_blank(*s))
        s++;
    if (s >= end)
        return NULL;
    *raw_at = s;
    cap = (unsigned)(end - s) + 1u;
    buf = (WCHAR *)malloc((size_t)cap * sizeof(WCHAR));
    if (!buf)
        return NULL;

    while (s < end)
    {
        if (is_blank(*s) && qcount == 0)
            break;
        if (*s == L'\\')
        {
            buf[n++] = *s++;
            bcount++;
            continue;
        }
        if (*s == L'"')
        {
            if ((bcount & 1) == 0)
            {
                n -= (unsigned)(bcount / 2);
                qcount++;
            }
            else
            {
                n = n - (unsigned)(bcount / 2) - 1u;
                buf[n++] = L'"';
            }
            s++;
            bcount = 0;
            while (s < end && *s == L'"')
            {
                qcount++;
                if (qcount == 3)
                {
                    buf[n++] = L'"';
                    qcount = 0;
                }
                s++;
            }
            if (qcount == 2)
                qcount = 0;
            continue;
        }
        buf[n++] = *s++;
        bcount = 0;
    }
    buf[n] = 0;
    *ps = s;
    return buf;
}

static int
tokenize(const WCHAR *start, const WCHAR *end, parsed_arg *args, int *out_n)
{
    const WCHAR *s = start;
    const WCHAR *raw = start;
    WCHAR *arg;
    int n = 0;

    arg = take_argv0(&s, end, &raw);
    if (!arg)
        return 0;
    if (!push_arg(args, &n, 40, arg, raw))
        return -1;
    for (;;)
    {
        arg = take_arg(&s, end, &raw);
        if (!arg)
            break;
        if (!push_arg(args, &n, 40, arg, raw))
        {
            free_args(args, n);
            return -1;
        }
    }
    *out_n = n;
    return 1;
}

static int
split_eq(WCHAR *token, WCHAR **value)
{
    WCHAR *eq = token;
    while (*eq && *eq != L'=')
        eq++;
    if (*eq != L'=')
    {
        *value = NULL;
        return 0;
    }
    *eq = 0;
    *value = eq + 1;
    return 1;
}

static DWORD
fail(vetvdm_cmdline *out, const char *why)
{
    out->status = VETVDM_E_CMDLINE;
    out->error = why;
    return VETVDM_E_CMDLINE;
}

static int
option_is(const WCHAR *token, const WCHAR *name)
{
    return vcore_w_ieq(token, name);
}

static DWORD
set_kind(vetvdm_cmdline *out, const WCHAR *value)
{
    if (vcore_w_ieq(value, L"dos"))
        out->kind = VETVDM_KIND_DOS;
    else if (vcore_w_ieq(value, L"win16"))
        out->kind = VETVDM_KIND_WIN16;
    else if (vcore_w_ieq(value, L"auto"))
        out->kind = VETVDM_KIND_AUTO;
    else
        return fail(out, "bad kind");
    return VETVDM_OK;
}

static DWORD
set_layer(vetvdm_cmdline *out, const WCHAR *value)
{
    int layer = vetvdm_layer_from_name(value);
    if (layer < 0)
        return fail(out, "bad layer");
    out->layer = layer;
    return VETVDM_OK;
}

static DWORD
store_norm(WCHAR *dest, DWORD cch, const WCHAR *value, vetvdm_cmdline *out,
          const char *why)
{
    DWORD status = vetvdm_normalize_image_path(value, dest, cch);
    if (status != VETVDM_OK)
        return fail(out, why);
    return VETVDM_OK;
}

static void
split_program(const WCHAR *tail, WCHAR *program, DWORD cch, const WCHAR **args_at,
             int *overflow)
{
    const WCHAR *s = tail ? tail : L"";
    unsigned n = 0;
    *overflow = 0;
    while (is_blank(*s))
        s++;
    if (*s == L'"')
    {
        s++;
        while (*s && *s != L'"')
        {
            if (n + 1 < cch)
                program[n++] = *s;
            else
                *overflow = 1;
            s++;
        }
        if (*s == L'"')
            s++;
    }
    else
    {
        while (*s && !is_blank(*s))
        {
            if (n + 1 < cch)
                program[n++] = *s;
            else
                *overflow = 1;
            s++;
        }
    }
    program[n] = 0;
    while (is_blank(*s))
        s++;
    *args_at = s;
}

DWORD
vetvdm_cmdline_parse(const WCHAR *raw, vetvdm_cmdline *out)
{
    const WCHAR *prefix_end;
    const WCHAR *tail_at = NULL;
    int have_sep;
    parsed_arg args[40];
    int nargs = 0;
    int i;
    int saw_image = 0;
    int tok;
    const WCHAR *args_at = L"";
    int overflow = 0;
    DWORD status;

    if (!out)
        return VETVDM_E_INTERNAL;
    memset(out, 0, sizeof(*out));
    out->kind = VETVDM_KIND_AUTO;
    out->layer = VETVDM_LAYER_NONE;
    if (!raw || !raw[0])
        return fail(out, "empty command line");

    have_sep = find_separator(raw, &prefix_end, &tail_at);
    if (!have_sep)
        prefix_end = raw + vcore_w_len(raw);

    tok = tokenize(raw, prefix_end, args, &nargs);
    if (tok <= 0)
    {
        if (tok < 0)
            return fail(out, "out of memory");
        return fail(out, "empty command line");
    }

    for (i = 1; i < nargs; i++)
    {
        WCHAR *token = args[i].value;
        WCHAR *eq = NULL;
        const WCHAR *value = NULL;
        int took_next = 0;

        if (token[0] != L'-')
        {
            if (!have_sep)
            {
                tail_at = args[i].raw;
                break;
            }
            free_args(args, nargs);
            return fail(out, "unexpected argument before --");
        }
        split_eq(token, &eq);
        if (option_is(token, L"--from-hook"))
            out->from_hook = 1;
        else if (option_is(token, L"--keep-conf"))
            out->keep_conf = 1;
        else if (option_is(token, L"--dry-run"))
            out->dry_run = 1;
        else if (option_is(token, L"--help") || option_is(token, L"-h") ||
                 option_is(token, L"/?"))
            out->help = 1;
        else if (option_is(token, L"--version"))
            out->version = 1;
        else if (option_is(token, L"--kind") || option_is(token, L"--layer") ||
                 option_is(token, L"--profile") || option_is(token, L"--image") ||
                 option_is(token, L"--cwd") || option_is(token, L"--log"))
        {
            if (eq)
                value = eq;
            else if (i + 1 < nargs)
            {
                value = args[i + 1].value;
                took_next = 1;
            }
            else
            {
                free_args(args, nargs);
                return fail(out, "option needs a value");
            }
            if (option_is(token, L"--kind"))
                status = set_kind(out, value);
            else if (option_is(token, L"--layer"))
                status = set_layer(out, value);
            else if (option_is(token, L"--profile"))
            {
                if (vcore_w_len(value) >= MAX_PATH)
                    status = fail(out, "profile path too long");
                else
                {
                    vcore_w_copy(out->profile, MAX_PATH, value);
                    status = VETVDM_OK;
                }
            }
            else if (option_is(token, L"--image"))
            {
                status = store_norm(out->image, MAX_PATH, value, out, "bad image path");
                if (status == VETVDM_OK)
                    saw_image = 1;
            }
            else if (option_is(token, L"--cwd"))
                status = store_norm(out->cwd, MAX_PATH, value, out, "bad cwd");
            else
            {
                if (vcore_w_len(value) >= MAX_PATH)
                    status = fail(out, "log path too long");
                else
                {
                    vcore_w_copy(out->log_path, MAX_PATH, value);
                    status = VETVDM_OK;
                }
            }
            if (status != VETVDM_OK)
            {
                free_args(args, nargs);
                return status;
            }
            if (took_next)
                i++;
        }
        else
        {
            free_args(args, nargs);
            return fail(out, "unknown option");
        }
    }
    free_args(args, nargs);

    if (out->help || out->version)
    {
        out->tail = w_dup(L"");
        out->args = w_dup(L"");
        if (!out->tail || !out->args)
            return fail(out, "out of memory");
        out->status = VETVDM_OK;
        return VETVDM_OK;
    }

    if (have_sep || tail_at)
        out->tail = w_dup(tail_at ? tail_at : L"");
    else
        out->tail = w_dup(L"");
    if (!out->tail)
        return fail(out, "out of memory");

    split_program(out->tail, out->program, MAX_PATH, &args_at, &overflow);
    if (overflow)
        return fail(out, "program path too long");
    out->args = w_dup(args_at);
    if (!out->args)
        return fail(out, "out of memory");

    if (!saw_image)
    {
        if (!out->program[0])
            return fail(out, "missing program");
        status = store_norm(out->image, MAX_PATH, out->program, out, "bad image path");
        if (status != VETVDM_OK)
            return status;
    }
    else if (!out->image[0])
        return fail(out, "missing program");

    out->status = VETVDM_OK;
    out->error = NULL;
    return VETVDM_OK;
}
