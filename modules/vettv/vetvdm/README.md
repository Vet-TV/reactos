<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# vetvdm path resolution

First piece of `vetvdm.exe` (the wrapper that sends DOS programs to DOSBox-X and Win16 programs to WineVDM). This folder only resolves paths. It does not read policy, parse profiles, generate a DOSBox-X config, or start a process.

The ReactOS build does not compile this directory yet. Build & Kernel adds it when the rest of `vetvdm.exe` is ready.

## Rules

Shared files go under `<SystemDrive>\VetTV\...`. The drive is chosen in this order:

1. `GetEnvironmentVariableW(L"SystemDrive")`, when the value is a drive like `D:` or `D:\`.
2. The drive letter of `GetSystemWindowsDirectoryW`.
3. `C:`.

Per-user files go under the folder returned by

```c
SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE,
                 NULL, SHGFP_TYPE_CURRENT, folder);
```

plus `\VetTV\DOS\...`. That shell call is the only per-user input, so the same code works on the current profile layout and on the later upstream layout.

If the shell call fails, the path is not absolute, or it contains `%`, per-user files fall back to a `data` folder next to the running exe. The status is `VETVDM_PATH_FALLBACK` so the caller can show a notice. Callers that only need to know the call worked can use `vetvdm_path_succeeded`.

Profile environment variables are never read and never expanded. Paths are not built from a profile-directory literal. A directory whose name contains `%` is never created. A result that does not fit in `MAX_PATH` (including the terminating NUL) is rejected instead of truncated.

## API

Declared in `vetvdm_paths.h`. Wide strings, XP-era `kernel32` and `shell32` only.

| Status | Meaning |
|---|---|
| `VETVDM_PATH_OK` | Resolved, and a per-user path used the shell folder |
| `VETVDM_PATH_FALLBACK` | Resolved under `<exe>\data`; show a notice |
| `VETVDM_PATH_ERR_INVAL` | NULL argument, bad kind, or a suffix that is not relative |
| `VETVDM_PATH_ERR_NOSPACE` | Caller buffer is shorter than the result; buffer unchanged |
| `VETVDM_PATH_ERR_RANGE` | Result does not fit in `MAX_PATH` |
| `VETVDM_PATH_ERR_PERCENT` | A component contains `%`; nothing was created |
| `VETVDM_PATH_ERR_CREATE` | A directory could not be created; the path is still written |
| `VETVDM_PATH_ERR_NOEXE` | Shell folder unusable and the exe directory is unknown |

| Kind | Path |
|---|---|
| `VETVDM_PATH_SHARED_ROOT` | `<drive>\VetTV` |
| `VETVDM_PATH_DOSBOXX` | `<drive>\VetTV\dosbox-x\dosbox-x.exe` |
| `VETVDM_PATH_PROFILES` | `<drive>\VetTV\profiles` |
| `VETVDM_PATH_CONF_TEMPLATE` | `<drive>\VetTV\vetvdm\default.conf` |
| `VETVDM_PATH_SHARED_LOG` | `<drive>\VetTV\logs\vetvdm.log` |
| `VETVDM_PATH_USER_ROOT` | `<shell>\VetTV\DOS` or `<exe>\data` |
| `VETVDM_PATH_USER_LOG` | `<user root>\logs\instant.log` |

```c
void vetvdm_path_pick_drive(const WCHAR *system_drive_env,
                            const WCHAR *system_windows_dir,
                            WCHAR out[4]);

DWORD vetvdm_path_query_roots(vetvdm_path_roots *roots);
DWORD vetvdm_path_query_roots_ex(vetvdm_path_roots *roots,
                                 vetvdm_shgetfolderpath_fn shget,
                                 const WCHAR *module_path);

DWORD vetvdm_path_resolve(const vetvdm_path_roots *roots,
                          vetvdm_path_kind kind,
                          const WCHAR *suffix,
                          WCHAR *out, DWORD cch, DWORD *needed,
                          BOOL create, vetvdm_path_mkdir_fn mkdir_fn,
                          BOOL *used_fallback);

DWORD vetvdm_path_resolve_os(vetvdm_path_kind kind, const WCHAR *suffix,
                             WCHAR *out, DWORD cch, DWORD *needed,
                             BOOL create, BOOL *used_fallback);
```

`suffix` is an optional relative tail and is accepted only for `SHARED_ROOT`, `PROFILES`, and `USER_ROOT`. It may not contain a drive, a leading slash, `.` or `..`, or `%`. `cch` counts WCHARs including the NUL. `cch == 0` with a NULL buffer is a size probe (`VETVDM_PATH_ERR_NOSPACE`, `*needed` set). `create` makes missing directories after the volume prefix; the last component is created only when the path is a directory. `mkdir_fn` NULL uses `CreateDirectoryW`.

`query_roots_ex` takes the shell callback and the exe path so tests can force a shell failure. NULL uses `SHGetFolderPathW` and `GetModuleFileNameW(NULL)`. `vetvdm.exe` calls `vetvdm_path_query_roots` or `vetvdm_path_resolve_os`.

## Host test

`vetvdm_paths_test.c` covers normal resolution, `SHGetFolderPathW` failure and fallback, `%` rejection (including "do not create"), and buffer edges (one short, exact fit, `MAX_PATH`, past `MAX_PATH`).

RosBE or mingw-w64 i686, from this directory:

```
i686-w64-mingw32-gcc -std=c99 -Wall -Wextra -Werror -O2 \
  -D_WIN32_WINNT=0x0502 -DWINVER=0x0502 -DUNICODE -D_UNICODE \
  -o vetvdm_paths_test.exe vetvdm_paths.c vetvdm_paths_test.c \
  -lshell32 -lkernel32
```

## Left for Build & Kernel

- Add `add_subdirectory(vettv/vetvdm)` from `modules/CMakeLists.txt`.
- Link the future `vetvdm.exe` against `vetvdm_paths` and decide where that exe is installed. This folder does not copy anything onto the boot CD.
- The unit test is a host console program (`win32cui`). Do not put it on the ISO.
- Policy, profile lookup, conf generation, quoting, logging I/O, and the job-object launch described in the vetvdm core plan are not in this change.
- Keep `_WIN32_WINNT` at `0x0502`. The imports are `kernel32` and `shell32` (`SHGetFolderPathW`, `GetEnvironmentVariableW`, `GetSystemWindowsDirectoryW`, `GetModuleFileNameW`, `CreateDirectoryW`).
