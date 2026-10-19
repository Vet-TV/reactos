/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/vetvdm_test.c
 * PURPOSE:  Host-side tests for command line, path normalisation, and routing.
 *
 * Copyright (C) 2026 VetTV
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * Not installed on the boot image. The samples below are hand-written
 * headers, not copies of any program.
 */

#include "cmdline.h"
#include "image.h"
#include "route.h"
#include "vcore_util.h"

#include <stdio.h>
#include <string.h>

static int g_failed;
static int g_checks;

static void
check(int cond, const char *msg)
{
    g_checks++;
    if (!cond)
    {
        fprintf(stderr, "FAIL %s\n", msg);
        g_failed++;
    }
}

static void
put_u32(unsigned char *b, unsigned long value)
{
    b[0] = (unsigned char)value;
    b[1] = (unsigned char)(value >> 8);
    b[2] = (unsigned char)(value >> 16);
    b[3] = (unsigned char)(value >> 24);
}

static void
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

static int
has(const char *hay, const char *needle)
{
    return hay && needle && strstr(hay, needle) != NULL;
}

static void
test_normalize(void)
{
    WCHAR out[MAX_PATH];

    check(vetvdm_normalize_image_path(L"\\??\\C:\\Games\\DOOM.EXE", out, MAX_PATH) == VETVDM_OK, "nt drive");
    check(vcore_w_ieq(out, L"C:\\Games\\DOOM.EXE"), "nt drive text");
    check(vetvdm_normalize_image_path(L"\\??\\UNC\\server\\share\\a.exe", out, MAX_PATH) == VETVDM_OK, "nt unc");
    check(vcore_w_ieq(out, L"\\\\server\\share\\a.exe"), "nt unc text");
    check(vetvdm_normalize_image_path(L"\\\\?\\C:/Games/a.exe", out, MAX_PATH) == VETVDM_OK, "win32 nt drive");
    check(vcore_w_ieq(out, L"C:\\Games\\a.exe"), "win32 nt drive text");
    check(vetvdm_normalize_image_path(L"\\\\?\\UNC\\server\\share\\a.exe", out, MAX_PATH) == VETVDM_OK, "win32 nt unc");
    check(vcore_w_ieq(out, L"\\\\server\\share\\a.exe"), "win32 nt unc text");
    check(vetvdm_normalize_image_path(L"C:/Games/a.exe", out, MAX_PATH) == VETVDM_OK, "plain");
    check(vcore_w_ieq(out, L"C:\\Games\\a.exe"), "plain text");
    check(vetvdm_normalize_image_path(L"\\Device\\HarddiskVolume1\\a.exe", out, MAX_PATH) == VETVDM_E_CMDLINE, "device");
    check(vetvdm_normalize_image_path(L"\\\\.\\PIPE\\foo", out, MAX_PATH) == VETVDM_E_CMDLINE, "dot device");
    check(vetvdm_normalize_image_path(L"\\??\\HarddiskVolume1\\a.exe", out, MAX_PATH) == VETVDM_E_CMDLINE, "other nt");
    check(vetvdm_normalize_image_path(L"", out, MAX_PATH) == VETVDM_E_CMDLINE, "empty image");
    check(vetvdm_normalize_image_path(NULL, out, MAX_PATH) == VETVDM_E_CMDLINE, "null image");
}

static void
test_cmdline(void)
{
    vetvdm_cmdline cmd;
    DWORD status;
    const WCHAR *hook =
        L"C:\\ReactOS\\System32\\vetvdm.exe --from-hook --kind=dos "
        L"--image=\"C:\\Games\\DOOM.EXE\" -- C:\\Games\\DOOM.EXE -episode 1";
    const WCHAR *quoted_tail =
        L"vetvdm.exe --from-hook --kind=win16 --image=\"C:\\a.exe\" -- "
        L"\"C:\\Program Files\\game.exe\" /s";
    const WCHAR *inside =
        L"vetvdm.exe --image=\"C:\\foo -- bar.exe\" -- C:\\other.exe extra";
    const WCHAR *launcher = L"vetvdm.exe C:\\Games\\DOOM.EXE -episode 1";
    const WCHAR *spaced = L"vetvdm.exe \"C:\\Program Files\\game.exe\" -go";

    status = vetvdm_cmdline_parse(hook, &cmd);
    check(status == VETVDM_OK, "hook parse");
    check(cmd.from_hook == 1, "hook flag");
    check(cmd.kind == VETVDM_KIND_DOS, "hook kind");
    check(vcore_w_ieq(cmd.image, L"C:\\Games\\DOOM.EXE"), "hook image");
    check(cmd.tail && vcore_w_ieq(cmd.tail, L"C:\\Games\\DOOM.EXE -episode 1"), "hook tail");
    check(cmd.args && vcore_w_ieq(cmd.args, L"-episode 1"), "hook args");
    vetvdm_cmdline_free(&cmd);

    status = vetvdm_cmdline_parse(quoted_tail, &cmd);
    check(status == VETVDM_OK, "quoted tail parse");
    check(cmd.kind == VETVDM_KIND_WIN16, "quoted kind");
    check(vcore_w_ieq(cmd.image, L"C:\\a.exe"), "image wins");
    check(vcore_w_ieq(cmd.program, L"C:\\Program Files\\game.exe"), "quoted program");
    check(cmd.args && vcore_w_ieq(cmd.args, L"/s"), "quoted args");
    vetvdm_cmdline_free(&cmd);

    status = vetvdm_cmdline_parse(inside, &cmd);
    check(status == VETVDM_OK, "separator inside quotes");
    check(vcore_w_ieq(cmd.image, L"C:\\foo -- bar.exe"), "image keeps dashes");
    check(vcore_w_ieq(cmd.program, L"C:\\other.exe"), "tail program");
    check(cmd.args && vcore_w_ieq(cmd.args, L"extra"), "tail args");
    vetvdm_cmdline_free(&cmd);

    status = vetvdm_cmdline_parse(launcher, &cmd);
    check(status == VETVDM_OK, "launcher parse");
    check(cmd.from_hook == 0, "launcher not hook");
    check(vcore_w_ieq(cmd.image, L"C:\\Games\\DOOM.EXE"), "launcher image");
    check(cmd.args && vcore_w_ieq(cmd.args, L"-episode 1"), "launcher args");
    vetvdm_cmdline_free(&cmd);

    status = vetvdm_cmdline_parse(spaced, &cmd);
    check(status == VETVDM_OK, "spaced launcher");
    check(vcore_w_ieq(cmd.image, L"C:\\Program Files\\game.exe"), "spaced image");
    check(cmd.args && vcore_w_ieq(cmd.args, L"-go"), "spaced args");
    vetvdm_cmdline_free(&cmd);

    status = vetvdm_cmdline_parse(L"vetvdm.exe --kind=dos --kind=win16 C:\\a.com", &cmd);
    check(status == VETVDM_OK && cmd.kind == VETVDM_KIND_WIN16, "last kind wins");
    vetvdm_cmdline_free(&cmd);

    status = vetvdm_cmdline_parse(L"vetvdm.exe --layer=ntvdm --dry-run --keep-conf C:\\a.exe", &cmd);
    check(status == VETVDM_OK, "flags parse");
    check(cmd.layer == VETVDM_LAYER_NTVDM && cmd.dry_run && cmd.keep_conf, "flags values");
    vetvdm_cmdline_free(&cmd);

    status = vetvdm_cmdline_parse(L"vetvdm.exe --profile=doom --cwd=C:\\Games --log=C:\\v.log C:\\a.exe", &cmd);
    check(status == VETVDM_OK, "profile cwd log");
    check(vcore_w_ieq(cmd.profile, L"doom"), "profile id");
    check(vcore_w_ieq(cmd.cwd, L"C:\\Games"), "cwd");
    check(vcore_w_ieq(cmd.log_path, L"C:\\v.log"), "log path");
    vetvdm_cmdline_free(&cmd);

    status = vetvdm_cmdline_parse(L"vetvdm.exe --help", &cmd);
    check(status == VETVDM_OK && cmd.help, "help");
    vetvdm_cmdline_free(&cmd);

    status = vetvdm_cmdline_parse(L"vetvdm.exe --version", &cmd);
    check(status == VETVDM_OK && cmd.version, "version");
    vetvdm_cmdline_free(&cmd);

    status = vetvdm_cmdline_parse(L"vetvdm.exe --bogus C:\\a.exe", &cmd);
    check(status == VETVDM_E_CMDLINE, "unknown option");
    vetvdm_cmdline_free(&cmd);

    status = vetvdm_cmdline_parse(L"vetvdm.exe --kind=nope C:\\a.exe", &cmd);
    check(status == VETVDM_E_CMDLINE, "bad kind");
    vetvdm_cmdline_free(&cmd);

    status = vetvdm_cmdline_parse(L"vetvdm.exe --image=\\Device\\HarddiskVolume1\\a.exe -- C:\\a.exe", &cmd);
    check(status == VETVDM_E_CMDLINE, "device image");
    vetvdm_cmdline_free(&cmd);

    status = vetvdm_cmdline_parse(L"vetvdm.exe --image=\\??\\C:\\Games\\a.exe -- C:\\Games\\a.exe", &cmd);
    check(status == VETVDM_OK && vcore_w_ieq(cmd.image, L"C:\\Games\\a.exe"), "hook nt image");
    vetvdm_cmdline_free(&cmd);

    status = vetvdm_cmdline_parse(L"vetvdm.exe", &cmd);
    check(status == VETVDM_E_CMDLINE, "missing program");
    vetvdm_cmdline_free(&cmd);

    status = vetvdm_cmdline_parse(L"vetvdm.exe --from-hook extra -- C:\\a.exe", &cmd);
    check(status == VETVDM_E_CMDLINE, "arg before separator");
    vetvdm_cmdline_free(&cmd);
}

static void
test_classify_and_route(void)
{
    unsigned char b[128];
    const char pe[4] = { 'P', 'E', 0, 0 };
    vetvdm_route_input in;
    vetvdm_route_result out;
    int cls;
    DWORD err = 0;

    sample(b, sizeof(b), 0, 0, NULL, 0);
    check(vetvdm_classify_image(b, sizeof(b)) == VETVDM_IMG_DOS_MZ, "mz lfanew 0");
    sample(b, 32, 0, 0, NULL, 0);
    check(vetvdm_classify_image(b, 32) == VETVDM_IMG_DOS_MZ, "short mz");
    sample(b, sizeof(b), 1, 0, NULL, 0);
    check(vetvdm_classify_image(b, sizeof(b)) == VETVDM_IMG_DOS_MZ, "zm");

    sample(b, sizeof(b), 0, 0x40, "NE", 2);
    cls = vetvdm_classify_image(b, sizeof(b));
    check(cls == VETVDM_IMG_WIN16, "ne");

    sample(b, sizeof(b), 0, 0x40, "LE", 2);
    check(vetvdm_classify_image(b, sizeof(b)) == VETVDM_IMG_DOS_LE, "le");
    sample(b, sizeof(b), 0, 0x40, "LX", 2);
    check(vetvdm_classify_image(b, sizeof(b)) == VETVDM_IMG_DOS_LX, "lx");
    sample(b, sizeof(b), 0, 0x40, pe, 4);
    check(vetvdm_classify_image(b, sizeof(b)) == VETVDM_IMG_PE, "pe");
    check(vetvdm_classify_image((const unsigned char *)"XX", 2) == VETVDM_IMG_UNKNOWN, "garbage");

    check(vetvdm_kind_for_image(VETVDM_KIND_AUTO, VETVDM_IMG_UNKNOWN, L"C:\\a.com") == VETVDM_KIND_DOS, "com auto");
    check(vetvdm_kind_for_image(VETVDM_KIND_AUTO, VETVDM_IMG_UNKNOWN, L"C:\\a.exe") == VETVDM_KIND_AUTO, "exe unknown stays auto");
    check(vetvdm_kind_for_image(VETVDM_KIND_DOS, VETVDM_IMG_WIN16, L"C:\\a.exe") == VETVDM_KIND_DOS, "explicit dos");

    memset(&in, 0, sizeof(in));
    in.image = VETVDM_IMG_DOS_MZ;
    in.kind = VETVDM_KIND_AUTO;
    in.dos_mode = L"dosboxx";
    in.win16_mode = L"winevdm";
    check(vetvdm_decide_route(&in, &out) == VETVDM_OK && out.layer == VETVDM_LAYER_DOSBOXX, "mz dosbox");

    in.image = VETVDM_IMG_DOS_LE;
    check(vetvdm_decide_route(&in, &out) == VETVDM_OK && out.layer == VETVDM_LAYER_DOSBOXX, "le dosbox");
    in.image = VETVDM_IMG_DOS_LX;
    check(vetvdm_decide_route(&in, &out) == VETVDM_OK && out.layer == VETVDM_LAYER_DOSBOXX, "lx dosbox");

    in.image = VETVDM_IMG_WIN16;
    check(vetvdm_decide_route(&in, &out) == VETVDM_OK && out.layer == VETVDM_LAYER_WINEVDM, "ne wine");

    in.image = VETVDM_IMG_PE;
    in.kind = VETVDM_KIND_DOS;
    in.forced_layer = VETVDM_LAYER_DOSBOXX;
    check(vetvdm_decide_route(&in, &out) == VETVDM_E_NOTDOS, "pe rejected");
    in.forced_layer = VETVDM_LAYER_NONE;

    in.image = VETVDM_IMG_WIN16;
    in.kind = VETVDM_KIND_DOS;
    check(vetvdm_decide_route(&in, &out) == VETVDM_OK && out.layer == VETVDM_LAYER_DOSBOXX, "kind dos beats ne");

    in.image = VETVDM_IMG_DOS_MZ;
    in.kind = VETVDM_KIND_WIN16;
    check(vetvdm_decide_route(&in, &out) == VETVDM_OK && out.layer == VETVDM_LAYER_WINEVDM, "kind win16 beats mz");

    in.kind = VETVDM_KIND_AUTO;
    in.image = VETVDM_IMG_UNKNOWN;
    check(vetvdm_decide_route(&in, &out) == VETVDM_E_NOTDOS, "auto garbage");
    in.kind = VETVDM_KIND_DOS;
    check(vetvdm_decide_route(&in, &out) == VETVDM_OK && out.layer == VETVDM_LAYER_DOSBOXX, "dos com");
    in.kind = VETVDM_KIND_WIN16;
    check(vetvdm_decide_route(&in, &out) == VETVDM_E_NOTDOS, "win16 garbage");

    in.kind = VETVDM_KIND_DOS;
    in.image = VETVDM_IMG_DOS_MZ;
    in.passthrough = 1;
    check(vetvdm_decide_route(&in, &out) == VETVDM_OK && out.layer == VETVDM_LAYER_NTVDM, "passthrough");
    in.forced_layer = VETVDM_LAYER_DOSBOXX;
    check(vetvdm_decide_route(&in, &out) == VETVDM_OK && out.layer == VETVDM_LAYER_DOSBOXX && !out.ask, "layer beats passthrough");
    in.forced_layer = VETVDM_LAYER_NONE;
    in.passthrough = 0;
    in.profile_layer = L"winevdm";
    check(vetvdm_decide_route(&in, &out) == VETVDM_OK && out.layer == VETVDM_LAYER_WINEVDM, "profile layer");
    in.profile_layer = NULL;
    in.dos_mode = L"ask";
    check(vetvdm_decide_route(&in, &out) == VETVDM_OK && out.ask && out.layer == VETVDM_LAYER_DOSBOXX, "ask dos");
    in.dos_mode = L"ntvdm";
    check(vetvdm_decide_route(&in, &out) == VETVDM_OK && out.layer == VETVDM_LAYER_NTVDM && !out.ask, "policy ntvdm");

    in.kind = VETVDM_KIND_WIN16;
    in.image = VETVDM_IMG_WIN16;
    in.win16_mode = L"dosboxx";
    check(vetvdm_decide_route(&in, &out) == VETVDM_OK && out.layer == VETVDM_LAYER_WIN31, "win16 dosboxx is win31");
    in.win16_mode = L"ask";
    check(vetvdm_decide_route(&in, &out) == VETVDM_OK && out.ask && out.layer == VETVDM_LAYER_WINEVDM, "ask win16");

    check(vetvdm_layer_for_launch(VETVDM_LAYER_DOSBOXX, 0, 0, 1, 0, &err) == VETVDM_LAYER_NTVDM && err == VETVDM_OK, "dosbox fallback");
    check(vetvdm_layer_for_launch(VETVDM_LAYER_DOSBOXX, 1, 0, 1, 0, &err) == VETVDM_LAYER_DOSBOXX && err == VETVDM_E_NOLAYER, "forced dosbox");
    check(vetvdm_layer_for_launch(VETVDM_LAYER_WIN31, 0, 1, 1, 0, &err) == VETVDM_LAYER_WINEVDM && err == VETVDM_OK, "win31 to wine");
    check(vetvdm_layer_for_launch(VETVDM_LAYER_WIN31, 0, 1, 0, 0, &err) == VETVDM_LAYER_NTVDM && err == VETVDM_OK, "win31 to ntvdm");
    check(vetvdm_layer_for_launch(VETVDM_LAYER_WIN31, 1, 1, 1, 0, &err) == VETVDM_LAYER_WIN31 && err == VETVDM_E_WIN31, "forced win31");
    check(vetvdm_layer_for_launch(VETVDM_LAYER_WINEVDM, 0, 1, 0, 0, &err) == VETVDM_LAYER_NTVDM && err == VETVDM_OK, "wine fallback");
    check(vetvdm_layer_for_launch(VETVDM_LAYER_WINEVDM, 1, 1, 0, 0, &err) == VETVDM_LAYER_WINEVDM && err == VETVDM_E_NOLAYER, "forced wine");
}

static void
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
    check(has(conf.data, ">V:\\ERRORLVL.TXT echo %ERRORLEVEL%"), "errorlevel line");
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

static void
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

static void
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

static int
run_tests(void)
{
    test_normalize();
    test_cmdline();
    test_classify_and_route();
    test_quote_conf_hash_log();
    test_profile_and_policy();
    test_policy_merge();
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
