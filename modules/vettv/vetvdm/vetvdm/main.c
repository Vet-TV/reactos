/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/vetvdm/main.c
 * PURPOSE:  vetvdm.exe host: policy, route, conf, and layer launch.
 *
 * Copyright (C) 2026 VetTV
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * Subsystem is windows (win32gui), not console. Explorer launches must
 * not allocate a console. --help, --version, --dry-run, and errors attach
 * to the parent console with AttachConsole when one exists. --from-hook
 * attaches as well so a DOSBox-X child inherits the caller's console, and
 * it shows no UI unless policy says ask and a desktop is visible.
 */

#include "win32.h"

#include "cmdline.h"
#include "image.h"
#include "route.h"
#include "vetvdm_paths.h"
#include "vcore_util.h"

#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VETVDM_VERSION_TEXT "vetvdm 0.1\n"
#define VETVDM_CMDLINE_CCH  32768u

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

static int g_console;

static void
warn_add(char *buf, unsigned cap, const char *text)
{
    unsigned n = 0;
    if (!buf || cap == 0 || !text || !text[0])
        return;
    while (buf[n])
        n++;
    if (n && n + 1 < cap)
        buf[n++] = ' ';
    while (*text && n + 1 < cap)
        buf[n++] = *text++;
    buf[n] = 0;
}

static int
file_exists(const WCHAR *path)
{
    DWORD attr;
    if (!path || !path[0])
        return 0;
    attr = GetFileAttributesW(path);
    if (attr == INVALID_FILE_ATTRIBUTES)
        return 0;
    if (attr & FILE_ATTRIBUTE_DIRECTORY)
        return 0;
    return 1;
}

static int
dir_exists(const WCHAR *path)
{
    DWORD attr;
    if (!path || !path[0])
        return 0;
    attr = GetFileAttributesW(path);
    if (attr == INVALID_FILE_ATTRIBUTES)
        return 0;
    return (attr & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
}

static void
file_part(const WCHAR *path, WCHAR *out, DWORD cch)
{
    const WCHAR *last = path ? path : L"";
    const WCHAR *cursor;
    for (cursor = last; *cursor; cursor++)
    {
        if (*cursor == L'\\' || *cursor == L'/')
            last = cursor + 1;
    }
    vcore_w_copy(out, cch, last);
}

static void
parent_dir(const WCHAR *path, WCHAR *out, DWORD cch)
{
    unsigned n;
    unsigned i;
    unsigned cut = 0;
    int seen = 0;

    if (!out || cch == 0)
        return;
    out[0] = 0;
    if (!path)
        return;
    n = vcore_w_len(path);
    for (i = 0; i < n; i++)
    {
        if (path[i] == L'\\' || path[i] == L'/')
        {
            cut = i;
            seen = 1;
        }
    }
    if (!seen)
        return;
    if (cut == 2 && path[1] == L':')
        cut = 3;
    if (cut >= cch)
        return;
    for (i = 0; i < cut; i++)
        out[i] = path[i];
    out[cut] = 0;
}

static int
join_path(WCHAR *out, DWORD cch, const WCHAR *dir, const WCHAR *tail)
{
    unsigned n;
    unsigned i;
    unsigned t;
    int slash;

    if (!out || cch < 2 || !dir || !tail)
        return 0;
    n = vcore_w_len(dir);
    t = vcore_w_len(tail);
    slash = (n > 0 && dir[n - 1] != L'\\' && dir[n - 1] != L'/');
    if (n + (slash ? 1u : 0u) + t + 1u > cch)
        return 0;
    for (i = 0; i < n; i++)
        out[i] = dir[i];
    if (slash)
        out[n++] = L'\\';
    for (i = 0; i < t; i++)
        out[n++] = tail[i];
    out[n] = 0;
    return 1;
}

static int
replace_file(const WCHAR *full, const WCHAR *name, WCHAR *out, DWORD cch)
{
    WCHAR dir[MAX_PATH];
    parent_dir(full, dir, MAX_PATH);
    if (!dir[0])
        return 0;
    return join_path(out, cch, dir, name);
}

static DWORD
full_path(const WCHAR *in, WCHAR *out, DWORD cch)
{
    DWORD n;
    if (!out || cch == 0)
        return VETVDM_E_INTERNAL;
    out[0] = 0;
    if (!in || !in[0])
        return VETVDM_OK;
    n = GetFullPathNameW(in, cch, out, NULL);
    if (n == 0 || n >= cch)
        return VETVDM_E_CMDLINE;
    return VETVDM_OK;
}

static int
console_attach(int alloc_ok)
{
    HANDLE out;
    if (g_console)
        return 1;
    if (!AttachConsole(ATTACH_PARENT_PROCESS))
    {
        out = GetStdHandle(STD_OUTPUT_HANDLE);
        if (out && out != INVALID_HANDLE_VALUE && GetFileType(out) == FILE_TYPE_CHAR)
        {
            g_console = 1;
            return 1;
        }
        if (!alloc_ok || !AllocConsole())
            return 0;
    }
    out = CreateFileW(L"CONOUT$", GENERIC_WRITE,
                     FILE_SHARE_READ | FILE_SHARE_WRITE,
                     NULL, OPEN_EXISTING, 0, NULL);
    if (out == INVALID_HANDLE_VALUE)
        return 0;
    SetStdHandle(STD_OUTPUT_HANDLE, out);
    SetStdHandle(STD_ERROR_HANDLE, out);
    g_console = 1;
    return 1;
}

static void
console_write(const char *text)
{
    DWORD bytes;
    DWORD wrote = 0;
    WCHAR *wide;
    HANDLE out;
    DWORD chars;

    if (!g_console || !text || !text[0])
        return;
    bytes = 0;
    while (text[bytes])
        bytes++;
    wide = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, ((size_t)bytes + 1u) * sizeof(WCHAR));
    if (!wide)
        return;
    if (vcore_utf8_to_wide(text, wide, bytes + 1u) != VETVDM_OK)
    {
        HeapFree(GetProcessHeap(), 0, wide);
        return;
    }
    out = GetStdHandle(STD_OUTPUT_HANDLE);
    chars = vcore_w_len(wide);
    if (!out || out == INVALID_HANDLE_VALUE ||
        !WriteConsoleW(out, wide, chars, &wrote, NULL))
    {
        if (out && out != INVALID_HANDLE_VALUE)
            WriteFile(out, text, bytes, &wrote, NULL);
    }
    HeapFree(GetProcessHeap(), 0, wide);
}

static void
show_error(int from_hook, DWORD code, const char *why)
{
    char line[320];

    if (code < 0xE0000000u)
        return;
    _snprintf(line, sizeof(line), "vetvdm: %s (0x%08lX)\n",
             why ? why : "error", (unsigned long)code);
    line[sizeof(line) - 1] = 0;
    /* user32 stays in ask.c. A launcher with no console gets one here. */
    if (console_attach(from_hook ? 0 : 1))
        console_write(line);
}

static const char *
why_for(DWORD code, const char *why)
{
    if (why && why[0])
        return why;
    switch (code)
    {
    case VETVDM_E_CMDLINE:
        return "bad command line";
    case VETVDM_E_NOPROG:
        return "program not found";
    case VETVDM_E_NOTDOS:
        return "not a DOS or Win16 image";
    case VETVDM_E_NOLAYER:
        return "layer executable missing";
    case VETVDM_E_START:
        return "could not start the layer";
    case VETVDM_E_WIN31:
        return "Windows 3.1 path missing";
    case VETVDM_E_PROFILE:
        return "profile error";
    case VETVDM_E_CANCEL:
        return "cancelled";
    case VETVDM_E_NORESULT:
        return "layer exited without a result";
    case VETVDM_E_INTERNAL:
        return "internal error";
    default:
        return "error";
    }
}

typedef struct host
{
    vetvdm_cmdline cmd;
    vcore_policy policy;
    vcore_profile profile;
    vetvdm_path_roots roots;
    vcore_expand_ctx expand;
    WCHAR system_root[MAX_PATH];
    WCHAR program_files[MAX_PATH];
    WCHAR image[MAX_PATH];
    WCHAR cwd[MAX_PATH];
    WCHAR dosbox[MAX_PATH];
    WCHAR wine[MAX_PATH];
    WCHAR win31_root[MAX_PATH];
    WCHAR wincom[64];
    WCHAR temp[MAX_PATH];
    WCHAR log_path[MAX_PATH];
    char sha[65];
    char warn[192];
    const char *why;
    DWORD file_size;
    DWORD started;
    DWORD log_level;
    int have_roots;
    int cls;
    int kind;
    int layer;
    int forced;
    int dosbox_ok;
    int wine_ok;
    int win31_ok;
    int have_temp;
    int skip_log;
    int dry_ran;
    unsigned char *header;
} host;

static int
oem_ok(const WCHAR *text)
{
    BOOL used = FALSE;
    char scratch[4096];
    int wrote;
    if (!text || !text[0])
        return 1;
    wrote = WideCharToMultiByte(CP_OEMCP, WC_NO_BEST_FIT_CHARS,
                               text, -1, scratch, (int)sizeof(scratch),
                               NULL, &used);
    if (wrote <= 0 || used)
        return 0;
    return 1;
}

static void
locate_sibling(const WCHAR *configured, const WCHAR *const *names,
              int nnames, WCHAR *out, DWORD cch, int *found)
{
    int i;
    *found = 0;
    out[0] = 0;
    if (!configured || !configured[0])
        return;
    vcore_w_copy(out, cch, configured);
    if (file_exists(configured))
    {
        *found = 1;
        return;
    }
    for (i = 0; i < nnames; i++)
    {
        WCHAR sibling[MAX_PATH];
        if (!replace_file(configured, names[i], sibling, MAX_PATH))
            continue;
        if (file_exists(sibling))
        {
            vcore_w_copy(out, cch, sibling);
            *found = 1;
            return;
        }
    }
}

/*
 * deps.lock pins DOSBox-X 2026.10.01. The XP payload names are
 * dosbox-x_XPx86_SDL2.exe then dosbox-x_XPx86_SDL1.exe. The spec default
 * path is still dosbox-x.exe, so that filename (or an empty name) selects
 * the pinned siblings in the same directory. Any other DosBoxXPath filename
 * is a user override and is tried first.
 */
static void
locate_dosbox(const WCHAR *configured, WCHAR *out, DWORD cch, int *found)
{
    static const WCHAR *pinned[] = {
        L"dosbox-x_XPx86_SDL2.exe",
        L"dosbox-x_XPx86_SDL1.exe",
        L"dosbox-x.exe"
    };
    WCHAR base[MAX_PATH];
    int custom;
    int i;

    *found = 0;
    out[0] = 0;
    if (!configured || !configured[0])
        return;
    file_part(configured, base, MAX_PATH);
    custom = !vcore_w_ieq(base, L"dosbox-x.exe");
    if (custom && file_exists(configured))
    {
        vcore_w_copy(out, cch, configured);
        *found = 1;
        return;
    }
    for (i = 0; i < 3; i++)
    {
        WCHAR sibling[MAX_PATH];
        if (!replace_file(configured, pinned[i], sibling, MAX_PATH))
            continue;
        if (!out[0] && !custom)
            vcore_w_copy(out, cch, sibling);
        if (file_exists(sibling))
        {
            vcore_w_copy(out, cch, sibling);
            *found = 1;
            return;
        }
    }
    if (!out[0])
        vcore_w_copy(out, cch, configured);
}

/* WineVDM pin is 0.8.1 at %ProgramFiles%\otvdm\otvdmw.exe, then otvdm.exe. */
static void
locate_wine(const WCHAR *configured, WCHAR *out, DWORD cch, int *found)
{
    static const WCHAR *names[] = {
        L"otvdmw.exe",
        L"otvdm.exe"
    };
    locate_sibling(configured, names, 2, out, cch, found);
}

static int
win31_probe(const WCHAR *root, WCHAR *guest, DWORD cch)
{
    WCHAR path[MAX_PATH];
    if (guest && cch)
        guest[0] = 0;
    if (!root || !root[0] || !dir_exists(root))
        return 0;
    if (join_path(path, MAX_PATH, root, L"WINDOWS\\WIN.COM") && file_exists(path))
    {
        vcore_w_copy(guest, cch, L"C:\\WINDOWS\\WIN.COM");
        return 1;
    }
    if (join_path(path, MAX_PATH, root, L"WIN.COM") && file_exists(path))
    {
        vcore_w_copy(guest, cch, L"C:\\WIN.COM");
        return 1;
    }
    return 0;
}

static int
passthrough_hit(const vcore_policy *pol, const WCHAR *image, const char *sha)
{
    WCHAR name[MAX_PATH];
    int i;
    file_part(image, name, MAX_PATH);
    vcore_w_upper_ascii(name);
    for (i = 0; i < pol->n_pass; i++)
    {
        WCHAR entry[MAX_PATH];
        char utf[128];
        vcore_w_copy(entry, MAX_PATH, pol->pass_through[i]);
        vcore_w_upper_ascii(entry);
        if (entry[0] && vcore_w_ieq(entry, name))
            return 1;
        if (sha && sha[0] &&
            vcore_wide_to_utf8(pol->pass_through[i], utf, sizeof(utf)) == VETVDM_OK &&
            vcore_cstr_ieq(utf, sha))
            return 1;
    }
    return 0;
}

static void
fill_defaults(host *st)
{
    WCHAR path[MAX_PATH];
    vcore_policy *pol = &st->policy;

    memset(pol, 0, sizeof(*pol));
    pol->enabled = 1;
    pol->log_level = 1;
    vcore_w_copy(pol->dos_mode, 16, L"dosboxx");
    vcore_w_copy(pol->win16_mode, 16, L"winevdm");
    if (st->have_roots &&
        vetvdm_path_succeeded(vetvdm_path_resolve(
            &st->roots, VETVDM_PATH_DOSBOXX, NULL, path, MAX_PATH,
            NULL, FALSE, NULL, NULL)))
        vcore_w_copy(pol->dosboxx_path, MAX_PATH, path);
    if (st->have_roots &&
        vetvdm_path_succeeded(vetvdm_path_resolve(
            &st->roots, VETVDM_PATH_PROFILES, NULL, path, MAX_PATH,
            NULL, FALSE, NULL, NULL)))
    {
        vcore_w_copy(pol->profile_dirs[0], MAX_PATH, path);
        pol->n_profile_dirs = 1;
    }
    if (st->have_roots &&
        vetvdm_path_succeeded(vetvdm_path_resolve(
            &st->roots, VETVDM_PATH_CONF_TEMPLATE, NULL, path, MAX_PATH,
            NULL, FALSE, NULL, NULL)))
        vcore_w_copy(pol->conf_template, MAX_PATH, path);
    if (st->program_files[0] &&
        join_path(path, MAX_PATH, st->program_files, L"otvdm\\otvdmw.exe"))
        vcore_w_copy(pol->winevdm_path, MAX_PATH, path);
}

static DWORD
load_policy(host *st)
{
    HKEY machine = NULL;
    HKEY user = NULL;
    vetvdm_reg_ctx machine_ctx;
    vetvdm_reg_ctx user_ctx;
    vcore_policy_source machine_src;
    vcore_policy_source user_src;
    vcore_policy defaults;
    UINT nroot;
    HRESULT hr;

    if (vetvdm_path_query_roots(&st->roots) == VETVDM_PATH_OK)
        st->have_roots = 1;
    st->expand.system_drive = st->have_roots ? st->roots.system_drive : L"C:";
    nroot = GetSystemWindowsDirectoryW(st->system_root, MAX_PATH);
    if (nroot > 0 && nroot < MAX_PATH)
        st->expand.system_root = st->system_root;
    hr = SHGetFolderPathW(NULL, CSIDL_PROGRAM_FILES, NULL, SHGFP_TYPE_CURRENT,
                         st->program_files);
    if (FAILED(hr))
        st->program_files[0] = 0;
    else
        st->expand.program_files = st->program_files;

    fill_defaults(st);
    defaults = st->policy;
    vetvdm_reg_open(HKEY_LOCAL_MACHINE, &machine);
    vetvdm_reg_open(HKEY_CURRENT_USER, &user);
    memset(&machine_ctx, 0, sizeof(machine_ctx));
    memset(&user_ctx, 0, sizeof(user_ctx));
    machine_ctx.key = machine;
    machine_ctx.expand = &st->expand;
    user_ctx.key = user;
    user_ctx.expand = &st->expand;
    vetvdm_reg_source(&machine_src, &machine_ctx);
    vetvdm_reg_source(&user_src, &user_ctx);
    if (!machine)
        memset(&machine_src, 0, sizeof(machine_src));
    if (!user)
        memset(&user_src, 0, sizeof(user_src));
    vcore_policy_load(machine ? &machine_src : NULL,
                     user ? &user_src : NULL,
                     &defaults, &st->expand, &st->policy);
    vetvdm_reg_close(machine);
    vetvdm_reg_close(user);
    st->log_level = st->policy.log_level;
    return VETVDM_OK;
}

static void
dos_guest_name(const host *st, WCHAR *out, DWORD cch)
{
    const char *guest = st->profile.guest_exe;
    int plain = guest && guest[0];
    unsigned i;
    for (i = 0; guest && guest[i]; i++)
    {
        if (guest[i] == '\\' || guest[i] == '/' || guest[i] == ':')
            plain = 0;
    }
    if (plain && vcore_utf8_to_wide(guest, out, cch) == VETVDM_OK && out[0])
        return;
    file_part(st->image, out, cch);
}

static unsigned
strip_dir(WCHAR *path)
{
    unsigned n = vcore_w_len(path);
    while (n > 3 && (path[n - 1] == L'\\' || path[n - 1] == L'/'))
        path[--n] = 0;
    return n;
}

static int
prefix_rel(const WCHAR *root_in, const WCHAR *dir_in, WCHAR *rel, DWORD cch)
{
    WCHAR root[MAX_PATH];
    WCHAR dir[MAX_PATH];
    unsigned nr;
    unsigned i;

    vcore_w_copy(root, MAX_PATH, root_in ? root_in : L"");
    vcore_w_copy(dir, MAX_PATH, dir_in ? dir_in : L"");
    nr = strip_dir(root);
    strip_dir(dir);
    if (!nr || vcore_w_len(dir) < nr)
        return -1;
    for (i = 0; i < nr; i++)
    {
        WCHAR a = root[i];
        WCHAR b = dir[i];
        if (a == L'/')
            a = L'\\';
        if (b == L'/')
            b = L'\\';
        if (vcore_w_lower(a) != vcore_w_lower(b))
            return -1;
    }
    if (dir[nr] == 0)
    {
        if (rel && cch)
            rel[0] = 0;
        return (int)nr;
    }
    if (dir[nr] != L'\\' && dir[nr] != L'/')
        return -1;
    if (rel)
        vcore_w_copy(rel, cch, dir + nr + 1);
    return (int)nr;
}

static char
upper_drive(char drive)
{
    if (drive >= 'a' && drive <= 'z')
        return (char)(drive - 'a' + 'A');
    return drive;
}

static char
pick_result_drive(const vcore_mount_spec *mounts, int n)
{
    char letter;
    for (letter = 'V'; letter >= 'A'; letter--)
    {
        int i;
        int used = 0;
        for (i = 0; i < n; i++)
        {
            if (upper_drive(mounts[i].drive) == letter)
                used = 1;
        }
        if (!used)
            return letter;
    }
    return 0;
}

static DWORD
add_mount(vcore_mount_spec *mounts, int *n, int maxn, char drive, int type,
         const WCHAR *path)
{
    vcore_mount_spec *slot;
    if (*n >= maxn)
        return VETVDM_E_INTERNAL;
    slot = &mounts[*n];
    memset(slot, 0, sizeof(*slot));
    slot->drive = upper_drive(drive);
    slot->type = type;
    if (vcore_wide_to_utf8(path, slot->path, sizeof(slot->path)) != VETVDM_OK)
        return VETVDM_E_CMDLINE;
    (*n)++;
    return VETVDM_OK;
}

static DWORD
fill_args(const host *st, char *args_utf, DWORD cap)
{
    args_utf[0] = 0;
    if (st->cmd.args && st->cmd.args[0])
    {
        if (!oem_ok(st->cmd.args))
            return VETVDM_E_CMDLINE;
        return vcore_wide_to_utf8(st->cmd.args, args_utf, cap);
    }
    if (st->profile.guest_args[0])
    {
        WCHAR wide[200];
        if (vcore_utf8_to_wide(st->profile.guest_args, wide, 200) != VETVDM_OK)
            return VETVDM_E_PROFILE;
        if (!oem_ok(wide))
            return VETVDM_E_CMDLINE;
        vcore_cstr_copy(args_utf, cap, st->profile.guest_args);
    }
    return VETVDM_OK;
}

static DWORD
build_conf(host *st, int win31, vcore_buf *conf, WCHAR *conf_path, DWORD cch_path)
{
    vcore_mount_spec mounts[10];
    int n_mounts = 0;
    WCHAR prog_dir[MAX_PATH];
    WCHAR rel[MAX_PATH];
    WCHAR guest[MAX_PATH];
    char guest_utf[MAX_PATH * 3];
    char args_utf[2048];
    char program_line[1024];
    char cd_rel[MAX_PATH * 3];
    char work[4];
    char result[4];
    char logfile[1024];
    char *template_text = NULL;
    vcore_conf_input input;
    vcore_lock_report locks;
    DWORD status;
    int best = -1;
    int best_i = -1;
    int i;
    int truncated = 0;
    char result_letter;

    memset(mounts, 0, sizeof(mounts));
    parent_dir(st->image, prog_dir, MAX_PATH);
    if (!prog_dir[0])
        return VETVDM_E_CMDLINE;
    if (!st->have_temp)
    {
        status = vcore_tempdir_create(st->temp);
        if (status != VETVDM_OK)
            return status;
        st->have_temp = 1;
    }
    if (!join_path(conf_path, cch_path, st->temp, L"dosbox-x.conf"))
        return VETVDM_E_INTERNAL;

    work[0] = 0;
    cd_rel[0] = 0;
    result[0] = 0;
    program_line[0] = 0;

    if (win31)
    {
        if (!st->win31_ok || !st->win31_root[0] || !st->wincom[0])
            return VETVDM_E_WIN31;
        status = add_mount(mounts, &n_mounts, 10, 'C', VCORE_MOUNT_DIR, st->win31_root);
        if (status != VETVDM_OK)
            return status;
        status = add_mount(mounts, &n_mounts, 10, 'D', VCORE_MOUNT_DIR, prog_dir);
        if (status != VETVDM_OK)
            return status;
    }
    else if (st->profile.n_mounts > 0)
    {
        for (i = 0; i < st->profile.n_mounts; i++)
        {
            WCHAR resolved[MAX_PATH];
            const vcore_mount *src = &st->profile.mounts[i];
            status = vcore_resolve_mount_path(st->profile.source_path, src->path,
                                            resolved, MAX_PATH);
            if (status != VETVDM_OK)
                return status;
            status = add_mount(mounts, &n_mounts, 10, (char)src->drive, src->type, resolved);
            if (status != VETVDM_OK)
                return status;
            if (src->type == VCORE_MOUNT_DIR)
            {
                int score = prefix_rel(resolved, prog_dir, rel, MAX_PATH);
                if (score > best)
                {
                    best = score;
                    best_i = n_mounts - 1;
                }
            }
        }
        if (best_i >= 0)
        {
            WCHAR resolved[MAX_PATH];
            work[0] = upper_drive(mounts[best_i].drive);
            work[1] = 0;
            status = vcore_resolve_mount_path(st->profile.source_path,
                                            st->profile.mounts[best_i].path,
                                            resolved, MAX_PATH);
            if (status != VETVDM_OK)
                return status;
            rel[0] = 0;
            prefix_rel(resolved, prog_dir, rel, MAX_PATH);
            if (rel[0])
            {
                if (!oem_ok(rel))
                {
                    st->why = "program name is not usable in DOS";
                    return VETVDM_E_CMDLINE;
                }
                if (vcore_wide_to_utf8(rel, cd_rel, sizeof(cd_rel)) != VETVDM_OK)
                    return VETVDM_E_CMDLINE;
            }
        }
        else
        {
            for (i = 0; i < n_mounts; i++)
            {
                if (mounts[i].type == VCORE_MOUNT_DIR)
                {
                    work[0] = upper_drive(mounts[i].drive);
                    work[1] = 0;
                    break;
                }
            }
        }
    }
    else
    {
        status = add_mount(mounts, &n_mounts, 10, 'C', VCORE_MOUNT_DIR, prog_dir);
        if (status != VETVDM_OK)
            return status;
        work[0] = 'C';
        work[1] = 0;
    }

    result_letter = pick_result_drive(mounts, n_mounts);
    if (!result_letter)
        return VETVDM_E_INTERNAL;
    status = add_mount(mounts, &n_mounts, 10, result_letter, VCORE_MOUNT_DIR, st->temp);
    if (status != VETVDM_OK)
        return status;
    result[0] = result_letter;
    result[1] = 0;

    status = fill_args(st, args_utf, sizeof(args_utf));
    if (status != VETVDM_OK)
    {
        if (status == VETVDM_E_CMDLINE)
            st->why = "program name is not usable in DOS";
        return status;
    }

    if (win31)
    {
        char game_utf[MAX_PATH * 3];
        char game_cmd[600];
        WCHAR game_wide[MAX_PATH];
        dos_guest_name(st, guest, MAX_PATH);
        if (!oem_ok(guest) || !oem_ok(st->wincom))
        {
            st->why = "program name is not usable in DOS";
            return VETVDM_E_CMDLINE;
        }
        if (vcore_w_len(guest) + 4 >= MAX_PATH)
            return VETVDM_E_CMDLINE;
        game_wide[0] = L'D';
        game_wide[1] = L':';
        game_wide[2] = L'\\';
        vcore_w_copy(game_wide + 3, MAX_PATH - 3, guest);
        if (vcore_wide_to_utf8(game_wide, game_utf, sizeof(game_utf)) != VETVDM_OK)
            return VETVDM_E_CMDLINE;
        status = vcore_dos_command(game_utf, args_utf, game_cmd, sizeof(game_cmd), &truncated);
        if (status != VETVDM_OK)
            return status;
        if (vcore_wide_to_utf8(st->wincom, guest_utf, sizeof(guest_utf)) != VETVDM_OK)
            return VETVDM_E_CMDLINE;
        if (vcore_cstr_len(guest_utf) + 1 + vcore_cstr_len(game_cmd) + 1 > sizeof(program_line))
            return VETVDM_E_CMDLINE;
        vcore_cstr_copy(program_line, sizeof(program_line), guest_utf);
        {
            unsigned used = vcore_cstr_len(program_line);
            program_line[used++] = ' ';
            vcore_cstr_copy(program_line + used, sizeof(program_line) - used, game_cmd);
        }
        work[0] = 0;
        cd_rel[0] = 0;
    }
    else
    {
        dos_guest_name(st, guest, MAX_PATH);
        if (!oem_ok(guest))
        {
            st->why = "program name is not usable in DOS";
            return VETVDM_E_CMDLINE;
        }
        if (vcore_wide_to_utf8(guest, guest_utf, sizeof(guest_utf)) != VETVDM_OK)
            return VETVDM_E_CMDLINE;
        status = vcore_dos_command(guest_utf, args_utf, program_line, sizeof(program_line),
                                  &truncated);
        if (status != VETVDM_OK)
            return status;
    }
    if (truncated)
        warn_add(st->warn, sizeof(st->warn), "truncated-tail");

    if (vcore_wide_to_utf8(st->temp, logfile, sizeof(logfile)) != VETVDM_OK)
        return VETVDM_E_CMDLINE;
    {
        unsigned used = vcore_cstr_len(logfile);
        const char *tail = "/dosbox.log";
        unsigned k;
        if (used && logfile[used - 1] == '\\')
            used--;
        if (used + 11 >= sizeof(logfile))
            return VETVDM_E_CMDLINE;
        for (k = 0; tail[k]; k++)
            logfile[used++] = (tail[k] == '/') ? '\\' : tail[k];
        logfile[used] = 0;
    }

    if (st->policy.conf_template[0])
    {
        char *buf = (char *)malloc(256u * 1024u);
        DWORD got = 0;
        int rc;
        if (!buf)
            return VETVDM_E_INTERNAL;
        rc = vetvdm_read_file(NULL, st->policy.conf_template, buf, 256u * 1024u, &got);
        if (rc == 1)
        {
            template_text = buf;
            buf[got] = 0;
        }
        else
        {
            free(buf);
            if (rc < 0)
                warn_add(st->warn, sizeof(st->warn), "template-unreadable");
        }
    }

    memset(&input, 0, sizeof(input));
    input.template_text = template_text;
    input.profile = &st->profile;
    input.mounts = mounts;
    input.n_mounts = n_mounts;
    input.workdir_drive = work[0] ? work : NULL;
    input.cd_rel = cd_rel[0] ? cd_rel : NULL;
    input.program_line = program_line;
    input.args_for_profile = args_utf;
    input.use_profile_autoexec = (!win31 && st->profile.n_autoexec > 0) ? 1 : 0;
    input.result_drive = result;
    input.logfile = logfile;
    status = vcore_conf_build(&input, conf, &locks);
    free(template_text);
    if (status != VETVDM_OK)
        return status;
    if (locks.forced_hostkey)
        warn_add(st->warn, sizeof(st->warn), "locked-hostkey");
    if (locks.stripped_mapperfile)
        warn_add(st->warn, sizeof(st->warn), "stripped-mapperfile");
    return VETVDM_OK;
}

static DWORD
write_conf(const WCHAR *path, const char *text)
{
    HANDLE file;
    DWORD len = 0;
    DWORD wrote = 0;
    if (!text)
        return VETVDM_E_INTERNAL;
    while (text[len])
        len++;
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                      FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return VETVDM_E_INTERNAL;
    if (len && (!WriteFile(file, text, len, &wrote, NULL) || wrote != len))
    {
        CloseHandle(file);
        return VETVDM_E_INTERNAL;
    }
    CloseHandle(file);
    return VETVDM_OK;
}

static int
wappend(WCHAR *dst, DWORD cap, DWORD *used, const WCHAR *src)
{
    DWORD n = 0;
    DWORD i;
    if (!src)
        return 1;
    while (src[n])
        n++;
    if (*used + n + 1 > cap)
        return 0;
    for (i = 0; i < n; i++)
        dst[*used + i] = src[i];
    *used += n;
    dst[*used] = 0;
    return 1;
}

static WCHAR *
build_command(const host *st, int layer, const WCHAR *conf_path)
{
    WCHAR *cmd;
    WCHAR quoted[MAX_PATH * 2];
    DWORD used = 0;
    const WCHAR *app = NULL;

    cmd = (WCHAR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                            VETVDM_CMDLINE_CCH * sizeof(WCHAR));
    if (!cmd)
        return NULL;

    if (layer == VETVDM_LAYER_DOSBOXX || layer == VETVDM_LAYER_WIN31)
    {
        const WCHAR *exe = st->dosbox[0] ? st->dosbox : L"dosbox-x.exe";
        if (vetvdm_quote_win32(exe, quoted, MAX_PATH * 2) != VETVDM_OK ||
            !wappend(cmd, VETVDM_CMDLINE_CCH, &used, quoted) ||
            !wappend(cmd, VETVDM_CMDLINE_CCH, &used, L" -conf ") ||
            !conf_path ||
            vetvdm_quote_win32(conf_path, quoted, MAX_PATH * 2) != VETVDM_OK ||
            !wappend(cmd, VETVDM_CMDLINE_CCH, &used, quoted) ||
            !wappend(cmd, VETVDM_CMDLINE_CCH, &used,
                    L" -exit -nopromptfolder -fastlaunch"))
        {
            HeapFree(GetProcessHeap(), 0, cmd);
            return NULL;
        }
        return cmd;
    }

    if (layer == VETVDM_LAYER_WINEVDM)
    {
        WCHAR image_q[MAX_PATH * 2];
        const WCHAR *exe = st->wine[0] ? st->wine : L"otvdmw.exe";
        if (vetvdm_quote_win32(exe, quoted, MAX_PATH * 2) != VETVDM_OK ||
            vetvdm_quote_win32(st->image, image_q, MAX_PATH * 2) != VETVDM_OK ||
            !wappend(cmd, VETVDM_CMDLINE_CCH, &used, quoted) ||
            !wappend(cmd, VETVDM_CMDLINE_CCH, &used, L" ") ||
            !wappend(cmd, VETVDM_CMDLINE_CCH, &used, image_q))
        {
            HeapFree(GetProcessHeap(), 0, cmd);
            return NULL;
        }
        if (st->cmd.args && st->cmd.args[0])
        {
            if (!wappend(cmd, VETVDM_CMDLINE_CCH, &used, L" ") ||
                !wappend(cmd, VETVDM_CMDLINE_CCH, &used, st->cmd.args))
            {
                HeapFree(GetProcessHeap(), 0, cmd);
                return NULL;
            }
        }
        return cmd;
    }

    if (st->cmd.tail && st->cmd.tail[0])
    {
        if (!wappend(cmd, VETVDM_CMDLINE_CCH, &used, st->cmd.tail))
        {
            HeapFree(GetProcessHeap(), 0, cmd);
            return NULL;
        }
        return cmd;
    }
    app = st->image;
    if (vetvdm_quote_win32(app, quoted, MAX_PATH * 2) != VETVDM_OK ||
        !wappend(cmd, VETVDM_CMDLINE_CCH, &used, quoted))
    {
        HeapFree(GetProcessHeap(), 0, cmd);
        return NULL;
    }
    return cmd;
}

static DWORD
read_errorlevel(const WCHAR *dir, DWORD *code)
{
    WCHAR path[MAX_PATH];
    HANDLE file;
    char buf[64];
    DWORD got = 0;
    unsigned i = 0;
    unsigned value = 0;
    int any = 0;

    if (!join_path(path, MAX_PATH, dir, L"ERRORLVL.TXT"))
        return VETVDM_E_NORESULT;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                      NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return VETVDM_E_NORESULT;
    if (!ReadFile(file, buf, sizeof(buf) - 1, &got, NULL))
    {
        CloseHandle(file);
        return VETVDM_E_NORESULT;
    }
    CloseHandle(file);
    buf[got] = 0;
    while (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\r' || buf[i] == '\n')
        i++;
    while (buf[i] >= '0' && buf[i] <= '9')
    {
        any = 1;
        value = value * 10u + (unsigned)(buf[i] - '0');
        if (value > 255u)
            return VETVDM_E_NORESULT;
        i++;
    }
    if (!any)
        return VETVDM_E_NORESULT;
    *code = value;
    return VETVDM_OK;
}

static void
remember_choice(host *st)
{
    WCHAR dir[MAX_PATH];
    WCHAR file[MAX_PATH];
    WCHAR hash[80];
    char body[256];
    HANDLE out;
    DWORD wrote = 0;
    unsigned n;

    if (!st->sha[0] || st->policy.n_profile_dirs < 1)
    {
        warn_add(st->warn, sizeof(st->warn), "remember-skipped");
        return;
    }
    if (vcore_utf8_to_wide(st->sha, hash, 70) != VETVDM_OK)
        return;
    n = vcore_w_len(hash);
    if (n + 6 >= 80)
        return;
    hash[n++] = L'.';
    hash[n++] = L'j';
    hash[n++] = L's';
    hash[n++] = L'o';
    hash[n++] = L'n';
    hash[n] = 0;
    if (!join_path(dir, MAX_PATH, st->policy.profile_dirs[0], L"by-hash"))
        return;
    CreateDirectoryW(st->policy.profile_dirs[0], NULL);
    CreateDirectoryW(dir, NULL);
    if (!join_path(file, MAX_PATH, dir, hash))
        return;
    _snprintf(body, sizeof(body),
             "{\n  \"id\": \"remember\",\n  \"layer\": \"%s\",\n"
             "  \"match\": { \"sha256\": \"%s\" }\n}\n",
             vetvdm_layer_name(st->layer), st->sha);
    body[sizeof(body) - 1] = 0;
    out = CreateFileW(file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                     FILE_ATTRIBUTE_NORMAL, NULL);
    if (out == INVALID_HANDLE_VALUE)
    {
        warn_add(st->warn, sizeof(st->warn), "remember-failed");
        return;
    }
    n = 0;
    while (body[n])
        n++;
    if (!WriteFile(out, body, n, &wrote, NULL) || wrote != n)
        warn_add(st->warn, sizeof(st->warn), "remember-failed");
    CloseHandle(out);
}

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
mkdir_parents(const WCHAR *file)
{
    WCHAR dir[MAX_PATH];
    unsigned n;
    unsigned i;
    parent_dir(file, dir, MAX_PATH);
    n = vcore_w_len(dir);
    for (i = 0; i < n; i++)
    {
        if (dir[i] != L'\\')
            continue;
        if (i > 0 && dir[i - 1] == L':')
            continue;
        if (i == 0 || (i == 1 && dir[0] == L'\\'))
            continue;
        dir[i] = 0;
        CreateDirectoryW(dir, NULL);
        dir[i] = L'\\';
    }
    if (dir[0])
        CreateDirectoryW(dir, NULL);
}

static void
write_log(host *st, DWORD code)
{
    vcore_log_rec rec;
    vcore_buf line;
    SYSTEMTIME now;
    char program[1024];
    char kind_buf[16];
    const char *caller;
    WCHAR path[MAX_PATH];
    HANDLE file;
    DWORD wrote = 0;
    DWORD end;
    WIN32_FILE_ATTRIBUTE_DATA info;

    if (st->skip_log || st->log_level == 0)
        return;
    if (st->cmd.dry_run && st->log_level < 2)
        return;
    if (st->cmd.log_path[0])
        vcore_w_copy(path, MAX_PATH, st->cmd.log_path);
    else if (st->have_roots &&
             vetvdm_path_succeeded(vetvdm_path_resolve(
                 &st->roots, VETVDM_PATH_SHARED_LOG, NULL, path, MAX_PATH,
                 NULL, TRUE, NULL, NULL)))
        ;
    else
        return;

    if (st->image[0])
    {
        if (vcore_wide_to_utf8(st->image, program, sizeof(program)) != VETVDM_OK)
            vcore_cstr_copy(program, sizeof(program), "-");
    }
    else if (st->cmd.image[0])
    {
        if (vcore_wide_to_utf8(st->cmd.image, program, sizeof(program)) != VETVDM_OK)
            vcore_cstr_copy(program, sizeof(program), "-");
    }
    else
        vcore_cstr_copy(program, sizeof(program), "-");

    if (st->cmd.from_hook)
        caller = "hook";
    else if (st->cmd.profile[0])
        caller = "shell";
    else
        caller = "cli";

    vcore_cstr_copy(kind_buf, sizeof(kind_buf), vetvdm_kind_name(st->kind));
    GetLocalTime(&now);
    end = GetTickCount();
    memset(&rec, 0, sizeof(rec));
    rec.year = now.wYear;
    rec.month = now.wMonth;
    rec.day = now.wDay;
    rec.hour = now.wHour;
    rec.min = now.wMinute;
    rec.sec = now.wSecond;
    rec.pid = GetCurrentProcessId();
    rec.caller = caller;
    rec.program = program;
    rec.sha256 = st->sha[0] ? st->sha : NULL;
    rec.kind = kind_buf;
    rec.layer = vetvdm_layer_name(st->layer);
    rec.profile_id = st->profile.id[0] ? st->profile.id : NULL;
    rec.exit_code = code;
    rec.duration_ms = end - st->started;
    rec.warn = st->warn[0] ? st->warn : NULL;
    if (vcore_log_format(&rec, &line) != VETVDM_OK)
        return;

    if (GetFileAttributesExW(path, GetFileExInfoStandard, &info))
    {
        if (info.nFileSizeHigh != 0 || info.nFileSizeLow >= VETVDM_LOG_ROTATE)
        {
            WCHAR rotated[MAX_PATH];
            unsigned n = vcore_w_len(path);
            if (n + 3 < MAX_PATH)
            {
                unsigned k;
                for (k = 0; k < n; k++)
                    rotated[k] = path[k];
                rotated[n] = L'.';
                rotated[n + 1] = L'1';
                rotated[n + 2] = 0;
                MoveFileExW(path, rotated, MOVEFILE_REPLACE_EXISTING);
            }
        }
    }
    mkdir_parents(path);
    file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                      OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        vcore_buf_free(&line);
        return;
    }
    WriteFile(file, line.data, (DWORD)line.len, &wrote, NULL);
    CloseHandle(file);
    vcore_buf_free(&line);
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

static DWORD
resolve_image(host *st)
{
    DWORD status;
    DWORD attr;
    DWORD header_len = 0;
    int kind;

    status = full_path(st->cmd.image, st->image, MAX_PATH);
    if (status != VETVDM_OK)
    {
        st->why = "bad image path";
        return VETVDM_E_NOPROG;
    }
    attr = GetFileAttributesW(st->image);
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY))
    {
        st->why = "program not found";
        return VETVDM_E_NOPROG;
    }
    status = vetvdm_load_image_info(st->image, &st->header, &header_len,
                                  st->sha, &st->file_size);
    if (status != VETVDM_OK)
    {
        st->why = "program not found";
        return status;
    }
    st->cls = vetvdm_classify_image(st->header, header_len);
    kind = vetvdm_kind_for_image(st->cmd.kind, st->cls, st->image);
    if (kind == VETVDM_KIND_AUTO)
    {
        if (st->cls == VETVDM_IMG_WIN16)
            kind = VETVDM_KIND_WIN16;
        else if (st->cls == VETVDM_IMG_DOS_MZ || st->cls == VETVDM_IMG_DOS_LE ||
                 st->cls == VETVDM_IMG_DOS_LX)
            kind = VETVDM_KIND_DOS;
    }
    st->kind = kind;

    if (st->cmd.cwd[0])
    {
        status = full_path(st->cmd.cwd, st->cwd, MAX_PATH);
        if (status != VETVDM_OK)
        {
            st->why = "bad cwd";
            return VETVDM_E_CMDLINE;
        }
    }
    else if (st->cmd.from_hook)
    {
        DWORD n = GetCurrentDirectoryW(MAX_PATH, st->cwd);
        if (n == 0 || n >= MAX_PATH)
            st->cwd[0] = 0;
    }
    else
        parent_dir(st->image, st->cwd, MAX_PATH);
    return VETVDM_OK;
}

static DWORD
route_image(host *st)
{
    const WCHAR *dirs[VCORE_PROFILE_DIRS_MAX];
    WCHAR profile_layer[16];
    vetvdm_route_input in;
    vetvdm_route_result out;
    DWORD status;
    int i;
    int remember = 0;

    for (i = 0; i < st->policy.n_profile_dirs && i < VCORE_PROFILE_DIRS_MAX; i++)
        dirs[i] = st->policy.profile_dirs[i];
    status = vcore_profile_lookup(dirs, st->policy.n_profile_dirs, st->image,
                                st->cmd.profile[0] ? st->cmd.profile : NULL,
                                st->file_size, st->sha, vetvdm_read_file, NULL,
                                &st->profile);
    if (status != VETVDM_OK)
    {
        st->why = "profile error";
        return status;
    }
    profile_layer[0] = 0;
    if (st->profile.layer[0])
        vcore_utf8_to_wide(st->profile.layer, profile_layer, 16);

    memset(&in, 0, sizeof(in));
    in.image = st->cls;
    in.kind = st->kind;
    in.forced_layer = st->cmd.layer;
    in.dos_mode = st->policy.dos_mode;
    in.win16_mode = st->policy.win16_mode;
    in.profile_layer = profile_layer;
    in.passthrough = passthrough_hit(&st->policy, st->image, st->sha);
    status = vetvdm_decide_route(&in, &out);
    if (status != VETVDM_OK)
    {
        st->why = (st->cls == VETVDM_IMG_PE) ? "not a DOS or Win16 image" : "not a DOS or Win16 image";
        st->layer = VETVDM_LAYER_NONE;
        return status;
    }
    st->layer = out.layer;
    st->forced = (st->cmd.layer != VETVDM_LAYER_NONE) ? 1 : 0;

    if (out.ask && !st->cmd.dry_run)
    {
        int choice = vetvdm_ask(st->kind == VETVDM_KIND_WIN16, &remember);
        if (choice == 0)
        {
            st->why = "cancelled";
            return VETVDM_E_CANCEL;
        }
        if (choice == 3)
            warn_add(st->warn, sizeof(st->warn), "ask-no-desktop");
        else
        {
            if (st->kind == VETVDM_KIND_WIN16)
                st->layer = (choice == 1) ? VETVDM_LAYER_WINEVDM : VETVDM_LAYER_WIN31;
            else
                st->layer = (choice == 1) ? VETVDM_LAYER_DOSBOXX : VETVDM_LAYER_NTVDM;
            st->forced = 1;
            if (remember)
                remember_choice(st);
        }
    }
    else if (out.ask)
        warn_add(st->warn, sizeof(st->warn), "ask");
    return VETVDM_OK;
}

static void
locate_layers(host *st)
{
    locate_dosbox(st->policy.dosboxx_path, st->dosbox, MAX_PATH, &st->dosbox_ok);
    locate_wine(st->policy.winevdm_path, st->wine, MAX_PATH, &st->wine_ok);
    vcore_w_copy(st->win31_root, MAX_PATH, st->policy.win31_path);
    st->win31_ok = win31_probe(st->win31_root, st->wincom, 64);
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

int WINAPI
wWinMain(HINSTANCE instance, HINSTANCE previous, LPWSTR command, int show)
{
    (void)instance;
    (void)previous;
    (void)command;
    (void)show;
    ExitProcess(run());
    return 0;
}
