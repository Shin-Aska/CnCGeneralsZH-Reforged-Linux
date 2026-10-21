from __future__ import annotations
import ctypes
from ctypes import wintypes
import json
import os
import sys

def identity_json() -> str:
    """Return the actual current Windows process identity with lossless birth digits."""
    created, exited, kernel, user = (wintypes.FILETIME() for _ in range(4))
    api = ctypes.WinDLL("kernel32", use_last_error=True)
    api.GetCurrentProcess.restype = wintypes.HANDLE
    api.GetProcessTimes.argtypes = [wintypes.HANDLE, *([ctypes.POINTER(wintypes.FILETIME)] * 4)]
    api.GetProcessTimes.restype = wintypes.BOOL
    assert api.GetProcessTimes(api.GetCurrentProcess(), ctypes.byref(created), ctypes.byref(exited), ctypes.byref(kernel), ctypes.byref(user))
    api.GetCommandLineW.restype = wintypes.LPWSTR
    api.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    api.OpenProcess.restype = wintypes.HANDLE
    api.CloseHandle.argtypes = [wintypes.HANDLE]
    api.CloseHandle.restype = wintypes.BOOL
    api.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
    api.QueryFullProcessImageNameW.restype = wintypes.BOOL
    handle = api.OpenProcess(0x1000, False, os.getppid())
    assert handle
    try:
        parent_created, parent_exit, parent_kernel, parent_user = (wintypes.FILETIME() for _ in range(4))
        assert api.GetProcessTimes(handle, ctypes.byref(parent_created), ctypes.byref(parent_exit), ctypes.byref(parent_kernel), ctypes.byref(parent_user))
        image, capacity = ctypes.create_unicode_buffer(32768), wintypes.DWORD(32768)
        assert api.QueryFullProcessImageNameW(handle, 0, image, ctypes.byref(capacity))
        parent = {"pid":os.getppid(),"creationFiletime":str((parent_created.dwHighDateTime << 32)|parent_created.dwLowDateTime),"executable":image.value}
    finally:
        assert api.CloseHandle(handle)
    return json.dumps({"commandLine":api.GetCommandLineW(),"parent":parent,"pid":os.getpid(),"parentPid":os.getppid(),"creationFiletime":str((created.dwHighDateTime << 32)|created.dwLowDateTime),"executable":sys.executable,"argv":sys.argv,"bytecodeDisabled":sys.dont_write_bytecode})

