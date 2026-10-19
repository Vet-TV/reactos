/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/core/tests/test_cmdline.c
 * PURPOSE:  Command line, path, and routing tests.
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
#include "image.h"
#include "route.h"
#include "vcore_util.h"

#include <stdio.h>
#include <string.h>

void
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

void
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

void
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
    cls = VETVDM_IMG_WIN16;
    check(vetvdm_resolve_kind(VETVDM_KIND_DOS, VETVDM_IMG_WIN16, L"C:\\a.exe",
                             1, VETVDM_BIN_WOW, &cls) == VETVDM_KIND_WIN16,
         "wow overrides kind=dos");
    check(cls == VETVDM_IMG_WIN16, "wow class");
    cls = VETVDM_IMG_WIN16;
    check(vetvdm_resolve_kind(VETVDM_KIND_WIN16, VETVDM_IMG_WIN16, L"C:\\a.exe",
                             1, VETVDM_BIN_DOS, &cls) == VETVDM_KIND_DOS,
         "borland ne stays dos");
    check(cls == VETVDM_IMG_DOS_MZ, "borland class");
    cls = VETVDM_IMG_WIN16;
    check(vetvdm_resolve_kind(VETVDM_KIND_DOS, VETVDM_IMG_WIN16, L"C:\\a.exe",
                             0, 0, &cls) == VETVDM_KIND_DOS,
         "no binary type keeps kind");
    check(cls == VETVDM_IMG_WIN16, "no binary type keeps class");
    check(strcmp(vetvdm_wine_lookup_step(0, 1, 0, 1), "bundled") == 0, "wine bundled before pf");
    check(strcmp(vetvdm_wine_lookup_step(1, 1, 0, 1), "winevdm-path") == 0, "wine configured wins");
    check(strcmp(vetvdm_wine_lookup_step(0, 1, 1, 1), "programfiles") == 0, "wine same path skips bundled");
    check(strcmp(vetvdm_wine_lookup_step(0, 0, 0, 0), "missing") == 0, "wine missing");

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
