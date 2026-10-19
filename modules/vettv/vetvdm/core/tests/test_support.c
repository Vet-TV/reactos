/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/core/tests/test_support.c
 * PURPOSE:  Check helper for the vetvdm host tests.
 *
 * Copyright (C) 2026 VetTV
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "test_harness.h"

#include <stdio.h>
#include <string.h>

int g_failed;
int g_checks;

void
check(int cond, const char *msg)
{
    g_checks++;
    if (!cond)
    {
        fprintf(stderr, "FAIL %s\n", msg);
        g_failed++;
    }
}

void
put_u32(unsigned char *b, unsigned long value)
{
    b[0] = (unsigned char)value;
    b[1] = (unsigned char)(value >> 8);
    b[2] = (unsigned char)(value >> 16);
    b[3] = (unsigned char)(value >> 24);
}

void
sample(unsigned char *b, size_t n, int zm, unsigned long lfanew,
      const char *sig, size_t siglen)
{
    size_t i;
    for (i = 0; i < n; i++)
        b[i] = 0;
    b[0] = zm ? 'Z' : 'M';
    b[1] = zm ? 'M' : 'Z';
    if (n >= 0x40)
        put_u32(b + 0x3C, lfanew);
    if (sig && lfanew + siglen <= n)
        memcpy(b + lfanew, sig, siglen);
}

int
has(const char *hay, const char *needle)
{
    return hay && needle && strstr(hay, needle) != NULL;
}
