/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/core/tests/test_harness.h
 * PURPOSE:  Shared counters for the vetvdm host tests.
 *
 * Copyright (C) 2026 VetTV
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#ifndef VETVDM_TEST_HARNESS_H
#define VETVDM_TEST_HARNESS_H

#include <stddef.h>

extern int g_failed;
extern int g_checks;

void check(int cond, const char *msg);
void put_u32(unsigned char *b, unsigned long value);
void sample(unsigned char *b, size_t n, int zm, unsigned long lfanew,
           const char *sig, size_t siglen);
int has(const char *hay, const char *needle);

void test_normalize(void);
void test_cmdline(void);
void test_classify_and_route(void);
void test_quote_conf_hash_log(void);
void test_profile_and_policy(void);
void test_policy_merge(void);
void test_goldens(void);

#endif /* VETVDM_TEST_HARNESS_H */
