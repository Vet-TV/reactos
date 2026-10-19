<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# vetvdm

`vetvdm.exe` decides how a DOS or Win16 program runs. The kernel32 hook calls it as `vetvdm.exe --from-hook --kind=dos|win16 --image="<path>" -- <original command line>`. A person or the shell can also launch it without `--from-hook`.

DOS (MZ, LE, LX) goes to DOSBox-X. Win16 (NE) goes to WineVDM (`otvdmw.exe`). Either layer falls back to ReactOS NTVDM by `CreateProcess` of the original program when the chosen executable is missing and the layer was not forced. The game's exit code (0–255, or WineVDM's code as returned) is passed through. vetvdm's own failures use `0xE00000xx`.

The executable is a console program (`win32cui`). ReactOS cmd waits only for console programs. `--from-hook` keeps the caller's console and working directory. A launch that was not started from the hook, and that owns a fresh console of its own, calls `FreeConsole` before the game so that window does not stay empty. `--help`, `--version`, and `--dry-run` keep the console so their text is visible.

DOSBox-X, WineVDM, Windows 3.1, and games are not in this tree. Paths come from the path library and from `HKLM\Software\VetTV\RetroOS\VDM` (HKCU overrides one value at a time). The log line goes to `<SystemDrive>\VetTV\logs\vetvdm.log`.

The ReactOS build does not compile this directory yet. Build & Kernel adds `add_subdirectory(vettv/vetvdm)` from `modules/CMakeLists.txt`.

## Host build

RosBE or mingw-w64 i686. From this directory, the launcher:

```
i686-w64-mingw32-gcc -std=gnu99 -Wall -Wextra -Werror -O2 \
  -D_WIN32_WINNT=0x0502 -DWINVER=0x0502 -DUNICODE -D_UNICODE \
  -mconsole -municode \
  -I. -Icore -Ivetvdm -Ithird_party \
  -o vetvdm.exe \
  vetvdm/main.c vetvdm/hostutil.c vetvdm/host_route.c \
  vetvdm/host_dosbox.c vetvdm/host_doscmd.c vetvdm/host_wine.c \
  vetvdm/host_ntvdm.c vetvdm/host_log.c \
  vetvdm/cmdline.c vetvdm/cmdline_norm.c vetvdm/cmdline_quote.c \
  vetvdm/image.c vetvdm/route.c vetvdm/ask.c vetvdm/image_io.c \
  core/buf.c core/utf8.c core/sha256.c core/quote.c \
  core/policy.c core/profile.c core/profile_json.c \
  core/conf.c core/conf_emit.c core/dryrun.c core/log.c \
  core/policy_reg.c core/launch.c core/tempdir.c vetvdm_paths.c \
  -lshell32 -luser32 -ladvapi32 -lkernel32
```

The unit test (command line, NT paths, MZ/NE/LE/LX/PE routing, conf locks, SHA-256, golden dry-run text) also builds as a native program. That build does not run the Win32 host. Run it from this directory so `core/tests/golden/` resolves.

```
gcc -std=gnu99 -Wall -Wextra -Werror -fshort-wchar -DVETVDM_TEST_NATIVE \
  -I. -Icore -Ivetvdm -Ithird_party \
  -o vetvdm_test \
  core/tests/test_vetvdm.c core/tests/test_support.c \
  core/tests/test_cmdline.c core/tests/test_core.c \
  vetvdm/cmdline.c vetvdm/cmdline_norm.c vetvdm/cmdline_quote.c \
  vetvdm/image.c vetvdm/route.c \
  core/buf.c core/utf8.c core/sha256.c core/quote.c \
  core/policy.c core/profile.c core/profile_json.c \
  core/conf.c core/conf_emit.c core/dryrun.c core/log.c
```

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
i686-w64-mingw32-gcc -std=gnu99 -Wall -Wextra -Werror -O2 \
  -D_WIN32_WINNT=0x0502 -DWINVER=0x0502 -DUNICODE -D_UNICODE \
  -o vetvdm_paths_test.exe vetvdm_paths.c vetvdm_paths_test.c \
  -lshell32 -lkernel32
```

## Left for Build & Kernel

- Add `add_subdirectory(vettv/vetvdm)` from `modules/CMakeLists.txt` and install `vetvdm.exe` at `%SystemRoot%\System32\vetvdm.exe`. This folder does not copy anything onto the boot CD.
- The unit tests are host console programs (`win32cui`). Do not put them on the ISO.
- Keep `_WIN32_WINNT` at `0x0502`. `vetvdm.exe` imports `kernel32`, `user32`, `advapi32`, and `shell32`.
- The hook's recursion guard is the image name `vetvdm.exe`. vetvdm does not set `VETTV_VDM_BYPASS`. NTVDM is `CreateProcessW` of the original program, and the child inherits the environment unchanged. WineVDM (`otvdm.exe`) can still re-enter the hook; skipping that image name is a Build & Kernel question.
- DOSBox-X pin is 2026.10.01. The installer copies `dosbox-x_XPx86_SDL2.exe` onto `dosbox-x.exe`, and vetvdm launches that name with `-exit -nopromptfolder -fastlaunch`. The guest exit code is `config -get errorlevel >V:\ERRORLVL.TXT` in the generated conf. WineVDM's default is `%SystemDrive%\VetTV\winevdm\otvdmw.exe`; registry `WineVdmPath` overrides it, and `%ProgramFiles%\otvdm\otvdmw.exe` (then `otvdm.exe`) is the fallback when the chosen file is missing. See `THIRD_PARTY.txt`. JSON profiles use vendored jsmn (MIT). The log line is the shared `vetvdm.log`.
