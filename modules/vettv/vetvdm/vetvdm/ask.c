/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/vetvdm/ask.c
 * PURPOSE:  Tiny chooser used when policy mode is ask.
 *
 * Copyright (C) 2026 VetTV
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * No dialog is shown when the window station is not visible. On XP and
 * ReactOS the interactive user is session 0, so this checks the window
 * station rather than the session id.
 */

#include "win32.h"

static int
desktop_visible(void)
{
    HWINSTA station;
    USEROBJECTFLAGS flags;
    DWORD needed = 0;

    station = GetProcessWindowStation();
    if (!station)
        return 0;
    if (!GetUserObjectInformationW(station, UOI_FLAGS, &flags, sizeof(flags), &needed))
        return 0;
    return (flags.dwFlags & WSF_VISIBLE) ? 1 : 0;
}

int
vetvdm_ask(int win16, int *remember)
{
    int answer;
    const WCHAR *text;

    if (remember)
        *remember = 0;
    if (!desktop_visible())
        return 3;
    text = win16
        ? L"Run this 16-bit Windows program with WineVDM?\n\n"
          L"Yes = WineVDM\n"
          L"No = Windows 3.1 guest\n"
          L"Cancel = don't run"
        : L"Run this DOS program with DOSBox-X?\n\n"
          L"Yes = DOSBox-X\n"
          L"No = ReactOS NTVDM\n"
          L"Cancel = don't run";
    answer = MessageBoxW(NULL, text, L"vetvdm",
                        MB_YESNOCANCEL | MB_ICONQUESTION | MB_SETFOREGROUND);
    if (answer != IDYES && answer != IDNO)
        return 0;
    if (remember &&
        MessageBoxW(NULL, L"Remember this choice for this program?",
                   L"vetvdm", MB_YESNO | MB_ICONQUESTION) == IDYES)
        *remember = 1;
    return (answer == IDYES) ? 1 : 2;
}
