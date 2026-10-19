/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/core/conf.c
 * PURPOSE:  DOSBox-X conf generation and locked defaults.
 *
 * Copyright (C) 2026 VetTV
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * Pure: template text and a profile in, conf text out. No file I/O.
 * Locked defaults run after the profile overlay and cannot be skipped.
 * hostkey is written under [dosbox], never under [sdl].
 */

#include "vetvdm_core.h"

#include "vcore_util.h"

#include <stdlib.h>
#include <string.h>

#define SEC_MAX 20
#define KEY_MAX 24
#define NAME_MAX 32
#define KEYN_MAX 48
#define VAL_MAX 800
#define LINE_MAX 24
#define LINE_LEN 1024

typedef struct conf_section
{
    char name[NAME_MAX];
    char key[KEY_MAX][KEYN_MAX];
    char val[KEY_MAX][VAL_MAX];
    int nkeys;
} conf_section;

typedef struct conf_doc
{
    conf_section sec[SEC_MAX];
    int nsec;
    char line[LINE_MAX][LINE_LEN];
    int nlines;
    char result_drive;
    unsigned dropped_autoexec;
    DWORD status;
} conf_doc;

static void
lower_copy(char *dst, unsigned cap, const char *src)
{
    unsigned i = 0;
    if (!dst || cap == 0)
        return;
    if (!src)
    {
        dst[0] = 0;
        return;
    }
    while (src[i] && i + 1 < cap)
    {
        char ch = src[i];
        if (ch >= 'A' && ch <= 'Z')
            ch = (char)(ch - 'A' + 'a');
        dst[i++] = ch;
    }
    dst[i] = 0;
}

static conf_section *
find_sec(conf_doc *doc, const char *name, int create)
{
    char folded[NAME_MAX];
    int i;
    lower_copy(folded, NAME_MAX, name);
    for (i = 0; i < doc->nsec; i++)
    {
        if (vcore_cstr_ieq(doc->sec[i].name, folded))
            return &doc->sec[i];
    }
    if (!create)
        return NULL;
    if (doc->nsec >= SEC_MAX || !folded[0])
    {
        doc->status = VETVDM_E_INTERNAL;
        return NULL;
    }
    memset(&doc->sec[doc->nsec], 0, sizeof(doc->sec[0]));
    vcore_cstr_copy(doc->sec[doc->nsec].name, NAME_MAX, folded);
    return &doc->sec[doc->nsec++];
}

static void
set_key(conf_doc *doc, const char *section, const char *key, const char *value)
{
    conf_section *sec;
    int i;
    if (doc->status != VETVDM_OK)
        return;
    if (!key || !key[0] || !value)
    {
        doc->status = VETVDM_E_INTERNAL;
        return;
    }
    if (vcore_cstr_len(key) >= KEYN_MAX || vcore_cstr_len(value) >= VAL_MAX)
    {
        doc->status = VETVDM_E_INTERNAL;
        return;
    }
    sec = find_sec(doc, section, 1);
    if (!sec)
        return;
    for (i = 0; i < sec->nkeys; i++)
    {
        if (vcore_cstr_ieq(sec->key[i], key))
        {
            vcore_cstr_copy(sec->val[i], VAL_MAX, value);
            return;
        }
    }
    if (sec->nkeys >= KEY_MAX)
    {
        doc->status = VETVDM_E_INTERNAL;
        return;
    }
    lower_copy(sec->key[sec->nkeys], KEYN_MAX, key);
    vcore_cstr_copy(sec->val[sec->nkeys], VAL_MAX, value);
    sec->nkeys++;
}

static int
is_mapperfile_key(const char *key)
{
    static const char prefix[] = "mapperfile";
    unsigned i;
    if (!key)
        return 0;
    for (i = 0; prefix[i]; i++)
    {
        char ch = key[i];
        if (ch >= 'A' && ch <= 'Z')
            ch = (char)(ch - 'A' + 'a');
        if (ch != prefix[i])
            return 0;
    }
    return 1;
}

static void
apply_defaults(conf_doc *doc)
{
    set_key(doc, "sdl", "autolock", "true");
    set_key(doc, "sdl", "fullscreen", "true");
    set_key(doc, "sdl", "output", "surface");
    set_key(doc, "dosbox", "machine", "svga_s3");
    set_key(doc, "dosbox", "memsize", "16");
    set_key(doc, "dosbox", "quit warning", "false");
    set_key(doc, "cpu", "cycles", "auto");
    set_key(doc, "sblaster", "sbtype", "sb16");
}

static const char *
skip_ws(const char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    return s;
}

static void
trim_inplace(char *text)
{
    char *end;
    char *start = text;
    while (*start == ' ' || *start == '\t')
        start++;
    if (start != text)
    {
        char *d = text;
        while (*start)
            *d++ = *start++;
        *d = 0;
    }
    end = text + vcore_cstr_len(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r'))
        *--end = 0;
}

static void
parse_template(conf_doc *doc, const char *text)
{
    char section[NAME_MAX];
    int have = 0;
    const char *cursor = text;

    section[0] = 0;
    if (!text)
        return;
    while (*cursor)
    {
        char line[512];
        unsigned n = 0;
        const char *eq;
        while (*cursor && *cursor != '\n')
        {
            if (n + 1 < sizeof(line))
                line[n++] = *cursor;
            else
            {
                doc->status = VETVDM_E_INTERNAL;
                return;
            }
            cursor++;
        }
        line[n] = 0;
        if (*cursor == '\n')
            cursor++;
        trim_inplace(line);
        if (!line[0] || line[0] == '#' || line[0] == ';')
            continue;
        if (line[0] == '[')
        {
            char *close = line + 1;
            while (*close && *close != ']')
                close++;
            if (*close != ']')
            {
                doc->status = VETVDM_E_INTERNAL;
                return;
            }
            *close = 0;
            lower_copy(section, NAME_MAX, line + 1);
            have = 1;
            if (vcore_cstr_ieq(section, "autoexec"))
                doc->dropped_autoexec++;
            continue;
        }
        if (have && vcore_cstr_ieq(section, "autoexec"))
        {
            doc->dropped_autoexec++;
            continue;
        }
        if (!have)
        {
            doc->status = VETVDM_E_INTERNAL;
            return;
        }
        eq = line;
        while (*eq && *eq != '=')
            eq++;
        if (*eq != '=')
        {
            doc->status = VETVDM_E_INTERNAL;
            return;
        }
        {
            char key[KEYN_MAX];
            char val[VAL_MAX];
            unsigned kn = (unsigned)(eq - line);
            if (kn >= KEYN_MAX)
            {
                doc->status = VETVDM_E_INTERNAL;
                return;
            }
            memcpy(key, line, kn);
            key[kn] = 0;
            trim_inplace(key);
            vcore_cstr_copy(val, VAL_MAX, skip_ws(eq + 1));
            trim_inplace(val);
            set_key(doc, section, key, val);
        }
    }
}

static void
apply_profile(conf_doc *doc, const vcore_profile *prof)
{
    int i;
    if (!prof)
        return;
    if (prof->machine[0])
        set_key(doc, "dosbox", "machine", prof->machine);
    if (prof->memsize[0])
        set_key(doc, "dosbox", "memsize", prof->memsize);
    if (prof->cycles[0])
        set_key(doc, "cpu", "cycles", prof->cycles);
    if (prof->sbtype[0])
        set_key(doc, "sblaster", "sbtype", prof->sbtype);
    if (prof->gus[0])
        set_key(doc, "gus", "gus", prof->gus);
    if (prof->mpu401[0])
        set_key(doc, "midi", "mpu401", prof->mpu401);
    if (prof->output[0])
        set_key(doc, "sdl", "output", prof->output);
    if (prof->fullscreen[0])
        set_key(doc, "sdl", "fullscreen", prof->fullscreen);
    for (i = 0; i < prof->n_extra; i++)
        set_key(doc, prof->extra_sec[i], prof->extra_key[i], prof->extra_val[i]);
}

static void
apply_locks(conf_doc *doc, vcore_lock_report *locks)
{
    int s;
    int i;
    conf_section *dos;
    const char *prev = NULL;

    for (s = 0; s < doc->nsec; s++)
    {
        conf_section *sec = &doc->sec[s];
        for (i = 0; i < sec->nkeys; )
        {
            if (is_mapperfile_key(sec->key[i]) ||
                (vcore_cstr_ieq(sec->name, "sdl") && vcore_cstr_ieq(sec->key[i], "hostkey")))
            {
                int k;
                if (locks && is_mapperfile_key(sec->key[i]))
                    locks->stripped_mapperfile++;
                for (k = i + 1; k < sec->nkeys; k++)
                {
                    memcpy(sec->key[k - 1], sec->key[k], KEYN_MAX);
                    memcpy(sec->val[k - 1], sec->val[k], VAL_MAX);
                }
                sec->nkeys--;
                continue;
            }
            i++;
        }
    }

    dos = find_sec(doc, "dosbox", 1);
    if (!dos)
        return;
    for (i = 0; i < dos->nkeys; i++)
    {
        if (vcore_cstr_ieq(dos->key[i], "hostkey"))
        {
            prev = dos->val[i];
            break;
        }
    }
    if (!prev || !vcore_cstr_ieq(prev, "mapper"))
    {
        if (locks)
            locks->forced_hostkey++;
    }
    set_key(doc, "dosbox", "hostkey", "mapper");
}

static int
add_line(conf_doc *doc, const char *text)
{
    if (doc->status != VETVDM_OK)
        return 0;
    if (!text)
        return 0;
    if (doc->nlines >= LINE_MAX || vcore_cstr_len(text) >= LINE_LEN)
    {
        doc->status = VETVDM_E_INTERNAL;
        return 0;
    }
    vcore_cstr_copy(doc->line[doc->nlines], LINE_LEN, text);
    doc->nlines++;
    return 1;
}

static int
line_is_exit(const char *text)
{
    char buf[64];
    vcore_cstr_copy(buf, sizeof(buf), text ? text : "");
    trim_inplace(buf);
    return vcore_cstr_ieq(buf, "exit");
}

static void
expand_args(const char *line, const char *args, char *out, unsigned cap)
{
    unsigned used = 0;
    const char *cursor = line ? line : "";
    if (!args)
        args = "";
    out[0] = 0;
    while (*cursor && used + 1 < cap)
    {
        if (cursor[0] == '%' && cursor[1] == 'A' && cursor[2] == 'R' &&
            cursor[3] == 'G' && cursor[4] == 'S' && cursor[5] == '%')
        {
            const char *a = args;
            while (*a && used + 1 < cap)
                out[used++] = *a++;
            cursor += 6;
            continue;
        }
        out[used++] = *cursor++;
    }
    out[used] = 0;
}

static void
build_autoexec(conf_doc *doc, const vcore_conf_input *in)
{
    int i;
    char buf[LINE_LEN];

    for (i = 0; i < in->n_mounts; i++)
    {
        const vcore_mount_spec *mnt = &in->mounts[i];
        const char *verb = "mount";
        const char *tail = "";
        char drive;
        if (!mnt->path[0] || vcore_cstr_len(mnt->path) > 900)
        {
            doc->status = VETVDM_E_INTERNAL;
            return;
        }
        if (strchr(mnt->path, '"'))
        {
            doc->status = VETVDM_E_CMDLINE;
            return;
        }
        drive = mnt->drive;
        if (drive >= 'a' && drive <= 'z')
            drive = (char)(drive - 'a' + 'A');
        if (drive < 'A' || drive > 'Z')
        {
            doc->status = VETVDM_E_INTERNAL;
            return;
        }
        if (mnt->type == VCORE_MOUNT_ISO)
        {
            verb = "imgmount";
            tail = " -t iso";
        }
        else if (mnt->type == VCORE_MOUNT_IMG)
        {
            verb = "imgmount";
            tail = " -t hdd";
        }
        {
            unsigned used = 0;
            const char *p = verb;
            const char *path = mnt->path;
            buf[0] = 0;
            while (*p && used + 1 < LINE_LEN)
                buf[used++] = *p++;
            if (used + 8 >= LINE_LEN)
            {
                doc->status = VETVDM_E_INTERNAL;
                return;
            }
            buf[used++] = ' ';
            buf[used++] = drive;
            buf[used++] = ' ';
            buf[used++] = '"';
            while (*path && used + 8 < LINE_LEN)
                buf[used++] = *path++;
            buf[used++] = '"';
            p = tail;
            while (*p && used + 1 < LINE_LEN)
                buf[used++] = *p++;
            buf[used] = 0;
            if (*path)
            {
                doc->status = VETVDM_E_INTERNAL;
                return;
            }
            add_line(doc, buf);
        }
    }

    if (in->workdir_drive && in->workdir_drive[0])
    {
        buf[0] = in->workdir_drive[0];
        if (buf[0] >= 'a' && buf[0] <= 'z')
            buf[0] = (char)(buf[0] - 'a' + 'A');
        buf[1] = ':';
        buf[2] = 0;
        add_line(doc, buf);
    }
    if (in->cd_rel && in->cd_rel[0] && !in->use_profile_autoexec)
    {
        unsigned used = 0;
        const char *p = in->cd_rel;
        memcpy(buf, "cd ", 3);
        used = 3;
        while (*p && used + 1 < LINE_LEN)
            buf[used++] = *p++;
        buf[used] = 0;
        add_line(doc, buf);
    }

    if (in->use_profile_autoexec && in->profile)
    {
        for (i = 0; i < in->profile->n_autoexec; i++)
        {
            if (line_is_exit(in->profile->autoexec[i]))
                continue;
            expand_args(in->profile->autoexec[i], in->args_for_profile, buf, LINE_LEN);
            add_line(doc, buf);
        }
    }
    else if (in->program_line && in->program_line[0])
        add_line(doc, in->program_line);

    /*
     * DOSBox-X 2026.10.01 does not expand %ERRORLEVEL% (it is an ordinary
     * environment variable; issue 2350). The ladder is emitted later, not
     * stored: `if errorlevel N` means >= N, so the tests run from 255 down.
     */
    if (in->result_drive && in->result_drive[0])
    {
        char drive = in->result_drive[0];
        if (drive >= 'a' && drive <= 'z')
            drive = (char)(drive - 'a' + 'A');
        doc->result_drive = drive;
    }
}

static const char *g_order[] = {
    "sdl", "dosbox", "render", "cpu", "mixer", "midi", "sblaster", "gus",
    "speaker", "joystick", "serial", "dos", "ipx", "ethernet", "ne2000",
    "log", "ttf", "keyboard", "capture", "vsync", NULL
};

static void
emit_section(vcore_buf *out, const conf_section *sec, int *first)
{
    int order[KEY_MAX];
    int i;
    int n = sec->nkeys;
    if (n <= 0)
        return;
    for (i = 0; i < n; i++)
        order[i] = i;
    for (i = 1; i < n; i++)
    {
        int key = order[i];
        int j = i;
        while (j > 0 &&
               vcore_cstr_icmp(sec->key[order[j - 1]], sec->key[key]) > 0)
        {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = key;
    }
    if (!*first)
        vcore_buf_puts(out, "\n");
    *first = 0;
    vcore_buf_puts(out, "[");
    vcore_buf_puts(out, sec->name);
    vcore_buf_puts(out, "]\n");
    for (i = 0; i < n; i++)
    {
        vcore_buf_puts(out, sec->key[order[i]]);
        vcore_buf_puts(out, "=");
        vcore_buf_puts(out, sec->val[order[i]]);
        vcore_buf_puts(out, "\n");
    }
}

static void
emit_ladder(vcore_buf *out, char drive)
{
    int n;

    if (drive >= 'a' && drive <= 'z')
        drive = (char)(drive - 'a' + 'A');
    if (drive < 'A' || drive > 'Z')
    {
        vcore_buf_puts(out, "exit\n");
        return;
    }
    for (n = 255; n >= 1; n--)
        vcore_buf_printf(out, "if errorlevel %d goto e%d\n", n, n);
    vcore_buf_printf(out, ">%c:\\ERRORLVL.TXT echo 0\ngoto edone\n", drive);
    for (n = 255; n >= 1; n--)
        vcore_buf_printf(out, ":e%d\n>%c:\\ERRORLVL.TXT echo %d\ngoto edone\n",
                        n, drive, n);
    vcore_buf_puts(out, ":edone\nexit\n");
}

static void
emit_doc(const conf_doc *doc, vcore_buf *out)
{
    int emitted[SEC_MAX];
    int i;
    int first = 1;
    int unknown[SEC_MAX];
    int nunknown = 0;

    for (i = 0; i < doc->nsec; i++)
        emitted[i] = 0;
    for (i = 0; g_order[i]; i++)
    {
        int s;
        for (s = 0; s < doc->nsec; s++)
        {
            if (!emitted[s] && vcore_cstr_ieq(doc->sec[s].name, g_order[i]))
            {
                emit_section(out, &doc->sec[s], &first);
                emitted[s] = 1;
            }
        }
    }
    for (i = 0; i < doc->nsec; i++)
    {
        if (!emitted[i] && !vcore_cstr_ieq(doc->sec[i].name, "autoexec"))
            unknown[nunknown++] = i;
    }
    for (i = 1; i < nunknown; i++)
    {
        int key = unknown[i];
        int j = i;
        while (j > 0 &&
               vcore_cstr_icmp(doc->sec[unknown[j - 1]].name, doc->sec[key].name) > 0)
        {
            unknown[j] = unknown[j - 1];
            j--;
        }
        unknown[j] = key;
    }
    for (i = 0; i < nunknown; i++)
        emit_section(out, &doc->sec[unknown[i]], &first);

    if (doc->nlines > 0 || doc->result_drive)
    {
        if (!first)
            vcore_buf_puts(out, "\n");
        vcore_buf_puts(out, "[autoexec]\n");
        for (i = 0; i < doc->nlines; i++)
        {
            vcore_buf_puts(out, doc->line[i]);
            vcore_buf_puts(out, "\n");
        }
        emit_ladder(out, doc->result_drive);
    }
}

const char *
vcore_locked_defaults(void)
{
    return "[dosbox] hostkey=mapper\n"
           "; no mapperfile\n"
           "; ctrl+f9=upstream\n"
           "; job=kill-on-close\n";
}

DWORD
vcore_conf_build(const vcore_conf_input *in, vcore_buf *conf_out,
                vcore_lock_report *locks)
{
    conf_doc *doc;
    DWORD status;

    if (locks)
        memset(locks, 0, sizeof(*locks));
    if (!in || !conf_out)
        return VETVDM_E_INTERNAL;
    vcore_buf_init(conf_out);

    doc = (conf_doc *)calloc(1, sizeof(*doc));
    if (!doc)
        return VETVDM_E_INTERNAL;
    doc->status = VETVDM_OK;

    if (in->template_text && in->template_text[0])
        parse_template(doc, in->template_text);
    else
        apply_defaults(doc);
    apply_profile(doc, in->profile);
    if (in->logfile && in->logfile[0])
        set_key(doc, "log", "logfile", in->logfile);
    apply_locks(doc, locks);
    if (locks)
        locks->dropped_template_autoexec = doc->dropped_autoexec;
    build_autoexec(doc, in);
    status = doc->status;
    if (status == VETVDM_OK)
    {
        emit_doc(doc, conf_out);
        if (!conf_out->data)
            status = VETVDM_E_INTERNAL;
    }
    free(doc);
    if (status != VETVDM_OK)
        vcore_buf_free(conf_out);
    return status;
}
