/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/core/tests/test_vetvdm.c
 * PURPOSE:  Host-side test driver. Not installed on the boot image.
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

static int
run_tests(void)
{
    test_normalize();
    test_cmdline();
    test_classify_and_route();
    test_quote_conf_hash_log();
    test_profile_and_policy();
    test_policy_merge();
    test_goldens();
    if (g_failed)
    {
        fprintf(stderr, "%d failed / %d checks\n", g_failed, g_checks);
        return 1;
    }
    printf("%d checks ok\n", g_checks);
    return 0;
}

#ifdef VETVDM_TEST_NATIVE
int
main(void)
{
    return run_tests();
}
#else
int
wmain(void)
{
    return run_tests();
}
#endif
