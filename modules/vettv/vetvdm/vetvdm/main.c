/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/vetvdm/main.c
 * PURPOSE:  vetvdm.exe orchestration: parse, route, launch, and exit.
 *
 * Copyright (C) 2026 VetTV
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "host.h"

#include "win32.h"
#include "route.h"
#include "vcore_util.h"

#include <stdlib.h>
#include <string.h>

#define VETVDM_VERSION_TEXT "vetvdm 0.1\n"

static const char g_help[] =
    "vetvdm 0.1\n"
    "Usage: vetvdm.exe [options] [--] <program> [args]\n"
    "       vetvdm.exe --from-hook --kind=dos|win16 --image=<path> -- <command>\n"
    "\n"
    "  --kind=dos|win16|auto     Image kind (default auto)\n"
    "  --layer=dosboxx|winevdm|ntvdm|win31\n"
    "  --profile=<id or path.json>\n"
    "  --image=<path>            Full image path (wins over the program token)\n"
    "  --cwd=<dir>\n"
    "  --log=<file>\n"
    "  --keep-conf               Leave the generated dosbox-x.conf in place\n"
    "  --dry-run                 Print the layer and conf, then exit 0\n"
    "  --from-hook               Keep the caller's console and working directory\n"
    "  --version\n"
    "  --help\n";

static DWORD
launch_layer(host *st, int layer, WCHAR *command, DWORD *exit_code)
{
    vcore_job job;
    const WCHAR *app = NULL;
    DWORD status;

    memset(&job, 0, sizeof(job));
    if (layer == VETVDM_LAYER_DOSBOXX || layer == VETVDM_LAYER_WIN31)
        app = st->dosbox;
    else if (layer == VETVDM_LAYER_WINEVDM)
        app = st->wine;
    else if (layer == VETVDM_LAYER_NTVDM)
        app = st->image;
    else
        return VETVDM_E_INTERNAL;
    if (!app || !app[0] || !command)
        return VETVDM_E_NOLAYER;
    status = vcore_launch_in_job(app, command, st->cwd[0] ? st->cwd : NULL, &job);
    if (status != VETVDM_OK)
        return status;
    status = vcore_job_wait(&job, exit_code);
    vcore_job_close(&job);
    if (status != VETVDM_OK)
        return status;
    if (layer == VETVDM_LAYER_DOSBOXX || layer == VETVDM_LAYER_WIN31)
        return read_errorlevel(st->temp, exit_code);
    return VETVDM_OK;
}

static void
cleanup_host(host *st)
{
    if (st->have_temp && !st->cmd.keep_conf)
        vcore_tempdir_remove(st->temp);
    vetvdm_cmdline_free(&st->cmd);
    free(st->header);
    st->header = NULL;
}

static WCHAR *
build_command(const host *st, int layer, const WCHAR *conf_path)
{
    if (layer == VETVDM_LAYER_DOSBOXX || layer == VETVDM_LAYER_WIN31)
        return host_command_dosbox(st, conf_path);
    if (layer == VETVDM_LAYER_WINEVDM)
        return host_command_wine(st);
    return host_command_ntvdm(st);
}

static char *
command_utf8(const WCHAR *command)
{
    char *text;
    DWORD n;
    DWORD cap;
    if (!command)
        return NULL;
    n = vcore_w_len(command);
    cap = n * 3u + 4u;
    text = (char *)malloc(cap);
    if (!text)
        return NULL;
    if (vcore_wide_to_utf8(command, text, cap) != VETVDM_OK)
    {
        free(text);
        return NULL;
    }
    return text;
}

static DWORD
emit_dry(host *st, int layer, const char *conf, const WCHAR *command)
{
    vcore_dry_input in;
    vcore_buf text;
    char image[1024];
    char cwd[1024];
    char *cmdline;
    DWORD status;

    if (vcore_wide_to_utf8(st->image, image, sizeof(image)) != VETVDM_OK)
        return VETVDM_E_CMDLINE;
    if (st->cwd[0] && vcore_wide_to_utf8(st->cwd, cwd, sizeof(cwd)) != VETVDM_OK)
        return VETVDM_E_CMDLINE;
    if (!st->cwd[0])
        cwd[0] = 0;
    cmdline = command_utf8(command);
    memset(&in, 0, sizeof(in));
    in.layer = vetvdm_layer_name(layer);
    in.kind = vetvdm_kind_name(st->kind);
    in.caller = st->cmd.from_hook ? "hook" : (st->cmd.profile[0] ? "shell" : "cli");
    in.profile_id = st->profile.id;
    in.profile_src = vcore_profile_src_name(st->profile.src);
    in.image = image;
    in.sha256 = st->sha;
    in.cwd = cwd;
    in.cmdline = cmdline ? cmdline : "";
    in.conf = conf;
    in.note = st->warn[0] ? st->warn : NULL;
    status = vcore_dry_run_text(&in, &text);
    free(cmdline);
    if (status != VETVDM_OK)
        return status;
    if (console_attach(1))
        console_write(text.data ? text.data : "");
    vcore_buf_free(&text);
    st->dry_ran = 1;
    return VETVDM_OK;
}

static DWORD
do_dry(host *st)
{
    vcore_buf conf;
    WCHAR conf_path[MAX_PATH];
    WCHAR *command = NULL;
    DWORD status = VETVDM_OK;
    const char *conf_text = NULL;
    int layer = st->layer;

    vcore_buf_init(&conf);
    conf_path[0] = 0;
    locate_layers(st);
    if ((layer == VETVDM_LAYER_DOSBOXX || layer == VETVDM_LAYER_WIN31) && !st->dosbox_ok)
        warn_add(st->warn, sizeof(st->warn), "dosbox-missing");
    if (layer == VETVDM_LAYER_WINEVDM && !st->wine_ok)
        warn_add(st->warn, sizeof(st->warn), "wine-missing");
    if (layer == VETVDM_LAYER_WIN31 && !st->win31_ok)
        warn_add(st->warn, sizeof(st->warn), "win31-missing");

    if (layer == VETVDM_LAYER_DOSBOXX ||
        (layer == VETVDM_LAYER_WIN31 && st->win31_ok))
    {
        status = build_conf(st, layer == VETVDM_LAYER_WIN31, &conf, conf_path, MAX_PATH);
        if (status != VETVDM_OK)
        {
            vcore_buf_free(&conf);
            return status;
        }
        status = write_conf(conf_path, conf.data ? conf.data : "");
        if (status != VETVDM_OK)
        {
            vcore_buf_free(&conf);
            return status;
        }
        conf_text = conf.data;
    }
    if (layer == VETVDM_LAYER_WIN31 && !st->win31_ok)
    {
        command = (WCHAR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 16 * sizeof(WCHAR));
        if (!command)
        {
            vcore_buf_free(&conf);
            return VETVDM_E_INTERNAL;
        }
        command[0] = L'-';
    }
    else
        command = build_command(st, layer, conf_path[0] ? conf_path : NULL);
    if (!command && (layer == VETVDM_LAYER_DOSBOXX || layer == VETVDM_LAYER_WIN31 ||
                     layer == VETVDM_LAYER_WINEVDM || layer == VETVDM_LAYER_NTVDM))
    {
        vcore_buf_free(&conf);
        st->why = "bad command line";
        return VETVDM_E_CMDLINE;
    }
    status = emit_dry(st, layer, conf_text, command);
    HeapFree(GetProcessHeap(), 0, command);
    vcore_buf_free(&conf);
    return status;
}

/* 1 when this process is the only one attached to its console. */
static int
owns_fresh_console(void)
{
    DWORD ids[4];
    DWORD n;

    n = GetConsoleProcessList(ids, 4);
    return n == 1;
}

/*
 * A double-clicked console program owns a new console. Drop it before the
 * game so the window does not stay empty. cmd.exe shares its console, and
 * --from-hook keeps the caller's console for the DOSBox-X child.
 */
static void
release_own_console(const host *st)
{
    if (!st || st->cmd.from_hook)
        return;
    if (owns_fresh_console())
        FreeConsole();
}

static DWORD
do_launch(host *st)
{
    int desired = st->layer;
    int layer;
    DWORD err = VETVDM_OK;
    DWORD code = 1;
    DWORD status;
    vcore_buf conf;
    WCHAR conf_path[MAX_PATH];
    WCHAR *command = NULL;

    locate_layers(st);
    layer = vetvdm_layer_for_launch(desired, st->forced, st->dosbox_ok, st->wine_ok,
                                  st->win31_ok, &err);
    if (err != VETVDM_OK)
    {
        st->layer = desired;
        if (err == VETVDM_E_NOLAYER)
        {
            const WCHAR *missing = L"";
            char utf[MAX_PATH * 3];
            if (desired == VETVDM_LAYER_DOSBOXX)
                missing = st->policy.dosboxx_path;
            else if (desired == VETVDM_LAYER_WINEVDM)
                missing = st->policy.winevdm_path;
            if (missing && missing[0] &&
                vcore_wide_to_utf8(missing, utf, sizeof(utf)) == VETVDM_OK)
                warn_add(st->warn, sizeof(st->warn), utf);
            st->why = "layer executable missing";
        }
        else
            st->why = "Windows 3.1 path missing";
        return err;
    }
    if (layer != desired)
    {
        warn_add(st->warn, sizeof(st->warn), "layer-fallback");
        st->layer = layer;
    }

    vcore_buf_init(&conf);
    conf_path[0] = 0;
    if (layer == VETVDM_LAYER_DOSBOXX || layer == VETVDM_LAYER_WIN31)
    {
        status = build_conf(st, layer == VETVDM_LAYER_WIN31, &conf, conf_path, MAX_PATH);
        if (status != VETVDM_OK)
        {
            vcore_buf_free(&conf);
            return status;
        }
        status = write_conf(conf_path, conf.data ? conf.data : "");
        vcore_buf_free(&conf);
        if (status != VETVDM_OK)
            return status;
    }
    command = build_command(st, layer, conf_path[0] ? conf_path : NULL);
    if (!command)
    {
        st->why = "bad command line";
        return VETVDM_E_CMDLINE;
    }
    release_own_console(st);
    status = launch_layer(st, layer, command, &code);
    HeapFree(GetProcessHeap(), 0, command);
    if (status != VETVDM_OK)
    {
        st->why = why_for(status, NULL);
        return status;
    }
    return code;
}

static DWORD
run(void)
{
    host st;
    DWORD code;

    memset(&st, 0, sizeof(st));
    st.started = GetTickCount();
    st.log_level = 1;
    st.kind = VETVDM_KIND_AUTO;
    code = vetvdm_cmdline_parse(GetCommandLineW(), &st.cmd);
    if (code != VETVDM_OK)
    {
        st.why = st.cmd.error ? st.cmd.error : "bad command line";
        goto done;
    }
    if (st.cmd.help || st.cmd.version)
    {
        console_attach(1);
        console_write(st.cmd.help ? g_help : VETVDM_VERSION_TEXT);
        st.skip_log = 1;
        code = VETVDM_OK;
        goto done;
    }
    if (st.cmd.from_hook)
        console_attach(0);

    code = load_policy(&st);
    if (code != VETVDM_OK)
        goto done;
    code = resolve_image(&st);
    if (code != VETVDM_OK)
        goto done;
    code = route_image(&st);
    if (code != VETVDM_OK)
        goto done;
    if (st.cmd.dry_run)
        code = do_dry(&st);
    else
        code = do_launch(&st);

done:
    if (code >= 0xE0000000u && !st.why)
        st.why = why_for(code, NULL);
    if (!st.dry_ran)
        write_log(&st, code);
    else if (st.log_level >= 2)
        write_log(&st, code);
    if (code >= 0xE0000000u)
        show_error(st.cmd.from_hook, code, why_for(code, st.why));
    cleanup_host(&st);
    return code;
}

int
wmain(int argc, WCHAR **argv)
{
    (void)argc;
    (void)argv;
    ExitProcess(run());
    return 0;
}
