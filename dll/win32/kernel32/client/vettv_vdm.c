/* SPDX-License-Identifier: GPL-2.0-or-later */
/* VETTV: PROJECT VetTV Retro OS; FILE dll/win32/kernel32/client/vettv_vdm.c */
/* VETTV: Redirect DOS and Win16 CreateProcess to System32\vetvdm.exe. GPL-2.0-or-later. */
#include <k32.h>
#define NDEBUG
#include <debug.h>

/* VETTV: HKLM\Software\VetTV\RetroOS\VDM\HookEnabled (REG_DWORD), default off. */
static UNICODE_STRING VetTvHookKey =
    RTL_CONSTANT_STRING(L"\\Registry\\Machine\\Software\\VetTV\\RetroOS\\VDM");
static UNICODE_STRING VetTvHookValue = RTL_CONSTANT_STRING(L"HookEnabled");
static UNICODE_STRING VetTvImageName = RTL_CONSTANT_STRING(L"vetvdm.exe");

static BOOLEAN
VetTvHookEnabled(VOID)
{
    OBJECT_ATTRIBUTES Attributes;
    HANDLE KeyHandle;
    NTSTATUS Status;
    ULONG Length;
    struct { ULONG TitleIndex, Type, DataLength, Data; } Value;

    /* VETTV: NtOpenKey + NtQueryValueKey only. Absent or 0 leaves the hook off. */
    InitializeObjectAttributes(&Attributes, &VetTvHookKey, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenKey(&KeyHandle, KEY_QUERY_VALUE, &Attributes);
    if (!NT_SUCCESS(Status)) return FALSE;
    Status = NtQueryValueKey(KeyHandle, &VetTvHookValue, KeyValuePartialInformation,
                             &Value, sizeof(Value), &Length);
    NtClose(KeyHandle);
    return NT_SUCCESS(Status) && Value.Type == REG_DWORD &&
           Value.DataLength == sizeof(ULONG) && Value.Data != 0;
}

static BOOLEAN
VetTvCallerIsVetVdm(VOID)
{
    PUNICODE_STRING Image;
    UNICODE_STRING Leaf;
    USHORT Chars, Index;
    /* VETTV: loop guard. CreateProcess from vetvdm.exe reaches the real VDM. */
    if (!NtCurrentPeb()->ProcessParameters) return FALSE;
    Image = &NtCurrentPeb()->ProcessParameters->ImagePathName;
    if (!Image->Buffer || Image->Length < VetTvImageName.Length) return FALSE;
    Chars = (USHORT)(Image->Length / sizeof(WCHAR));
    for (Index = Chars; Index > 0; Index--)
        if (Image->Buffer[Index - 1] == L'\\' || Image->Buffer[Index - 1] == L'/')
            break;
    Leaf.Buffer = &Image->Buffer[Index];
    Leaf.Length = (USHORT)((Chars - Index) * sizeof(WCHAR));
    Leaf.MaximumLength = Leaf.Length;
    return RtlEqualUnicodeString(&Leaf, &VetTvImageName, TRUE);
}

static PWCHAR
VetTvCopy(PWCHAR Dest, PCWSTR Source, SIZE_T Chars)
{
    RtlCopyMemory(Dest, Source, Chars * sizeof(WCHAR));
    return Dest + Chars;
}
BOOLEAN
VetTvRedirectToVdm(IN PUNICODE_STRING PathName, IN LPCWSTR CommandLine,
                   IN LPCWSTR Kind, OUT PWCHAR *NewCmdLine)
{
    static const WCHAR Tail[] = L"\\System32\\vetvdm.exe";
    static const WCHAR Mid[] = L"\" --from-hook --kind=";
    static const WCHAR Img[] = L" --image=\"";
    static const WCHAR Sep[] = L"\" -- ";
    WCHAR VetVdmPath[MAX_PATH];
    PCWSTR Root, Cmd;
    SIZE_T RootChars, TailChars, KindChars, ImageChars, CmdChars, Total, i;
    PWCHAR Buffer, Dest;
    *NewCmdLine = NULL;
    /* VETTV: fail open onto stock behavior. Never fail the launch from here. */
    if (!VetTvHookEnabled() || VetTvCallerIsVetVdm() || !PathName || !PathName->Buffer ||
        PathName->Length < sizeof(WCHAR) || (PathName->Length & 1) || !Kind || !*Kind)
        return FALSE;
    /* VETTV: %SystemRoot%\System32\vetvdm.exe via SharedUserData. No profile expansion. */
    Root = SharedUserData->NtSystemRoot;
    RootChars = wcslen(Root);
    TailChars = RTL_NUMBER_OF(Tail) - 1;
    if (!RootChars || RootChars + TailChars >= RTL_NUMBER_OF(VetVdmPath)) return FALSE;
    VetTvCopy(VetTvCopy(VetVdmPath, Root, RootChars), Tail, TailChars + 1);
    /* VETTV: RtlDoesFileExists_UStr is not exported; U is the same DOS-path check. */
    if (!RtlDoesFileExists_U(VetVdmPath)) return FALSE;
    ImageChars = PathName->Length / sizeof(WCHAR);
    for (i = 0; i < ImageChars; i++)
        if (PathName->Buffer[i] == L'"' || PathName->Buffer[i] == UNICODE_NULL)
            return FALSE;
    KindChars = wcslen(Kind);
    Cmd = CommandLine ? CommandLine : L"";
    CmdChars = wcslen(Cmd);
    if (KindChars > 8 || ImageChars > 32767 || CmdChars > 32767) return FALSE;
    /* VETTV: "<SystemRoot>\System32\vetvdm.exe" --from-hook --kind=dos|win16 --image="<PathName>" -- <cmd> */
    Total = 1 + RootChars + TailChars + (RTL_NUMBER_OF(Mid) - 1) + KindChars +
            (RTL_NUMBER_OF(Img) - 1) + ImageChars + (RTL_NUMBER_OF(Sep) - 1) + CmdChars + 1;
    Buffer = RtlAllocateHeap(RtlGetProcessHeap(), 0, Total * sizeof(WCHAR));
    if (!Buffer) return FALSE;
    Dest = VetTvCopy(Buffer, L"\"", 1);
    Dest = VetTvCopy(Dest, VetVdmPath, RootChars + TailChars);
    Dest = VetTvCopy(Dest, Mid, RTL_NUMBER_OF(Mid) - 1);
    Dest = VetTvCopy(Dest, Kind, KindChars);
    Dest = VetTvCopy(Dest, Img, RTL_NUMBER_OF(Img) - 1);
    Dest = VetTvCopy(Dest, PathName->Buffer, ImageChars);
    Dest = VetTvCopy(Dest, Sep, RTL_NUMBER_OF(Sep) - 1);
    Dest = VetTvCopy(Dest, Cmd, CmdChars);
    *Dest = UNICODE_NULL;
    /* VETTV: size mismatch means the command line is wrong; free it and fail open. */
    if ((SIZE_T)(Dest - Buffer) + 1 != Total)
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, Buffer);
        return FALSE;
    }
    *NewCmdLine = Buffer;
    return TRUE;
}
