/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PROJECT:  VetTV Retro OS
 * FILE:     modules/vettv/vetvdm/core/vcore_types.h
 * PURPOSE:  Shared integer types for vetvdm core and host code.
 *
 * Copyright (C) 2026 VetTV
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#ifndef VCORE_TYPES_H
#define VCORE_TYPES_H

/*
 * Native unit tests compile with -fshort-wchar and no Win32 headers.
 * The ReactOS / mingw build uses the real XP headers.
 */
#if defined(VETVDM_TEST_NATIVE)
#include <stddef.h>
#include <stdint.h>
typedef wchar_t WCHAR;
typedef uint32_t DWORD;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#ifndef MAX_PATH
#define MAX_PATH 260
#endif
#else
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0502
#endif
#ifndef WINVER
#define WINVER 0x0502
#endif
#ifndef _WIN32_IE
#define _WIN32_IE 0x0501
#endif
#include <windows.h>
#endif

typedef char vetvdm_wchar_must_be_16[(sizeof(WCHAR) == 2) ? 1 : -1];

#endif /* VCORE_TYPES_H */
