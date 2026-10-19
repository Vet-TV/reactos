/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/vetvdm_paths_test.c
 * PURPOSE:  Host-side tests for vetvdm path resolution.
 *
 * Copyright (C) 2026 VetTV
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

/*
 * Covers normal resolution, shell-folder failure fallback, results that
 * must not contain '%', and caller-buffer edges (too small, exact, past
 * MAX_PATH). Directory creation is a test double, so the suite does not
 * write a real profile folder.
 */

#include "vetvdm_paths.h"

#include <shlobj.h>
#include <stdio.h>
#include <string.h>

static int g_checks;
static int g_failed;

static int g_mkdir_calls;
static int g_mkdir_fail_on;
static int g_mkdir_saw_percent;
#define MADE_MAX 12
static WCHAR g_made[MADE_MAX][MAX_PATH];
static int g_made_n;

static int g_sh_calls;
static HWND g_sh_hwnd;
static int g_sh_csidl;
static HANDLE g_sh_token;
static DWORD g_sh_flags;
static HRESULT g_sh_hr;
static const WCHAR *g_sh_path;

static void
expect_true(int cond, const char *msg)
{
    g_checks++;
    if (!cond)
    {
        g_failed++;
        printf("FAIL %s\n", msg);
    }
}

static void
print_w(const WCHAR *text)
{
    DWORD i;

    if (!text)
    {
        printf("(null)");
        return;
    }
    for (i = 0; text[i]; i++)
    {
        unsigned int ch = text[i];
        if (ch >= 32 && ch < 127)
            putchar((int)ch);
        else
            printf("\\u%04x", ch);
    }
}

static int
weq(const WCHAR *left, const WCHAR *right)
{
    if (!left || !right)
        return left == right;
    while (*left && *left == *right)
    {
        left++;
        right++;
    }
    return *left == *right;
}

static DWORD
wlen(const WCHAR *text)
{
    DWORD n = 0;
    while (text && text[n])
        n++;
    return n;
}

static void
wcopy(WCHAR *dst, DWORD cap, const WCHAR *src)
{
    DWORD i = 0;

    if (!dst || cap == 0)
        return;
    if (!src)
    {
        dst[0] = 0;
        return;
    }
    while (src[i] && i + 1 < cap)
    {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static int
has_percent(const WCHAR *text)
{
    DWORD i;
    for (i = 0; text && text[i]; i++)
    {
        if (text[i] == L'%')
            return 1;
    }
    return 0;
}

static void
expect_status(DWORD got, DWORD want, const char *msg)
{
    g_checks++;
    if (got != want)
    {
        g_failed++;
        printf("FAIL %s (got %lu want %lu)\n", msg,
               (unsigned long)got, (unsigned long)want);
    }
}

static void
expect_w(const WCHAR *got, const WCHAR *want, const char *msg)
{
    g_checks++;
    if (!weq(got, want))
    {
        g_failed++;
        printf("FAIL %s\n  got  ", msg);
        print_w(got);
        printf("\n  want ");
        print_w(want);
        printf("\n");
    }
}

static void
expect_no_percent(const WCHAR *text, const char *msg)
{
    g_checks++;
    if (has_percent(text))
    {
        g_failed++;
        printf("FAIL %s contains %%: ", msg);
        print_w(text);
        printf("\n");
    }
}

static void
fill(WCHAR *buf, DWORD n, WCHAR ch)
{
    DWORD i;
    for (i = 0; i < n; i++)
        buf[i] = ch;
}

static int
is_fill(const WCHAR *buf, DWORD n, WCHAR ch)
{
    DWORD i;
    for (i = 0; i < n; i++)
    {
        if (buf[i] != ch)
            return 0;
    }
    return 1;
}

static void
made_reset(void)
{
    g_mkdir_calls = 0;
    g_mkdir_fail_on = 0;
    g_mkdir_saw_percent = 0;
    g_made_n = 0;
}

static BOOL WINAPI
test_mkdir(LPCWSTR path)
{
    DWORD i;

    g_mkdir_calls++;
    if (path)
    {
        for (i = 0; path[i]; i++)
        {
            if (path[i] == L'%')
                g_mkdir_saw_percent = 1;
        }
    }
    if (g_mkdir_fail_on && g_mkdir_calls == g_mkdir_fail_on)
        return FALSE;
    if (path && g_made_n < MADE_MAX)
    {
        wcopy(g_made[g_made_n], MAX_PATH, path);
        g_made_n++;
    }
    return TRUE;
}

static void
set_roots(vetvdm_path_roots *roots,
          const WCHAR *drive,
          const WCHAR *local,
          BOOL local_ok,
          const WCHAR *exe,
          BOOL exe_ok)
{
    memset(roots, 0, sizeof(*roots));
    if (drive)
        wcopy(roots->system_drive, 4, drive);
    if (local)
        wcopy(roots->local_appdata, MAX_PATH, local);
    roots->local_appdata_ok = local_ok;
    if (exe)
        wcopy(roots->exe_dir, MAX_PATH, exe);
    roots->exe_dir_ok = exe_ok;
}

static DWORD
resolve(const vetvdm_path_roots *roots,
        vetvdm_path_kind kind,
        const WCHAR *suffix,
        WCHAR *out,
        DWORD cch,
        DWORD *needed,
        BOOL create,
        BOOL *fallback)
{
    return vetvdm_path_resolve(roots, kind, suffix, out, cch, needed,
                               create, test_mkdir, fallback);
}

static int
is_drive_token(const WCHAR *drive)
{
    if (!drive || !drive[0] || drive[1] != L':' || drive[2] != 0)
        return 0;
    if (drive[0] >= L'A' && drive[0] <= L'Z')
        return 1;
    if (drive[0] >= L'a' && drive[0] <= L'z')
        return 1;
    return 0;
}

static void
test_pick_drive(void)
{
    WCHAR out[4];

    vetvdm_path_pick_drive(L"D:", NULL, out);
    expect_w(out, L"D:", "env drive");

    vetvdm_path_pick_drive(L"D:\\", L"E:\\OS", out);
    expect_w(out, L"D:", "env drive with slash wins");

    vetvdm_path_pick_drive(L"d:/", NULL, out);
    expect_w(out, L"d:", "lowercase drive kept");

    vetvdm_path_pick_drive(NULL, L"E:\\OS", out);
    expect_w(out, L"E:", "windows directory drive");

    vetvdm_path_pick_drive(L"not-a-drive", L"Q:\\OS", out);
    expect_w(out, L"Q:", "bad env uses windows directory");

    vetvdm_path_pick_drive(L"Q:\\extra", NULL, out);
    expect_w(out, L"C:", "env longer than a drive falls back");

    vetvdm_path_pick_drive(NULL, L"Q:relative", out);
    expect_w(out, L"C:", "relative windows directory rejected");

    vetvdm_path_pick_drive(NULL, NULL, out);
    expect_w(out, L"C:", "both missing");

    /* A percent sign means the value is still an unexpanded reference. */
    vetvdm_path_pick_drive(L"D:%", L"E:\\OS", out);
    expect_w(out, L"E:", "percent in env is skipped");

    vetvdm_path_pick_drive(NULL, L"E:\\OS%rest", out);
    expect_w(out, L"C:", "percent in windows directory is skipped");

    vetvdm_path_pick_drive(L"D:", L"E:\\OS%rest", out);
    expect_w(out, L"D:", "clean env wins over a bad windows directory");

    expect_no_percent(out, "picked drive");
}

static void
test_normal_paths(void)
{
    vetvdm_path_roots roots;
    WCHAR buf[MAX_PATH];
    DWORD needed = 0;
    BOOL fallback = TRUE;
    DWORD status;

    set_roots(&roots, L"D:", L"X:\\shell\\local", TRUE, L"E:\\apps", TRUE);
    made_reset();

    status = resolve(&roots, VETVDM_PATH_SHARED_ROOT, NULL, buf, MAX_PATH,
                     &needed, FALSE, &fallback);
    expect_status(status, VETVDM_PATH_OK, "shared root status");
    expect_w(buf, L"D:\\VetTV", "shared root");
    expect_true(needed == 9, "shared root capacity");
    expect_true(fallback == FALSE, "shared root is not a fallback");
    expect_no_percent(buf, "shared root");

    status = resolve(&roots, VETVDM_PATH_DOSBOXX, NULL, buf, MAX_PATH,
                     &needed, FALSE, &fallback);
    expect_status(status, VETVDM_PATH_OK, "dosbox-x status");
    expect_w(buf, L"D:\\VetTV\\dosbox-x\\dosbox-x.exe", "dosbox-x");
    expect_no_percent(buf, "dosbox-x");

    status = resolve(&roots, VETVDM_PATH_PROFILES, NULL, buf, MAX_PATH,
                     &needed, FALSE, &fallback);
    expect_status(status, VETVDM_PATH_OK, "profiles status");
    expect_w(buf, L"D:\\VetTV\\profiles", "profiles");

    status = resolve(&roots, VETVDM_PATH_PROFILES, L"by-name\\GAME.json",
                     buf, MAX_PATH, &needed, FALSE, &fallback);
    expect_status(status, VETVDM_PATH_OK, "profile file status");
    expect_w(buf, L"D:\\VetTV\\profiles\\by-name\\GAME.json", "profile file");
    expect_no_percent(buf, "profile file");

    status = resolve(&roots, VETVDM_PATH_CONF_TEMPLATE, NULL, buf, MAX_PATH,
                     &needed, FALSE, &fallback);
    expect_status(status, VETVDM_PATH_OK, "template status");
    expect_w(buf, L"D:\\VetTV\\vetvdm\\default.conf", "template");

    status = resolve(&roots, VETVDM_PATH_SHARED_LOG, NULL, buf, MAX_PATH,
                     &needed, FALSE, &fallback);
    expect_status(status, VETVDM_PATH_OK, "shared log status");
    expect_w(buf, L"D:\\VetTV\\logs\\vetvdm.log", "shared log");

    status = resolve(&roots, VETVDM_PATH_USER_ROOT, NULL, buf, MAX_PATH,
                     &needed, FALSE, &fallback);
    expect_status(status, VETVDM_PATH_OK, "user root status");
    expect_w(buf, L"X:\\shell\\local\\VetTV\\DOS", "user root");
    expect_true(fallback == FALSE, "shell folder used");
    expect_no_percent(buf, "user root");

    status = resolve(&roots, VETVDM_PATH_USER_LOG, NULL, buf, MAX_PATH,
                     &needed, FALSE, &fallback);
    expect_status(status, VETVDM_PATH_OK, "user log status");
    expect_w(buf, L"X:\\shell\\local\\VetTV\\DOS\\logs\\instant.log", "user log");
    expect_no_percent(buf, "user log");

    status = resolve(&roots, VETVDM_PATH_USER_ROOT, L"saves/slot1",
                     buf, MAX_PATH, &needed, FALSE, &fallback);
    expect_status(status, VETVDM_PATH_OK, "slash suffix status");
    expect_w(buf, L"X:\\shell\\local\\VetTV\\DOS\\saves\\slot1", "slash suffix");

    status = resolve(&roots, VETVDM_PATH_SHARED_ROOT, L"", buf, MAX_PATH,
                     &needed, FALSE, &fallback);
    expect_w(buf, L"D:\\VetTV", "empty suffix");

    status = resolve(&roots, VETVDM_PATH_DOSBOXX, L"extra", buf, MAX_PATH,
                     &needed, FALSE, &fallback);
    expect_status(status, VETVDM_PATH_ERR_INVAL, "suffix on a file kind");

    set_roots(&roots, L"d:", NULL, FALSE, NULL, FALSE);
    status = resolve(&roots, VETVDM_PATH_SHARED_ROOT, NULL, buf, MAX_PATH,
                     &needed, FALSE, &fallback);
    expect_w(buf, L"d:\\VetTV", "lowercase drive preserved");

    set_roots(&roots, L"Q:\\", NULL, FALSE, NULL, FALSE);
    status = resolve(&roots, VETVDM_PATH_SHARED_ROOT, NULL, buf, MAX_PATH,
                     &needed, FALSE, &fallback);
    expect_w(buf, L"Q:\\VetTV", "drive token with slash");

    set_roots(&roots, L"", NULL, FALSE, NULL, FALSE);
    status = resolve(&roots, VETVDM_PATH_SHARED_ROOT, NULL, buf, MAX_PATH,
                     &needed, FALSE, &fallback);
    expect_status(status, VETVDM_PATH_OK, "missing drive status");
    expect_w(buf, L"C:\\VetTV", "missing drive falls back");
    expect_no_percent(buf, "fallback drive");

    set_roots(&roots, L"1:", NULL, FALSE, NULL, FALSE);
    status = resolve(&roots, VETVDM_PATH_SHARED_ROOT, NULL, buf, MAX_PATH,
                     &needed, FALSE, &fallback);
    expect_w(buf, L"C:\\VetTV", "unusable drive token");

    set_roots(&roots, L"D:", L"//server/share/local", TRUE, L"E:\\apps", TRUE);
    status = resolve(&roots, VETVDM_PATH_USER_ROOT, NULL, buf, MAX_PATH,
                     &needed, FALSE, &fallback);
    expect_status(status, VETVDM_PATH_OK, "unc shell status");
    expect_w(buf, L"\\\\server\\share\\local\\VetTV\\DOS", "unc shell folder");
    expect_no_percent(buf, "unc shell folder");

    expect_true(g_mkdir_calls == 0, "resolve without create does not mkdir");
}

static void
test_shell_failure_fallback(void)
{
    vetvdm_path_roots roots;
    WCHAR buf[MAX_PATH];
    WCHAR dirty[MAX_PATH];
    DWORD needed = 99;
    BOOL fallback = FALSE;
    DWORD status;

    made_reset();
    set_roots(&roots, L"D:", NULL, FALSE, L"E:\\apps", TRUE);

    fill(dirty, MAX_PATH, L'#');
    memcpy(buf, dirty, sizeof(buf));
    status = resolve(&roots, VETVDM_PATH_USER_ROOT, NULL, buf, MAX_PATH,
                     &needed, FALSE, &fallback);
    expect_status(status, VETVDM_PATH_FALLBACK, "shell miss status");
    expect_w(buf, L"E:\\apps\\data", "shell miss path");
    expect_true(fallback == TRUE, "shell miss flag");
    expect_no_percent(buf, "shell miss path");
    expect_true(vetvdm_path_succeeded(status), "fallback counts as success");

    status = resolve(&roots, VETVDM_PATH_USER_LOG, NULL, buf, MAX_PATH,
                     &needed, FALSE, &fallback);
    expect_status(status, VETVDM_PATH_FALLBACK, "shell miss log status");
    expect_w(buf, L"E:\\apps\\data\\logs\\instant.log", "shell miss log");
    expect_no_percent(buf, "shell miss log");

    status = resolve(&roots, VETVDM_PATH_SHARED_LOG, NULL, buf, MAX_PATH,
                     &needed, FALSE, &fallback);
    expect_status(status, VETVDM_PATH_OK, "shared path ignores shell miss");
    expect_w(buf, L"D:\\VetTV\\logs\\vetvdm.log", "shared log while shell is down");
    expect_true(fallback == FALSE, "shared flag stays clear");

    /* A shell result that still contains an unexpanded reference is a miss. */
    set_roots(&roots, L"D:", L"X:\\bad%name\\local", TRUE, L"E:\\apps", TRUE);
    status = resolve(&roots, VETVDM_PATH_USER_ROOT, NULL, buf, MAX_PATH,
                     &needed, TRUE, &fallback);
    expect_status(status, VETVDM_PATH_FALLBACK, "percent shell folder falls back");
    expect_w(buf, L"E:\\apps\\data", "percent shell folder path");
    expect_no_percent(buf, "fallback after percent shell folder");
    expect_true(g_mkdir_saw_percent == 0, "percent shell folder created nothing with %");
    expect_true(g_made_n == 2, "fallback create count");
    expect_w(g_made[0], L"E:\\apps", "fallback parent");
    expect_w(g_made[1], L"E:\\apps\\data", "fallback data dir");

    /* Relative shell results are not usable either. */
    made_reset();
    set_roots(&roots, L"D:", L"shell\\local", TRUE, L"E:\\apps", TRUE);
    status = resolve(&roots, VETVDM_PATH_USER_ROOT, NULL, buf, MAX_PATH,
                     &needed, FALSE, &fallback);
    expect_status(status, VETVDM_PATH_FALLBACK, "relative shell folder");
    expect_w(buf, L"E:\\apps\\data", "relative shell folder path");

    set_roots(&roots, L"D:", NULL, FALSE, L"E:", TRUE);
    status = resolve(&roots, VETVDM_PATH_USER_ROOT, NULL, buf, MAX_PATH,
                     &needed, FALSE, &fallback);
    expect_status(status, VETVDM_PATH_FALLBACK, "exe in drive root");
    expect_w(buf, L"E:\\data", "data next to an exe in the drive root");
    expect_no_percent(buf, "drive-root fallback");

    fill(buf, MAX_PATH, L'#');
    set_roots(&roots, L"D:", NULL, FALSE, NULL, FALSE);
    needed = 99;
    fallback = FALSE;
    status = resolve(&roots, VETVDM_PATH_USER_ROOT, NULL, buf, MAX_PATH,
                     &needed, TRUE, &fallback);
    expect_status(status, VETVDM_PATH_ERR_NOEXE, "no exe directory");
    expect_true(fallback == TRUE, "no exe still reports the shell miss");
    expect_true(needed == 0, "no exe does not invent a length");
    expect_true(is_fill(buf, MAX_PATH, L'#'), "no exe leaves the buffer");
    expect_true(g_mkdir_calls == 0, "no exe creates nothing");

    fill(buf, MAX_PATH, L'#');
    made_reset();
    set_roots(&roots, L"D:", NULL, FALSE, L"E:\\bad%dir", TRUE);
    status = resolve(&roots, VETVDM_PATH_USER_ROOT, NULL, buf, MAX_PATH,
                     &needed, TRUE, &fallback);
    expect_status(status, VETVDM_PATH_ERR_PERCENT, "percent in exe directory");
    expect_true(fallback == TRUE, "percent exe dir still notes the shell miss");
    expect_true(is_fill(buf, 8, L'#'), "percent exe dir writes nothing");
    expect_true(g_mkdir_calls == 0, "percent exe dir creates nothing");
    expect_true(g_mkdir_saw_percent == 0, "mkdir not asked to create %");
}

static HRESULT WINAPI
shget_scripted(HWND hwnd, int csidl, HANDLE token, DWORD flags, LPWSTR path)
{
    g_sh_calls++;
    g_sh_hwnd = hwnd;
    g_sh_csidl = csidl;
    g_sh_token = token;
    g_sh_flags = flags;
    if (!path)
        return E_FAIL;
    path[0] = 0;
    if (FAILED(g_sh_hr) || !g_sh_path)
        return FAILED(g_sh_hr) ? g_sh_hr : E_FAIL;
    wcopy(path, MAX_PATH, g_sh_path);
    return g_sh_hr;
}

static void
test_query_shell_failure(void)
{
    vetvdm_path_roots roots;
    WCHAR buf[MAX_PATH];
    WCHAR shared[MAX_PATH];
    DWORD needed = 0;
    BOOL fallback = FALSE;
    DWORD status;

    g_sh_calls = 0;
    g_sh_hr = E_FAIL;
    g_sh_path = NULL;
    status = vetvdm_path_query_roots_ex(&roots, shget_scripted,
                                        L"E:\\apps\\vetvdm.exe");
    expect_status(status, VETVDM_PATH_OK, "query itself succeeds");
    expect_true(g_sh_calls == 1, "shell folder queried once");
    expect_true(g_sh_hwnd == NULL, "shell hwnd is NULL");
    expect_true(g_sh_token == NULL, "shell token is NULL");
    expect_true(g_sh_flags == SHGFP_TYPE_CURRENT, "shell flags");
    expect_true(g_sh_csidl == (CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE),
                "shell csidl is local app data, create");
    expect_true(roots.local_appdata_ok == FALSE, "failed shell folder is not ok");
    expect_true(roots.exe_dir_ok == TRUE, "exe dir recorded");
    expect_w(roots.exe_dir, L"E:\\apps", "exe dir from module path");
    expect_true(is_drive_token(roots.system_drive), "query drive token");
    expect_no_percent(roots.system_drive, "query drive");

    status = resolve(&roots, VETVDM_PATH_USER_ROOT, NULL, buf, MAX_PATH,
                     &needed, FALSE, &fallback);
    expect_status(status, VETVDM_PATH_FALLBACK, "query failure resolves to data");
    expect_w(buf, L"E:\\apps\\data", "query failure path");
    expect_true(fallback == TRUE, "query failure flag");
    expect_no_percent(buf, "query failure path");

    shared[0] = roots.system_drive[0];
    shared[1] = L':';
    shared[2] = L'\\';
    wcopy(shared + 3, MAX_PATH - 3, L"VetTV");
    status = resolve(&roots, VETVDM_PATH_SHARED_ROOT, NULL, buf, MAX_PATH,
                     &needed, FALSE, &fallback);
    expect_status(status, VETVDM_PATH_OK, "query shared status");
    expect_w(buf, shared, "query shared path");
    expect_no_percent(buf, "query shared path");

    g_sh_calls = 0;
    g_sh_hr = S_OK;
    g_sh_path = L"X:\\shell\\local\\";
    status = vetvdm_path_query_roots_ex(&roots, shget_scripted,
                                        L"E:\\apps\\vetvdm.exe");
    expect_true(roots.local_appdata_ok == TRUE, "shell success is ok");
    expect_w(roots.local_appdata, L"X:\\shell\\local", "trailing slash stripped");
    status = resolve(&roots, VETVDM_PATH_USER_ROOT, NULL, buf, MAX_PATH,
                     &needed, FALSE, &fallback);
    expect_status(status, VETVDM_PATH_OK, "query success is not a fallback");
    expect_w(buf, L"X:\\shell\\local\\VetTV\\DOS", "query success path");
    expect_true(fallback == FALSE, "query success flag");
    expect_no_percent(buf, "query success path");

    g_sh_hr = S_OK;
    g_sh_path = L"X:\\bad%name";
    status = vetvdm_path_query_roots_ex(&roots, shget_scripted,
                                        L"E:\\apps\\vetvdm.exe");
    expect_true(roots.local_appdata_ok == FALSE, "unexpanded shell result dropped");
    expect_true(roots.local_appdata[0] == 0, "unexpanded shell result not stored");

    g_sh_hr = S_OK;
    g_sh_path = L"shell\\local";
    status = vetvdm_path_query_roots_ex(&roots, shget_scripted,
                                        L"E:\\vetvdm.exe");
    expect_true(roots.local_appdata_ok == FALSE, "relative shell result dropped");
    expect_w(roots.exe_dir, L"E:", "exe in the drive root");

    status = vetvdm_path_query_roots_ex(&roots, shget_scripted, L"vetvdm.exe");
    expect_true(roots.exe_dir_ok == FALSE, "module path without a directory");

    status = vetvdm_path_query_roots_ex(&roots, shget_scripted,
                                        L"E:\\bad%dir\\vetvdm.exe");
    expect_true(roots.exe_dir_ok == FALSE, "percent module path dropped");
    expect_true(roots.exe_dir[0] == 0, "percent module path not stored");
}

static void
test_rejects_percent_and_escapes(void)
{
    vetvdm_path_roots roots;
    WCHAR buf[MAX_PATH];
    DWORD needed = 0;
    BOOL fallback = FALSE;
    DWORD status;

    set_roots(&roots, L"D:", L"X:\\shell\\local", TRUE, L"E:\\apps", TRUE);
    fill(buf, MAX_PATH, L'#');
    made_reset();

    status = resolve(&roots, VETVDM_PATH_SHARED_ROOT, L"logs\\bad%name\\file",
                     buf, MAX_PATH, &needed, TRUE, &fallback);
    expect_status(status, VETVDM_PATH_ERR_PERCENT, "percent suffix");
    expect_true(is_fill(buf, MAX_PATH, L'#'), "percent suffix writes nothing");
    expect_true(g_mkdir_calls == 0, "percent suffix creates nothing");
    expect_true(needed == 0, "percent suffix has no length");

    status = resolve(&roots, VETVDM_PATH_USER_ROOT, L"..\\outside",
                     buf, MAX_PATH, &needed, TRUE, &fallback);
    expect_status(status, VETVDM_PATH_ERR_INVAL, "dotdot suffix");
    expect_true(g_mkdir_calls == 0, "dotdot creates nothing");

    status = resolve(&roots, VETVDM_PATH_SHARED_ROOT, L"foo\\.\\bar",
                     buf, MAX_PATH, &needed, TRUE, &fallback);
    expect_status(status, VETVDM_PATH_ERR_INVAL, "dot suffix");

    status = resolve(&roots, VETVDM_PATH_SHARED_ROOT, L"\\abs",
                     buf, MAX_PATH, &needed, FALSE, &fallback);
    expect_status(status, VETVDM_PATH_ERR_INVAL, "absolute suffix");

    status = resolve(&roots, VETVDM_PATH_SHARED_ROOT, L"C:rel",
                     buf, MAX_PATH, &needed, FALSE, &fallback);
    expect_status(status, VETVDM_PATH_ERR_INVAL, "drive suffix");

    status = resolve(&roots, VETVDM_PATH_SHARED_ROOT, L"foo\\\\bar",
                     buf, MAX_PATH, &needed, FALSE, &fallback);
    expect_status(status, VETVDM_PATH_ERR_INVAL, "empty component");

    status = resolve(&roots, (vetvdm_path_kind)99, NULL, buf, MAX_PATH,
                     &needed, FALSE, &fallback);
    expect_status(status, VETVDM_PATH_ERR_INVAL, "bad kind");

    status = vetvdm_path_resolve(NULL, VETVDM_PATH_SHARED_ROOT, NULL, buf,
                                 MAX_PATH, &needed, FALSE, test_mkdir, &fallback);
    expect_status(status, VETVDM_PATH_ERR_INVAL, "null roots");

    expect_true(is_fill(buf, MAX_PATH, L'#'), "rejected calls leave the buffer");
    expect_true(g_mkdir_saw_percent == 0, "rejects never mkdir a percent name");
}

static void
test_buffer_edges(void)
{
    vetvdm_path_roots roots;
    WCHAR buf[MAX_PATH + 32];
    WCHAR suffix[320];
    DWORD i;
    DWORD needed = 0;
    BOOL fallback = TRUE;
    DWORD status;
    DWORD path_len;

    set_roots(&roots, L"D:", L"X:\\shell\\local", TRUE, L"E:\\apps", TRUE);
    made_reset();

    /* D:\VetTV is 8 WCHARs; capacity including NUL is 9. */
    fill(buf, 16, L'#');
    status = resolve(&roots, VETVDM_PATH_SHARED_ROOT, NULL, NULL, 0,
                     &needed, TRUE, &fallback);
    expect_status(status, VETVDM_PATH_ERR_NOSPACE, "size probe");
    expect_true(needed == 9, "size probe length");
    expect_true(g_mkdir_calls == 0, "size probe does not create");

    fill(buf, 16, L'#');
    needed = 0;
    status = resolve(&roots, VETVDM_PATH_SHARED_ROOT, NULL, buf, 8,
                     &needed, TRUE, &fallback);
    expect_status(status, VETVDM_PATH_ERR_NOSPACE, "one short");
    expect_true(needed == 9, "one short length");
    expect_true(is_fill(buf, 16, L'#'), "one short leaves the buffer");
    expect_true(g_mkdir_calls == 0, "one short does not create");

    fill(buf, 16, L'#');
    status = resolve(&roots, VETVDM_PATH_SHARED_ROOT, NULL, buf, 9,
                     &needed, FALSE, &fallback);
    expect_status(status, VETVDM_PATH_OK, "exact fit");
    expect_w(buf, L"D:\\VetTV", "exact fit text");
    expect_true(buf[8] == 0, "exact fit NUL");
    expect_true(needed == 9, "exact fit length");

    fill(buf, 16, L'#');
    status = resolve(&roots, VETVDM_PATH_SHARED_ROOT, NULL, buf, 12,
                     &needed, FALSE, &fallback);
    expect_status(status, VETVDM_PATH_OK, "larger buffer");
    expect_w(buf, L"D:\\VetTV", "larger buffer text");
    expect_true(buf[9] == L'#', "bytes after NUL stay");

    status = resolve(&roots, VETVDM_PATH_SHARED_ROOT, NULL, NULL, 4,
                     &needed, FALSE, &fallback);
    expect_status(status, VETVDM_PATH_ERR_INVAL, "null out with room");

    /* 250 suffix chars + "D:\VetTV\" (9) = 259, which fills MAX_PATH. */
    for (i = 0; i < 250; i++)
        suffix[i] = L'a';
    suffix[250] = 0;
    fill(buf, MAX_PATH, L'#');
    status = resolve(&roots, VETVDM_PATH_SHARED_ROOT, suffix, buf, MAX_PATH,
                     &needed, FALSE, &fallback);
    expect_status(status, VETVDM_PATH_OK, "max path status");
    expect_true(needed == MAX_PATH, "max path capacity");
    path_len = wlen(buf);
    expect_true(path_len == MAX_PATH - 1, "max path length");
    expect_no_percent(buf, "max path");
    expect_true(buf[0] == L'D' && buf[1] == L':' &&
                buf[2] == L'\\' && buf[8] == L'\\' && buf[9] == L'a',
                "max path shape");

    fill(buf, MAX_PATH, L'#');
    status = resolve(&roots, VETVDM_PATH_SHARED_ROOT, suffix, buf, MAX_PATH - 1,
                     &needed, TRUE, &fallback);
    expect_status(status, VETVDM_PATH_ERR_NOSPACE, "max path one short");
    expect_true(needed == MAX_PATH, "max path one short length");
    expect_true(is_fill(buf, MAX_PATH, L'#'), "max path one short buffer");
    expect_true(g_mkdir_calls == 0, "max path one short creates nothing");

    /* One more character no longer fits in MAX_PATH, even in a big buffer. */
    suffix[250] = L'a';
    suffix[251] = 0;
    fill(buf, MAX_PATH + 32, L'#');
    needed = 0;
    status = resolve(&roots, VETVDM_PATH_SHARED_ROOT, suffix, buf, MAX_PATH + 32,
                     &needed, TRUE, &fallback);
    expect_status(status, VETVDM_PATH_ERR_RANGE, "past max path");
    expect_true(needed == MAX_PATH + 1, "past max path length");
    expect_true(is_fill(buf, 32, L'#'), "past max path writes nothing");
    expect_true(g_mkdir_calls == 0, "past max path creates nothing");

    /* Shell folder itself fits; the VetTV tail pushes it past MAX_PATH. */
    {
        WCHAR local[MAX_PATH];
        local[0] = L'X';
        local[1] = L':';
        local[2] = L'\\';
        for (i = 0; i < 250; i++)
            local[3 + i] = L'b';
        local[253] = 0;
        set_roots(&roots, L"D:", local, TRUE, L"E:\\apps", TRUE);
        fill(buf, MAX_PATH, L'#');
        status = resolve(&roots, VETVDM_PATH_USER_ROOT, NULL, buf, MAX_PATH,
                         &needed, TRUE, &fallback);
        expect_status(status, VETVDM_PATH_ERR_RANGE, "long shell folder");
        expect_true(needed > MAX_PATH, "long shell folder length");
        expect_true(is_fill(buf, MAX_PATH, L'#'), "long shell folder writes nothing");
        expect_true(g_mkdir_calls == 0, "long shell folder creates nothing");
        expect_no_percent(local, "long shell folder input");
    }

    set_roots(&roots, L"D:", NULL, FALSE, L"E:\\apps", TRUE);
    fill(buf, 32, L'#');
    status = resolve(&roots, VETVDM_PATH_USER_ROOT, NULL, buf, 12,
                     &needed, TRUE, &fallback);
    expect_status(status, VETVDM_PATH_ERR_NOSPACE, "fallback one short");
    expect_true(needed == 13, "fallback capacity");
    expect_true(is_fill(buf, 32, L'#'), "fallback one short buffer");
    expect_true(g_mkdir_calls == 0, "fallback one short creates nothing");

    status = resolve(&roots, VETVDM_PATH_USER_ROOT, NULL, buf, 13,
                     &needed, FALSE, &fallback);
    expect_status(status, VETVDM_PATH_FALLBACK, "fallback exact");
    expect_w(buf, L"E:\\apps\\data", "fallback exact text");
    expect_no_percent(buf, "fallback exact");
}

static void
test_create(void)
{
    vetvdm_path_roots roots;
    WCHAR buf[MAX_PATH];
    DWORD needed = 0;
    BOOL fallback = FALSE;
    DWORD status;

    set_roots(&roots, L"D:", L"X:\\shell\\local", TRUE, L"E:\\apps", TRUE);
    made_reset();
    status = resolve(&roots, VETVDM_PATH_USER_LOG, NULL, buf, MAX_PATH,
                     &needed, TRUE, &fallback);
    expect_status(status, VETVDM_PATH_OK, "create user log");
    expect_true(g_mkdir_saw_percent == 0, "create names have no percent");
    expect_true(g_made_n == 5, "user log parent count");
    expect_w(g_made[0], L"X:\\shell", "create shell");
    expect_w(g_made[1], L"X:\\shell\\local", "create local");
    expect_w(g_made[2], L"X:\\shell\\local\\VetTV", "create brand");
    expect_w(g_made[3], L"X:\\shell\\local\\VetTV\\DOS", "create dos");
    expect_w(g_made[4], L"X:\\shell\\local\\VetTV\\DOS\\logs", "create logs");
    expect_no_percent(buf, "created user log path");

    made_reset();
    status = resolve(&roots, VETVDM_PATH_DOSBOXX, NULL, buf, MAX_PATH,
                     &needed, TRUE, &fallback);
    expect_status(status, VETVDM_PATH_OK, "create dosbox parents");
    expect_true(g_made_n == 2, "dosbox parent count");
    expect_w(g_made[0], L"D:\\VetTV", "create vettv");
    expect_w(g_made[1], L"D:\\VetTV\\dosbox-x", "create dosbox dir");

    made_reset();
    set_roots(&roots, L"D:", L"\\\\server\\share\\local", TRUE, L"E:\\apps", TRUE);
    status = resolve(&roots, VETVDM_PATH_USER_ROOT, NULL, buf, MAX_PATH,
                     &needed, TRUE, &fallback);
    expect_status(status, VETVDM_PATH_OK, "create unc");
    expect_true(g_made_n == 3, "unc create skips the share root");
    expect_w(g_made[0], L"\\\\server\\share\\local", "unc first create");
    expect_w(g_made[2], L"\\\\server\\share\\local\\VetTV\\DOS", "unc dos dir");

    made_reset();
    g_mkdir_fail_on = 2;
    set_roots(&roots, L"D:", L"X:\\shell\\local", TRUE, L"E:\\apps", TRUE);
    status = resolve(&roots, VETVDM_PATH_PROFILES, NULL, buf, MAX_PATH,
                     &needed, TRUE, &fallback);
    expect_status(status, VETVDM_PATH_ERR_CREATE, "mkdir failure");
    expect_w(buf, L"D:\\VetTV\\profiles", "mkdir failure still returns the path");
    expect_true(g_mkdir_calls == 2, "mkdir stops after failure");
    expect_no_percent(buf, "mkdir failure path");

    made_reset();
    status = resolve(&roots, VETVDM_PATH_SHARED_ROOT, L"logs\\",
                     buf, MAX_PATH, &needed, TRUE, &fallback);
    expect_status(status, VETVDM_PATH_OK, "trailing slash is a directory");
    expect_w(buf, L"D:\\VetTV\\logs", "trailing slash stripped");
    expect_w(g_made[g_made_n - 1], L"D:\\VetTV\\logs", "trailing slash created");
}

static void
test_live_os(void)
{
    vetvdm_path_roots roots;
    WCHAR buf[MAX_PATH];
    WCHAR expect[MAX_PATH];
    DWORD needed = 0;
    BOOL fallback = TRUE;
    DWORD status;
    DWORD len;

    status = vetvdm_path_query_roots(&roots);
    expect_status(status, VETVDM_PATH_OK, "live query");
    expect_true(is_drive_token(roots.system_drive), "live drive");
    expect_no_percent(roots.system_drive, "live drive");
    expect_no_percent(roots.local_appdata, "live shell folder");
    expect_no_percent(roots.exe_dir, "live exe dir");

    expect[0] = roots.system_drive[0];
    expect[1] = L':';
    expect[2] = L'\\';
    wcopy(expect + 3, MAX_PATH - 3, L"VetTV");
    status = vetvdm_path_resolve(&roots, VETVDM_PATH_SHARED_ROOT, NULL, buf,
                                 MAX_PATH, &needed, FALSE, test_mkdir, &fallback);
    expect_status(status, VETVDM_PATH_OK, "live shared");
    expect_w(buf, expect, "live shared path");
    expect_true(fallback == FALSE, "live shared flag");
    expect_no_percent(buf, "live shared");

    made_reset();
    status = vetvdm_path_resolve(&roots, VETVDM_PATH_USER_ROOT, NULL, buf,
                                 MAX_PATH, &needed, FALSE, test_mkdir, &fallback);
    expect_true(g_mkdir_calls == 0, "live user resolve does not create");
    if (roots.local_appdata_ok)
    {
        len = wlen(roots.local_appdata);
        expect_true(len + 10 < MAX_PATH, "live shell folder has room");
        wcopy(expect, MAX_PATH, roots.local_appdata);
        wcopy(expect + len, MAX_PATH - len, L"\\VetTV\\DOS");
        expect_status(status, VETVDM_PATH_OK, "live user");
        expect_w(buf, expect, "live user path");
        expect_true(fallback == FALSE, "live user flag");
    }
    else if (roots.exe_dir_ok)
    {
        len = wlen(roots.exe_dir);
        wcopy(expect, MAX_PATH, roots.exe_dir);
        wcopy(expect + len, MAX_PATH - len, L"\\data");
        expect_status(status, VETVDM_PATH_FALLBACK, "live fallback");
        expect_w(buf, expect, "live fallback path");
        expect_true(fallback == TRUE, "live fallback flag");
    }
    else
    {
        expect_status(status, VETVDM_PATH_ERR_NOEXE, "live has no per-user root");
        expect_true(fallback == TRUE, "live no-exe flag");
    }
    if (vetvdm_path_succeeded(status))
        expect_no_percent(buf, "live user result");

    /* create stays FALSE: this must not make a directory on the machine. */
    status = vetvdm_path_resolve_os(VETVDM_PATH_SHARED_ROOT, NULL, buf, MAX_PATH,
                                    &needed, FALSE, &fallback);
    expect_status(status, VETVDM_PATH_OK, "resolve_os shared");
    expect_no_percent(buf, "resolve_os shared");
    expect_true(fallback == FALSE, "resolve_os shared flag");
}

int
wmain(void)
{
    test_pick_drive();
    test_normal_paths();
    test_shell_failure_fallback();
    test_query_shell_failure();
    test_rejects_percent_and_escapes();
    test_buffer_edges();
    test_create();
    test_live_os();

    if (g_failed)
    {
        printf("vetvdm_paths_test: %d checks, %d failed\n", g_checks, g_failed);
        return 1;
    }
    printf("vetvdm_paths_test: %d checks ok\n", g_checks);
    return 0;
}

int
main(void)
{
    return wmain();
}
