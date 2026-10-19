/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/core/buf.c
 * PURPOSE:  Growable UTF-8 buffer for conf text, dry-run, and logs.
 *
 * Copyright (C) 2026 VetTV
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "vetvdm_core.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
vcore_buf_init(vcore_buf *buf)
{
    if (!buf)
        return;
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

void
vcore_buf_free(vcore_buf *buf)
{
    if (!buf)
        return;
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

DWORD
vcore_buf_append(vcore_buf *buf, const char *text, size_t len)
{
    size_t need;
    size_t cap;

    if (!buf)
        return VETVDM_E_INTERNAL;
    if (len == 0)
        return VETVDM_OK;
    if (!text)
        return VETVDM_E_INTERNAL;
    if (buf->len + len < buf->len)
        return VETVDM_E_INTERNAL;
    need = buf->len + len + 1;
    if (need > buf->cap)
    {
        char *grown;
        cap = buf->cap ? buf->cap : 256;
        while (cap < need)
        {
            if (cap > ((size_t)-1) / 2)
                return VETVDM_E_INTERNAL;
            cap *= 2;
        }
        grown = (char *)realloc(buf->data, cap);
        if (!grown)
            return VETVDM_E_INTERNAL;
        buf->data = grown;
        buf->cap = cap;
    }
    memcpy(buf->data + buf->len, text, len);
    buf->len += len;
    buf->data[buf->len] = 0;
    return VETVDM_OK;
}

DWORD
vcore_buf_puts(vcore_buf *buf, const char *text)
{
    size_t n = 0;
    if (!text)
        return VETVDM_OK;
    while (text[n])
        n++;
    return vcore_buf_append(buf, text, n);
}

DWORD
vcore_buf_printf(vcore_buf *buf, const char *fmt, ...)
{
    char tmp[2048];
    int wrote;
    va_list ap;

    if (!buf || !fmt)
        return VETVDM_E_INTERNAL;
    va_start(ap, fmt);
#if defined(_MSC_VER) || defined(__MINGW32__)
    wrote = _vsnprintf(tmp, sizeof(tmp), fmt, ap);
#else
    wrote = vsnprintf(tmp, sizeof(tmp), fmt, ap);
#endif
    va_end(ap);
    tmp[sizeof(tmp) - 1] = 0;
    if (wrote < 0 || (size_t)wrote >= sizeof(tmp))
    {
        size_t n = 0;
        while (tmp[n])
            n++;
        return vcore_buf_append(buf, tmp, n);
    }
    return vcore_buf_append(buf, tmp, (size_t)wrote);
}
