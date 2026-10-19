/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/core/tests/test_core.c
 * PURPOSE:  Conf, profile, policy, and golden dry-run tests.
 *
 * Copyright (C) 2026 VetTV
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "test_harness.h"

#include "cmdline.h"
#include "vcore_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
test_quote_conf_hash_log(void)
{
    char line[256];
    char args[180];
    int truncated = 0;
    unsigned i;
    vcore_profile prof;
    vcore_conf_input input;
    vcore_mount_spec mount;
    vcore_buf conf;
    vcore_lock_report locks;
    vcore_log_rec rec;
    vcore_buf logline;
    vcore_dry_input dry;
    vcore_buf drytext;
    unsigned char hash[32];
    char hex[65];
    const char *template_text =
        "[sdl]\n"
        "fullscreen=false\n"
        "hostkey=ctrlshift\n"
        "mapperfile=pad.map\n"
        "output=opengl\n"
        "\n"
        "[dosbox]\n"
        "machine=vga\n"
        "\n"
        "[autoexec]\n"
        "echo leftover\n";
    const char *dosbox;
    const char *sdl;
    const char *sdl_end;

    for (i = 0; i < 140; i++)
        args[i] = 'A';
    args[140] = 0;
    check(vcore_dos_command("GAME.EXE", args, line, sizeof(line), &truncated) == VETVDM_OK, "dos quote");
    check(truncated == 1, "tail truncated");
    check(strlen(line) == strlen("GAME.EXE") + 1 + 127, "tail 127");
    check(vcore_dos_command("MY GAME.EXE", "", line, sizeof(line), &truncated) == VETVDM_OK, "quote space");
    check(strcmp(line, "\"MY GAME.EXE\"") == 0, "quoted program");
    check(vcore_dos_command("A\"B.EXE", "", line, sizeof(line), &truncated) == VETVDM_E_CMDLINE, "quote in name");

    memset(&prof, 0, sizeof(prof));
    vcore_cstr_copy(prof.machine, sizeof(prof.machine), "tandy");
    vcore_cstr_copy(prof.memsize, sizeof(prof.memsize), "32");
    vcore_cstr_copy(prof.id, sizeof(prof.id), "doom-sw-1.9");
    memset(&mount, 0, sizeof(mount));
    mount.drive = 'C';
    mount.type = VCORE_MOUNT_DIR;
    vcore_cstr_copy(mount.path, sizeof(mount.path), "C:\\Games");
    memset(&input, 0, sizeof(input));
    input.template_text = template_text;
    input.profile = &prof;
    input.mounts = &mount;
    input.n_mounts = 1;
    input.workdir_drive = "C";
    input.program_line = "DOOM.EXE";
    input.result_drive = "V";
    input.logfile = "C:\\temp\\dosbox.log";
    check(vcore_conf_build(&input, &conf, &locks) == VETVDM_OK, "conf build");
    check(conf.data != NULL, "conf text");
    check(has(conf.data, "machine=tandy"), "profile machine");
    check(has(conf.data, "memsize=32"), "profile mem");
    check(has(conf.data, "hostkey=mapper"), "hostkey mapper");
    check(!has(conf.data, "mapperfile"), "no mapperfile");
    check(!has(conf.data, "echo leftover"), "template autoexec dropped");
    check(has(conf.data, "DOOM.EXE"), "program line");
    check(has(conf.data, "exit\n"), "exit");
    check(has(conf.data, "config -get errorlevel >V:\\ERRORLVL.TXT\n"), "errorlevel capture");
    check(!has(conf.data, "%ERRORLEVEL%"), "no percent errorlevel");
    check(!has(conf.data, "if errorlevel"), "no errorlevel ladder");
    check(has(conf.data, "\nexit\n"), "exit");
    check(has(conf.data, "mount C \"C:\\Games\""), "mount");
    check(locks.stripped_mapperfile >= 1, "stripped count");
    dosbox = strstr(conf.data ? conf.data : "", "[dosbox]");
    sdl = strstr(conf.data ? conf.data : "", "[sdl]");
    check(dosbox && strstr(dosbox, "hostkey=mapper") != NULL, "hostkey in dosbox");
    if (sdl)
    {
        sdl_end = strstr(sdl + 1, "\n[");
        if (!sdl_end)
            sdl_end = sdl + strlen(sdl);
        check(strstr(sdl, "hostkey=") == NULL || strstr(sdl, "hostkey=") >= sdl_end, "no sdl hostkey");
    }
    else
        check(0, "sdl section");
    vcore_buf_free(&conf);

    vetvdm_sha256("", 0, hash);
    vetvdm_sha256_hex(hash, hex);
    check(strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0, "sha empty");
    vetvdm_sha256("abc", 3, hash);
    vetvdm_sha256_hex(hash, hex);
    check(strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0, "sha abc");

    memset(&rec, 0, sizeof(rec));
    rec.year = 2026;
    rec.month = 10;
    rec.day = 19;
    rec.hour = 4;
    rec.min = 5;
    rec.sec = 6;
    rec.pid = 42;
    rec.caller = "hook";
    rec.program = "C:\\Games\\DOOM.EXE";
    rec.sha256 = hex;
    rec.kind = "dos";
    rec.layer = "dosboxx";
    rec.profile_id = "doom-sw-1.9";
    rec.exit_code = 7;
    rec.duration_ms = 15;
    rec.warn = "truncated tail";
    check(vcore_log_format(&rec, &logline) == VETVDM_OK, "log format");
    check(has(logline.data, "2026-10-19 04:05:06 pid=42 caller=hook"), "log prefix");
    check(has(logline.data, "program=\"C:\\Games\\DOOM.EXE\""), "log program");
    check(has(logline.data, "exit=7 duration_ms=15"), "log exit");
    check(has(logline.data, "warn=truncated,tail"), "log warn");
    vcore_buf_free(&logline);
    rec.exit_code = VETVDM_E_NOPROG;
    rec.warn = NULL;
    check(vcore_log_format(&rec, &logline) == VETVDM_OK, "log error format");
    check(has(logline.data, "exit=0xE0000002"), "log high exit");
    vcore_buf_free(&logline);

    memset(&dry, 0, sizeof(dry));
    dry.layer = "dosboxx";
    dry.kind = "dos";
    dry.caller = "cli";
    dry.profile_id = "builtin";
    dry.profile_src = "builtin";
    dry.image = "C:\\Games\\DOOM.EXE";
    dry.sha256 = hex;
    dry.cwd = "C:\\Games";
    dry.cmdline = "dosbox-x.exe -conf C:\\temp\\dosbox-x.conf -exit -nopromptfolder -fastlaunch";
    dry.conf = "[dosbox]\nhostkey=mapper\n";
    check(vcore_dry_run_text(&dry, &drytext) == VETVDM_OK, "dry text");
    check(has(drytext.data, "layer=dosboxx\n"), "dry layer");
    check(has(drytext.data, "--- conf ---\n[dosbox]\nhostkey=mapper\n"), "dry conf");
    vcore_buf_free(&drytext);
}

static const char g_doom[] =
    "{\n"
    "  \"id\": \"doom-sw-1.9\",\n"
    "  \"match\": { \"sha256\": \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\", \"size\": 709905, \"name\": \"DOOM.EXE\" },\n"
    "  \"layer\": \"dosboxx\",\n"
    "  \"dosbox\": {\n"
    "    \"machine\": \"svga_s3\", \"memsize\": 16, \"cycles\": \"max\",\n"
    "    \"sbtype\": \"sb16\", \"gus\": false, \"mpu401\": \"intelligent\",\n"
    "    \"output\": \"opengl\", \"fullscreen\": true,\n"
    "    \"mounts\": [ { \"drive\": \"C\", \"path\": \".\", \"type\": \"dir\" } ],\n"
    "    \"autoexec\": [ \"DOOM.EXE %ARGS%\" ],\n"
    "    \"extra\": { \"dos\": { \"ver\": \"6.22\" } }\n"
    "  }\n"
    "}\n";

typedef struct fake_file
{
    const WCHAR *path;
    const char *body;
} fake_file;

static fake_file g_files[6];

static int
fake_read(void *ctx, const WCHAR *path, char *buf, DWORD cap, DWORD *len)
{
    int i;
    (void)ctx;
    for (i = 0; i < 6; i++)
    {
        unsigned n;
        if (!g_files[i].path || !vcore_w_ieq(g_files[i].path, path))
            continue;
        n = vcore_cstr_len(g_files[i].body);
        if (n + 1 > cap)
            return -1;
        memcpy(buf, g_files[i].body, n + 1);
        if (len)
            *len = n;
        return 1;
    }
    return 0;
}

void
test_profile_and_policy(void)
{
    vcore_profile prof;
    const WCHAR *dirs[1];
    const char *sha = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    vcore_expand_ctx ctx;
    WCHAR expanded[MAX_PATH];

    check(vcore_profile_parse(g_doom, (DWORD)strlen(g_doom), &prof) == VETVDM_OK, "doom parse");
    check(strcmp(prof.id, "doom-sw-1.9") == 0, "doom id");
    check(strcmp(prof.layer, "dosboxx") == 0, "doom layer");
    check(strcmp(prof.machine, "svga_s3") == 0, "doom machine");
    check(strcmp(prof.cycles, "max") == 0, "doom cycles");
    check(strcmp(prof.gus, "false") == 0, "doom gus");
    check(strcmp(prof.fullscreen, "true") == 0, "doom fullscreen");
    check(prof.n_mounts == 1 && prof.mounts[0].drive == L'C', "doom mount");
    check(prof.n_autoexec == 1 && strcmp(prof.autoexec[0], "DOOM.EXE %ARGS%") == 0, "doom autoexec");
    check(prof.n_extra == 1 && strcmp(prof.extra_val[0], "6.22") == 0, "doom extra");
    check(prof.match_size_set && prof.match_size == 709905u, "doom size");

    memset(g_files, 0, sizeof(g_files));
    dirs[0] = L"C:\\VetTV\\profiles";
    g_files[0].path = L"C:\\Games\\vettv.game.json";
    g_files[0].body = "{\"id\":\"beside\",\"layer\":\"ntvdm\"}";
    g_files[1].path = L"C:\\VetTV\\profiles\\by-hash\\aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.json";
    g_files[1].body = "{\"id\":\"hashhit\",\"layer\":\"winevdm\",\"match\":{\"sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}}";
    g_files[2].path = L"C:\\VetTV\\profiles\\by-name\\DOOM.EXE.json";
    g_files[2].body = "{\"id\":\"namehit\",\"match\":{\"size\":100}}";
    check(vcore_profile_lookup(dirs, 1, L"C:\\Games\\DOOM.EXE", NULL, 100, sha,
                             fake_read, NULL, &prof) == VETVDM_OK, "lookup game");
    check(strcmp(prof.id, "beside") == 0 && prof.src == VCORE_SRC_GAMEJSON, "game json wins");

    g_files[0].path = NULL;
    check(vcore_profile_lookup(dirs, 1, L"C:\\Games\\DOOM.EXE", NULL, 100, sha,
                             fake_read, NULL, &prof) == VETVDM_OK, "lookup hash");
    check(strcmp(prof.id, "hashhit") == 0 && prof.src == VCORE_SRC_BYHASH, "hash hit");

    g_files[1].body = "{\"id\":\"hashbad\",\"match\":{\"sha256\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"}}";
    check(vcore_profile_lookup(dirs, 1, L"C:\\Games\\DOOM.EXE", NULL, 100, sha,
                             fake_read, NULL, &prof) == VETVDM_OK, "hash mismatch falls through");
    check(strcmp(prof.id, "namehit") == 0 && prof.src == VCORE_SRC_BYNAME, "name hit");

    g_files[2].body = "{\"id\":\"namesize\",\"match\":{\"size\":99}}";
    check(vcore_profile_lookup(dirs, 1, L"C:\\Games\\DOOM.EXE", NULL, 100, sha,
                             fake_read, NULL, &prof) == VETVDM_OK, "size miss");
    check(strcmp(prof.id, "builtin") == 0 && prof.src == VCORE_SRC_BUILTIN, "builtin");

    g_files[3].path = L"C:\\VetTV\\profiles\\doom.json";
    g_files[3].body = "{\"id\":\"explicit-hit\",\"layer\":\"winevdm\"}";
    check(vcore_profile_lookup(dirs, 1, L"C:\\Games\\DOOM.EXE", L"doom", 100, sha,
                             fake_read, NULL, &prof) == VETVDM_OK, "explicit id");
    check(strcmp(prof.id, "explicit-hit") == 0 && prof.src == VCORE_SRC_EXPLICIT, "explicit hit");
    check(vcore_profile_lookup(dirs, 1, L"C:\\Games\\DOOM.EXE", L"missing", 100, sha,
                             fake_read, NULL, &prof) == VETVDM_E_PROFILE, "missing explicit");

    ctx.system_drive = L"C:";
    ctx.system_root = L"C:\\ReactOS";
    ctx.program_files = L"C:\\Program Files";
    check(vcore_expand_allowed(&ctx, L"%SystemDrive%\\VetTV\\dosbox-x\\dosbox-x.exe",
                             expanded, MAX_PATH) == VETVDM_OK, "expand drive");
    check(vcore_w_ieq(expanded, L"C:\\VetTV\\dosbox-x\\dosbox-x.exe"), "expand drive text");
    check(vcore_expand_allowed(&ctx, L"%SystemRoot%\\System32\\vetvdm.exe",
                             expanded, MAX_PATH) == VETVDM_OK, "expand root");
    check(vcore_expand_allowed(&ctx, L"%ProgramFiles%\\otvdm\\otvdmw.exe",
                             expanded, MAX_PATH) == VETVDM_OK, "expand pf");
    check(vcore_w_ieq(expanded, L"C:\\Program Files\\otvdm\\otvdmw.exe"), "expand pf text");
    check(vcore_expand_allowed(&ctx, L"%APPDATA%\\x", expanded, MAX_PATH) == VETVDM_E_CMDLINE, "no appdata");
    check(vcore_expand_allowed(&ctx, L"%LOCALAPPDATA%\\x", expanded, MAX_PATH) == VETVDM_E_CMDLINE, "no localappdata");
    check(vcore_expand_allowed(&ctx, L"%USERPROFILE%\\x", expanded, MAX_PATH) == VETVDM_E_CMDLINE, "no userprofile");
}

/* Policy callbacks live at file scope so C89 can take their address. */
typedef struct memsrc
{
    const WCHAR *dos_mode;
    const WCHAR *win16_mode;
    const WCHAR *profile_dirs;
    DWORD log_level;
    int has_log;
} memsrc;

static BOOL
mem_dword(void *ctx, const WCHAR *name, DWORD *out)
{
    memsrc *m = (memsrc *)ctx;
    if (m->has_log && vcore_w_ieq(name, L"LogLevel"))
    {
        *out = m->log_level;
        return TRUE;
    }
    return FALSE;
}

static BOOL
mem_string(void *ctx, const WCHAR *name, WCHAR *out, DWORD cch)
{
    memsrc *m = (memsrc *)ctx;
    const WCHAR *val = NULL;
    if (vcore_w_ieq(name, L"DosMode"))
        val = m->dos_mode;
    else if (vcore_w_ieq(name, L"Win16Mode"))
        val = m->win16_mode;
    if (!val || !val[0])
        return FALSE;
    if (vcore_w_len(val) + 1 > cch)
        return FALSE;
    vcore_w_copy(out, cch, val);
    return TRUE;
}

static BOOL
mem_multi(void *ctx, const WCHAR *name, WCHAR *out, DWORD cch)
{
    memsrc *m = (memsrc *)ctx;
    unsigned n;
    if (!vcore_w_ieq(name, L"ProfileDirs") || !m->profile_dirs)
        return FALSE;
    n = vcore_w_len(m->profile_dirs);
    if (n + 2 > cch)
        return FALSE;
    vcore_w_copy(out, cch, m->profile_dirs);
    out[n + 1] = 0;
    return TRUE;
}

void
test_policy_merge(void)
{
    vcore_expand_ctx ctx;
    vcore_policy defaults;
    vcore_policy out;
    vcore_policy_source machine;
    vcore_policy_source user;
    memsrc machine_mem;
    memsrc user_mem;

    ctx.system_drive = L"C:";
    ctx.system_root = L"C:\\ReactOS";
    ctx.program_files = L"C:\\Program Files";
    memset(&defaults, 0, sizeof(defaults));
    defaults.enabled = 1;
    defaults.log_level = 1;
    vcore_w_copy(defaults.dos_mode, 16, L"dosboxx");
    vcore_w_copy(defaults.win16_mode, 16, L"winevdm");
    vcore_w_copy(defaults.profile_dirs[0], MAX_PATH, L"C:\\VetTV\\profiles");
    defaults.n_profile_dirs = 1;

    memset(&machine_mem, 0, sizeof(machine_mem));
    machine_mem.dos_mode = L"ntvdm";
    machine_mem.profile_dirs = L"%SystemDrive%\\custom";
    machine_mem.has_log = 1;
    machine_mem.log_level = 2;
    memset(&user_mem, 0, sizeof(user_mem));
    user_mem.dos_mode = L"ask";

    memset(&machine, 0, sizeof(machine));
    memset(&user, 0, sizeof(user));
    machine.get_dword = mem_dword;
    machine.get_string = mem_string;
    machine.get_multi = mem_multi;
    machine.ctx = &machine_mem;
    user.get_dword = mem_dword;
    user.get_string = mem_string;
    user.get_multi = mem_multi;
    user.ctx = &user_mem;
    check(vcore_policy_load(&machine, &user, &defaults, &ctx, &out) == VETVDM_OK, "policy load");
    check(vcore_w_ieq(out.dos_mode, L"ask"), "hkcu dos mode");
    check(out.log_level == 2, "machine log level");
    check(out.n_profile_dirs == 1 && vcore_w_ieq(out.profile_dirs[0], L"C:\\custom"), "expanded profile dir");
    check(vcore_w_ieq(out.win16_mode, L"winevdm"), "default win16");

    machine_mem.profile_dirs = L"%APPDATA%\\nope";
    check(vcore_policy_load(&machine, &user, &defaults, &ctx, &out) == VETVDM_OK, "policy reject appdata");
    check(out.n_profile_dirs == 1 && vcore_w_ieq(out.profile_dirs[0], L"C:\\VetTV\\profiles"), "kept default dirs");
}

static const char *g_starlane =
    "{\n"
    "  \"id\": \"starlane\",\n"
    "  \"layer\": \"dosboxx\",\n"
    "  \"dosbox\": {\n"
    "    \"machine\": \"tandy\", \"memsize\": 32,\n"
    "    \"extra\": {\n"
    "      \"sdl\": { \"mapperfile\": \"pad.map\", \"hostkey\": \"ctrlshift\" },\n"
    "      \"dosbox\": { \"hostkey\": \"ctrlshift\" }\n"
    "    }\n"
    "  }\n"
    "}\n";

static const char *g_pixelquest =
    "{\n"
    "  \"id\": \"pixelquest\",\n"
    "  \"layer\": \"dosboxx\",\n"
    "  \"exe\": \"PIXEL.EXE\",\n"
    "  \"args\": \"-demo\",\n"
    "  \"dosbox\": {\n"
    "    \"cycles\": \"max\", \"sbtype\": \"sb16\",\n"
    "    \"mounts\": [ { \"drive\": \"C\", \"path\": \".\\\\games\", \"type\": \"dir\" } ],\n"
    "    \"autoexec\": [ \"PIXEL.EXE %ARGS%\" ]\n"
    "  }\n"
    "}\n";

static const char *g_castlekey =
    "{\n"
    "  \"id\": \"castlekey\",\n"
    "  \"layer\": \"ntvdm\",\n"
    "  \"dosbox\": { \"machine\": \"vgaonly\", \"fullscreen\": false }\n"
    "}\n";

static int
read_golden(const char *name, char *buf, unsigned cap)
{
    static const char *dirs[] = {
        "core/tests/golden/",
        "golden/",
        NULL
    };
    int d;
    for (d = 0; dirs[d]; d++)
    {
        char path[256];
        FILE *file;
        unsigned n = 0;
        size_t got;
        if (strlen(dirs[d]) + strlen(name) + 1 >= sizeof(path))
            continue;
        memcpy(path, dirs[d], strlen(dirs[d]) + 1);
        memcpy(path + strlen(dirs[d]), name, strlen(name) + 1);
        file = fopen(path, "rb");
        if (!file)
            continue;
        got = fread(buf, 1, cap - 1, file);
        n = (unsigned)got;
        buf[n] = 0;
        fclose(file);
        return 1;
    }
    return 0;
}

static void
cut_result(char *text)
{
    char *at = strstr(text, "config -get errorlevel ");
    if (at)
        *at = 0;
}

void
test_goldens(void)
{
    static const struct
    {
        const char *id;
        const char *json;
        const char *program;
        const char *layer;
        int use_auto;
    } cases[] = {
        { "starlane", NULL, "STARLANE.EXE", "dosboxx", 0 },
        { "pixelquest", NULL, "PIXEL.EXE", "dosboxx", 1 },
        { "castlekey", NULL, "CASTLE.EXE", "ntvdm", 0 }
    };
    const char *jsons[3];
    int i;

    jsons[0] = g_starlane;
    jsons[1] = g_pixelquest;
    jsons[2] = g_castlekey;
    for (i = 0; i < 3; i++)
    {
        vcore_profile prof;
        vcore_conf_input input;
        vcore_mount_spec mount;
        vcore_buf conf;
        vcore_lock_report locks;
        vcore_dry_input dry;
        vcore_buf text;
        char name[64];
        char expect[4096];
        char *prefix;

        expect[0] = 0;

        check(vcore_profile_parse(jsons[i], (DWORD)strlen(jsons[i]), &prof) == VETVDM_OK, cases[i].id);
        memset(&mount, 0, sizeof(mount));
        mount.drive = 'C';
        mount.type = VCORE_MOUNT_DIR;
        vcore_cstr_copy(mount.path, sizeof(mount.path), "C:\\Games");
        memset(&input, 0, sizeof(input));
        input.profile = &prof;
        input.mounts = &mount;
        input.n_mounts = 1;
        input.workdir_drive = "C";
        input.program_line = cases[i].program;
        input.args_for_profile = (i == 1) ? "-demo" : "";
        input.use_profile_autoexec = cases[i].use_auto;
        input.result_drive = "V";
        input.logfile = "C:\\temp\\dosbox.log";
        check(vcore_conf_build(&input, &conf, &locks) == VETVDM_OK, "golden conf");
        if (i == 0)
        {
            check(locks.stripped_mapperfile >= 1, "starlane stripped mapperfile");
            check(locks.forced_hostkey >= 1, "starlane forced hostkey");
            check(has(conf.data, "hostkey=mapper"), "starlane hostkey");
            check(!has(conf.data, "mapperfile"), "starlane no mapperfile");
            check(!has(conf.data, "ctrlshift"), "starlane no ctrlshift");
        }
        memset(&dry, 0, sizeof(dry));
        dry.layer = cases[i].layer;
        dry.kind = "dos";
        dry.caller = "cli";
        dry.profile_id = cases[i].id;
        dry.profile_src = "explicit";
        dry.image = "C:\\Games\\GAME.EXE";
        dry.sha256 = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
        dry.cwd = "C:\\Games";
        dry.cmdline = (i == 2)
            ? "C:\\Games\\GAME.EXE"
            : "\"C:\\VetTV\\dosbox-x\\dosbox-x.exe\" -conf \"C:\\temp\\dosbox-x.conf\" -exit -nopromptfolder -fastlaunch";
        dry.conf = conf.data;
        check(vcore_dry_run_text(&dry, &text) == VETVDM_OK, "golden dry");
        prefix = text.data ? text.data : (char *)"";
        check(has(prefix, "config -get errorlevel >V:\\ERRORLVL.TXT\n"), "golden capture");
        cut_result(prefix);
        memcpy(name, cases[i].id, strlen(cases[i].id) + 1);
        memcpy(name + strlen(cases[i].id), ".dry", 5);
        if (!read_golden(name, expect, sizeof(expect)))
        {
            fprintf(stderr, "MISSING %s\n%s\n", name, prefix);
            check(0, "golden file");
        }
        else
            check(strcmp(expect, prefix) == 0, name);
        if (expect[0] && strcmp(expect, prefix) != 0)
            fprintf(stderr, "GOLDEN %s\n--expect--\n%s\n--actual--\n%s\n", name, expect, prefix);
        vcore_buf_free(&conf);
        vcore_buf_free(&text);
    }
}
