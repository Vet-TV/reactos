/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/core/vetvdm_core.h
 * PURPOSE:  Shared vetvdm core used by vetvdm.exe.
 *
 * Copyright (C) 2026 VetTV
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#ifndef VETVDM_CORE_H
#define VETVDM_CORE_H

#include "vcore_types.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Program exit codes are 0-255. vetvdm's own failures use the high range
 * from VETVDM_SPEC section 6 so they cannot collide with a DOS ERRORLEVEL.
 */
#define VETVDM_OK            0u
#define VETVDM_E_CMDLINE     0xE0000001u
#define VETVDM_E_NOPROG      0xE0000002u
#define VETVDM_E_NOTDOS      0xE0000003u
#define VETVDM_E_NOLAYER     0xE0000004u
#define VETVDM_E_START       0xE0000005u
#define VETVDM_E_WIN31       0xE0000006u
#define VETVDM_E_PROFILE     0xE0000007u
#define VETVDM_E_CANCEL      0xE0000008u
#define VETVDM_E_NORESULT    0xE0000009u
#define VETVDM_E_INTERNAL    0xE00000FFu

#define VETVDM_LOG_ROTATE    (1024u * 1024u)
#define VETVDM_DOS_TAIL_MAX  127u

#define VCORE_MOUNT_DIR 1
#define VCORE_MOUNT_ISO 2
#define VCORE_MOUNT_IMG 3

#define VCORE_SRC_NONE     0
#define VCORE_SRC_EXPLICIT 1
#define VCORE_SRC_GAMEJSON 2
#define VCORE_SRC_BYHASH   3
#define VCORE_SRC_BYNAME   4
#define VCORE_SRC_BUILTIN  5

#define VCORE_PROFILE_DIRS_MAX 8
#define VCORE_PASSTHROUGH_MAX  32

typedef struct vcore_buf
{
    char *data;
    size_t len;
    size_t cap;
} vcore_buf;

void vcore_buf_init(vcore_buf *buf);
void vcore_buf_free(vcore_buf *buf);
DWORD vcore_buf_append(vcore_buf *buf, const char *text, size_t len);
DWORD vcore_buf_puts(vcore_buf *buf, const char *text);
DWORD vcore_buf_printf(vcore_buf *buf, const char *fmt, ...);

typedef struct vcore_mount
{
    WCHAR drive;
    int type;
    WCHAR path[MAX_PATH];
} vcore_mount;

typedef struct vcore_profile
{
    char id[64];
    char layer[16];
    char guest_exe[MAX_PATH];
    char guest_args[160];
    char machine[32];
    char memsize[16];
    char cycles[32];
    char sbtype[32];
    char gus[8];
    char mpu401[32];
    char output[32];
    char fullscreen[8];
    vcore_mount mounts[8];
    int n_mounts;
    char autoexec[8][256];
    int n_autoexec;
    char extra_sec[12][32];
    char extra_key[12][48];
    char extra_val[12][160];
    int n_extra;
    char match_sha[65];
    DWORD match_size;
    int match_size_set;
    char match_name[MAX_PATH];
    int src;
    WCHAR source_path[MAX_PATH];
} vcore_profile;

/* 1 = read, 0 = not found, -1 = unreadable or too large for buf. */
typedef int (*vcore_read_fn)(void *ctx, const WCHAR *path,
                            char *buf, DWORD cap, DWORD *len);

DWORD vcore_profile_parse(const char *json, DWORD len, vcore_profile *out);

DWORD vcore_profile_lookup(const WCHAR *const *dirs, int n_dirs,
                          const WCHAR *program,
                          const WCHAR *explicit_profile,
                          DWORD file_size,
                          const char *sha256_hex,
                          vcore_read_fn readfn, void *ctx,
                          vcore_profile *out);

DWORD vcore_resolve_mount_path(const WCHAR *profile_file,
                              const WCHAR *mount_path,
                              WCHAR *out, DWORD cch);

const char *vcore_profile_src_name(int src);

typedef struct vcore_policy
{
    DWORD enabled;
    DWORD log_level;
    WCHAR dos_mode[16];
    WCHAR win16_mode[16];
    WCHAR dosboxx_path[MAX_PATH];
    WCHAR winevdm_path[MAX_PATH];
    WCHAR win31_path[MAX_PATH];
    WCHAR conf_template[MAX_PATH];
    WCHAR profile_dirs[VCORE_PROFILE_DIRS_MAX][MAX_PATH];
    int n_profile_dirs;
    WCHAR pass_through[VCORE_PASSTHROUGH_MAX][MAX_PATH];
    int n_pass;
} vcore_policy;

typedef struct vcore_expand_ctx
{
    const WCHAR *system_drive;    /* L"C:" */
    const WCHAR *system_root;     /* L"C:\\ReactOS" */
    const WCHAR *program_files;   /* L"C:\\Program Files" */
} vcore_expand_ctx;

typedef struct vcore_policy_source
{
    BOOL (*get_dword)(void *ctx, const WCHAR *name, DWORD *out);
    BOOL (*get_string)(void *ctx, const WCHAR *name, WCHAR *out, DWORD cch);
    BOOL (*get_multi)(void *ctx, const WCHAR *name, WCHAR *out, DWORD cch);
    void *ctx;
} vcore_policy_source;

DWORD vcore_expand_allowed(const vcore_expand_ctx *ctx,
                          const WCHAR *text, WCHAR *out, DWORD cch);

DWORD vcore_policy_load(const vcore_policy_source *machine,
                       const vcore_policy_source *user,
                       const vcore_policy *defaults,
                       const vcore_expand_ctx *expand,
                       vcore_policy *out);

typedef struct vcore_mount_spec
{
    char drive;
    int type;
    char path[1024];
} vcore_mount_spec;

typedef struct vcore_conf_input
{
    const char *template_text;
    const vcore_profile *profile;
    const vcore_mount_spec *mounts;
    int n_mounts;
    const char *workdir_drive;
    const char *cd_rel;
    const char *program_line;
    const char *args_for_profile;
    int use_profile_autoexec;
    const char *result_drive;
    const char *logfile;
} vcore_conf_input;

typedef struct vcore_lock_report
{
    unsigned stripped_mapperfile;
    unsigned forced_hostkey;
    unsigned dropped_template_autoexec;
} vcore_lock_report;

DWORD vcore_conf_build(const vcore_conf_input *in, vcore_buf *conf_out,
                      vcore_lock_report *locks);
const char *vcore_locked_defaults(void);

typedef struct vcore_dry_input
{
    const char *layer;
    const char *kind;
    const char *caller;
    const char *profile_id;
    const char *profile_src;
    const char *image;
    const char *sha256;
    const char *cwd;
    const char *cmdline;
    const char *conf;
    const char *note;
} vcore_dry_input;

DWORD vcore_dry_run_text(const vcore_dry_input *in, vcore_buf *out);

typedef struct vcore_log_rec
{
    int year, month, day, hour, min, sec;
    DWORD pid;
    const char *caller;
    const char *program;
    const char *sha256;
    const char *kind;
    const char *layer;
    const char *profile_id;
    DWORD exit_code;
    DWORD duration_ms;
    const char *dosbox;
    const char *wine;
    const char *wine_from;
    const char *warn;
} vcore_log_rec;

DWORD vcore_log_format(const vcore_log_rec *rec, vcore_buf *line_out);

DWORD vcore_dos_command(const char *program, const char *args,
                       char *out, size_t cap, int *truncated);

void vetvdm_sha256(const void *data, size_t len, unsigned char out[32]);
void vetvdm_sha256_hex(const unsigned char hash[32], char out[65]);

typedef struct vetvdm_sha256_ctx
{
    uint32_t state[8];
    uint64_t bits;
    unsigned char buf[64];
    unsigned nbuf;
} vetvdm_sha256_ctx;

void vetvdm_sha256_init(vetvdm_sha256_ctx *ctx);
void vetvdm_sha256_update(vetvdm_sha256_ctx *ctx, const void *data, size_t len);
void vetvdm_sha256_final(vetvdm_sha256_ctx *ctx, unsigned char out[32]);

DWORD vcore_wide_to_utf8(const WCHAR *text, char *out, DWORD cap);
DWORD vcore_utf8_to_wide(const char *text, WCHAR *out, DWORD cch);

#endif /* VETVDM_CORE_H */
