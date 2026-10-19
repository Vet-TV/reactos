/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/core/launch.c
 * PURPOSE:  Start a layer process inside a kill-on-close job object.
 *
 * Copyright (C) 2026 VetTV
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * CreateProcessW is CREATE_SUSPENDED so AssignProcessToJobObject runs
 * before the child can spawn. Closing the job kills DOSBox-X and its
 * children (JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE).
 */

#include "win32.h"

#include <string.h>

static const WCHAR g_bypass_name[] = L"VETTV_VDM_BYPASS";

WCHAR *
vcore_env_with_bypass(void)
{
    WCHAR *cur = GetEnvironmentStringsW();
    DWORD bytes = 0;
    const WCHAR *cursor;
    WCHAR *block;
    WCHAR *dest;
    int replaced = 0;

    if (cur)
    {
        cursor = cur;
        while (*cursor)
        {
            DWORD n = 0;
            while (cursor[n])
                n++;
            bytes += (n + 1) * sizeof(WCHAR);
            cursor += n + 1;
        }
    }
    bytes += (DWORD)(sizeof(g_bypass_name) + sizeof(L"=1"));
    bytes += sizeof(WCHAR) * 2;
    block = (WCHAR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes);
    if (!block)
    {
        if (cur)
            FreeEnvironmentStringsW(cur);
        return NULL;
    }
    dest = block;
    if (cur)
    {
        cursor = cur;
        while (*cursor)
        {
            const WCHAR *eq = cursor;
            DWORD n = 0;
            int skip = 0;
            while (eq[n] && eq[n] != L'=')
                n++;
            if (n == (sizeof(g_bypass_name) / sizeof(WCHAR)) - 1)
            {
                DWORD i;
                skip = 1;
                for (i = 0; i < n; i++)
                {
                    WCHAR a = eq[i];
                    WCHAR b = g_bypass_name[i];
                    if (a >= L'a' && a <= L'z')
                        a = (WCHAR)(a - L'a' + L'A');
                    if (a != b)
                    {
                        skip = 0;
                        break;
                    }
                }
            }
            while (cursor[n])
                n++;
            if (!skip)
            {
                DWORD i;
                for (i = 0; i < n; i++)
                    dest[i] = cursor[i];
                dest[n] = 0;
                dest += n + 1;
            }
            else
                replaced = 1;
            cursor += n + 1;
        }
    }
    {
        const WCHAR *src = L"VETTV_VDM_BYPASS=1";
        while (*src)
            *dest++ = *src++;
        *dest++ = 0;
    }
    *dest = 0;
    (void)replaced;
    if (cur)
        FreeEnvironmentStringsW(cur);
    return block;
}

void
vcore_env_free(WCHAR *env)
{
    if (env)
        HeapFree(GetProcessHeap(), 0, env);
}

DWORD
vcore_launch_in_job(const WCHAR *app, WCHAR *cmdline, const WCHAR *cwd,
                   WCHAR *env, vcore_job *out)
{
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    DWORD flags;

    if (!out || !app || !app[0] || !cmdline)
        return VETVDM_E_INTERNAL;
    out->job = NULL;
    out->process = NULL;
    out->thread = NULL;
    out->pid = 0;

    out->job = CreateJobObjectW(NULL, NULL);
    if (!out->job)
        return VETVDM_E_START;
    memset(&info, 0, sizeof(info));
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(out->job, JobObjectExtendedLimitInformation,
                                &info, sizeof(info)))
    {
        CloseHandle(out->job);
        out->job = NULL;
        return VETVDM_E_START;
    }

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));
    flags = CREATE_SUSPENDED;
    if (env)
        flags |= CREATE_UNICODE_ENVIRONMENT;
    if (!CreateProcessW(app, cmdline, NULL, NULL, FALSE, flags, env,
                       (cwd && cwd[0]) ? cwd : NULL, &si, &pi))
    {
        CloseHandle(out->job);
        out->job = NULL;
        return VETVDM_E_START;
    }
    if (!AssignProcessToJobObject(out->job, pi.hProcess))
    {
        TerminateProcess(pi.hProcess, VETVDM_E_START);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        CloseHandle(out->job);
        out->job = NULL;
        return VETVDM_E_START;
    }
    if (ResumeThread(pi.hThread) == (DWORD)-1)
    {
        TerminateProcess(pi.hProcess, VETVDM_E_START);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        CloseHandle(out->job);
        out->job = NULL;
        return VETVDM_E_START;
    }
    out->process = pi.hProcess;
    out->thread = pi.hThread;
    out->pid = pi.dwProcessId;
    return VETVDM_OK;
}

DWORD
vcore_job_wait(vcore_job *job, DWORD *exit_code)
{
    DWORD code = 1;
    if (!job || !job->process)
        return VETVDM_E_START;
    if (WaitForSingleObject(job->process, INFINITE) != WAIT_OBJECT_0)
        return VETVDM_E_START;
    if (!GetExitCodeProcess(job->process, &code))
        return VETVDM_E_START;
    if (exit_code)
        *exit_code = code;
    return VETVDM_OK;
}

void
vcore_job_close(vcore_job *job)
{
    if (!job)
        return;
    if (job->thread)
        CloseHandle(job->thread);
    if (job->process)
        CloseHandle(job->process);
    if (job->job)
        CloseHandle(job->job);
    job->thread = NULL;
    job->process = NULL;
    job->job = NULL;
}
