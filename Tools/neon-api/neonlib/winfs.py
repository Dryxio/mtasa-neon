from __future__ import annotations

import ctypes
import os
from ctypes import wintypes
from pathlib import Path


if os.name != "nt":
    raise ImportError("the Windows handle backend is only available on Windows")


FILE_LIST_DIRECTORY = 0x0001
FILE_READ_DATA = 0x0001
FILE_WRITE_DATA = 0x0002
FILE_APPEND_DATA = 0x0004
FILE_ADD_SUBDIRECTORY = 0x0004
FILE_TRAVERSE = 0x0020
FILE_READ_ATTRIBUTES = 0x0080
FILE_WRITE_ATTRIBUTES = 0x0100
DELETE = 0x00010000
SYNCHRONIZE = 0x00100000
FILE_SHARE_ALL = 0x00000007
FILE_OPEN = 0x00000001
FILE_CREATE = 0x00000002
FILE_OPEN_IF = 0x00000003
FILE_DIRECTORY_FILE = 0x00000001
FILE_SYNCHRONOUS_IO_NONALERT = 0x00000020
FILE_NON_DIRECTORY_FILE = 0x00000040
FILE_OPEN_REPARSE_POINT = 0x00200000
FILE_ATTRIBUTE_DIRECTORY = 0x00000010
FILE_ATTRIBUTE_REPARSE_POINT = 0x00000400
FILE_FLAG_BACKUP_SEMANTICS = 0x02000000
FILE_FLAG_OPEN_REPARSE_POINT = 0x00200000
OPEN_EXISTING = 3
OBJ_CASE_INSENSITIVE = 0x00000040
FILE_ATTRIBUTE_TAG_INFO_CLASS = 9
FILE_RENAME_INFORMATION = 10
FILE_DISPOSITION_INFORMATION = 13
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value


class _UNICODE_STRING(ctypes.Structure):
    _fields_ = [
        ("Length", wintypes.USHORT),
        ("MaximumLength", wintypes.USHORT),
        ("Buffer", wintypes.LPWSTR),
    ]


class _OBJECT_ATTRIBUTES(ctypes.Structure):
    _fields_ = [
        ("Length", wintypes.ULONG),
        ("RootDirectory", wintypes.HANDLE),
        ("ObjectName", ctypes.POINTER(_UNICODE_STRING)),
        ("Attributes", wintypes.ULONG),
        ("SecurityDescriptor", wintypes.LPVOID),
        ("SecurityQualityOfService", wintypes.LPVOID),
    ]


class _IO_STATUS_VALUE(ctypes.Union):
    _fields_ = [("Status", wintypes.LONG), ("Pointer", wintypes.LPVOID)]


class _IO_STATUS_BLOCK(ctypes.Structure):
    _anonymous_ = ("value",)
    _fields_ = [("value", _IO_STATUS_VALUE), ("Information", ctypes.c_size_t)]


class _FILE_ATTRIBUTE_TAG_INFO(ctypes.Structure):
    _fields_ = [("FileAttributes", wintypes.DWORD), ("ReparseTag", wintypes.DWORD)]


class _FILE_RENAME_HEADER(ctypes.Structure):
    _fields_ = [
        ("ReplaceIfExists", wintypes.BOOLEAN),
        ("RootDirectory", wintypes.HANDLE),
        ("FileNameLength", wintypes.DWORD),
    ]


_kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
_ntdll = ctypes.WinDLL("ntdll")

_kernel32.CreateFileW.argtypes = [
    wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID,
    wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE,
]
_kernel32.CreateFileW.restype = wintypes.HANDLE
_kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
_kernel32.CloseHandle.restype = wintypes.BOOL
_kernel32.DuplicateHandle.argtypes = [
    wintypes.HANDLE, wintypes.HANDLE, wintypes.HANDLE, ctypes.POINTER(wintypes.HANDLE),
    wintypes.DWORD, wintypes.BOOL, wintypes.DWORD,
]
_kernel32.DuplicateHandle.restype = wintypes.BOOL
_kernel32.GetCurrentProcess.restype = wintypes.HANDLE
_kernel32.GetFileInformationByHandleEx.argtypes = [
    wintypes.HANDLE, ctypes.c_int, wintypes.LPVOID, wintypes.DWORD,
]
_kernel32.GetFileInformationByHandleEx.restype = wintypes.BOOL
_kernel32.GetFileSizeEx.argtypes = [wintypes.HANDLE, ctypes.POINTER(ctypes.c_longlong)]
_kernel32.GetFileSizeEx.restype = wintypes.BOOL
_kernel32.ReadFile.argtypes = [
    wintypes.HANDLE, wintypes.LPVOID, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), wintypes.LPVOID,
]
_kernel32.ReadFile.restype = wintypes.BOOL
_kernel32.WriteFile.argtypes = [
    wintypes.HANDLE, wintypes.LPCVOID, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), wintypes.LPVOID,
]
_kernel32.WriteFile.restype = wintypes.BOOL
_kernel32.FlushFileBuffers.argtypes = [wintypes.HANDLE]
_kernel32.FlushFileBuffers.restype = wintypes.BOOL
_ntdll.NtCreateFile.argtypes = [
    ctypes.POINTER(wintypes.HANDLE), wintypes.DWORD, ctypes.POINTER(_OBJECT_ATTRIBUTES),
    ctypes.POINTER(_IO_STATUS_BLOCK), ctypes.POINTER(ctypes.c_longlong), wintypes.ULONG,
    wintypes.ULONG, wintypes.ULONG, wintypes.ULONG, wintypes.LPVOID, wintypes.ULONG,
]
_ntdll.NtCreateFile.restype = wintypes.LONG
_ntdll.NtSetInformationFile.argtypes = [
    wintypes.HANDLE, ctypes.POINTER(_IO_STATUS_BLOCK), wintypes.LPVOID, wintypes.ULONG, wintypes.ULONG,
]
_ntdll.NtSetInformationFile.restype = wintypes.LONG
_ntdll.RtlNtStatusToDosError.argtypes = [wintypes.LONG]
_ntdll.RtlNtStatusToDosError.restype = wintypes.ULONG


def _winerror() -> OSError:
    return ctypes.WinError(ctypes.get_last_error())


def _raise_status(status: int, operation: str) -> None:
    if status >= 0:
        return
    error = int(_ntdll.RtlNtStatusToDosError(status))
    if error in {2, 3}:
        raise FileNotFoundError(error, operation)
    if error in {80, 183}:
        raise FileExistsError(error, operation)
    raise OSError(error, f"{operation}: {ctypes.FormatError(error)}")


def _attributes(handle: int) -> int:
    info = _FILE_ATTRIBUTE_TAG_INFO()
    if not _kernel32.GetFileInformationByHandleEx(
        handle, FILE_ATTRIBUTE_TAG_INFO_CLASS, ctypes.byref(info), ctypes.sizeof(info),
    ):
        raise _winerror()
    return int(info.FileAttributes)


def _validate_handle(handle: int, *, directory: bool) -> None:
    attributes = _attributes(handle)
    if attributes & FILE_ATTRIBUTE_REPARSE_POINT:
        raise ValueError("approved path contains a Windows reparse point")
    if bool(attributes & FILE_ATTRIBUTE_DIRECTORY) != directory:
        raise ValueError("approved handle has the wrong filesystem type")


def open_directory(path: Path) -> int:
    handle = _kernel32.CreateFileW(
        os.fspath(path),
        FILE_LIST_DIRECTORY | FILE_TRAVERSE | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
        FILE_SHARE_ALL, None, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, None,
    )
    if handle in {None, INVALID_HANDLE_VALUE}:
        raise _winerror()
    value = int(handle)
    try:
        _validate_handle(value, directory=True)
        return value
    except Exception:
        close(value)
        raise


def _relative_handle(
    parent: int, name: str, *, directory: bool, create: bool = False,
    open_if: bool = False, writable: bool = False, append: bool = False,
) -> int:
    if not name or name in {".", ".."} or "/" in name or "\\" in name or "\x00" in name:
        raise ValueError("Windows handle-relative path component is unsafe")
    buffer = ctypes.create_unicode_buffer(name)
    encoded_length = len(name.encode("utf-16-le"))
    unicode = _UNICODE_STRING(encoded_length, encoded_length + 2, ctypes.cast(buffer, wintypes.LPWSTR))
    attributes = _OBJECT_ATTRIBUTES(
        ctypes.sizeof(_OBJECT_ATTRIBUTES), parent, ctypes.pointer(unicode),
        OBJ_CASE_INSENSITIVE, None, None,
    )
    output = wintypes.HANDLE()
    status_block = _IO_STATUS_BLOCK()
    desired = FILE_READ_ATTRIBUTES | SYNCHRONIZE
    options = FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT
    if directory:
        desired |= FILE_LIST_DIRECTORY | FILE_WRITE_DATA | FILE_ADD_SUBDIRECTORY | FILE_TRAVERSE
        options |= FILE_DIRECTORY_FILE
    else:
        desired |= FILE_APPEND_DATA if append else FILE_READ_DATA
        if (writable or create or open_if) and not append:
            desired |= FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES | DELETE
        options |= FILE_NON_DIRECTORY_FILE
    disposition = FILE_CREATE if create else FILE_OPEN_IF if open_if else FILE_OPEN
    status = int(_ntdll.NtCreateFile(
        ctypes.byref(output), desired, ctypes.byref(attributes), ctypes.byref(status_block),
        None, 0, FILE_SHARE_ALL, disposition, options, None, 0,
    ))
    _raise_status(status, f"open relative component {name}")
    handle = int(output.value)
    try:
        _validate_handle(handle, directory=directory)
        return handle
    except Exception:
        close(handle)
        raise


def open_directory_at(parent: int, name: str, *, create: bool = False) -> int:
    return _relative_handle(parent, name, directory=True, create=create)


def duplicate(handle: int) -> int:
    process = _kernel32.GetCurrentProcess()
    output = wintypes.HANDLE()
    if not _kernel32.DuplicateHandle(process, handle, process, ctypes.byref(output), 0, False, 0x2):
        raise _winerror()
    return int(output.value)


def close(handle: int) -> None:
    if not _kernel32.CloseHandle(handle):
        raise _winerror()


def read_regular_at(parent: int, name: str, maximum: int) -> bytes:
    handle = _relative_handle(parent, name, directory=False)
    try:
        size = ctypes.c_longlong()
        if not _kernel32.GetFileSizeEx(handle, ctypes.byref(size)):
            raise _winerror()
        if size.value > maximum:
            raise ValueError(f"configured input exceeds {maximum} bytes")
        chunks: list[bytes] = []
        remaining = size.value
        while remaining:
            length = min(1024 * 1024, remaining)
            buffer = ctypes.create_string_buffer(length)
            received = wintypes.DWORD()
            if not _kernel32.ReadFile(handle, buffer, length, ctypes.byref(received), None):
                raise _winerror()
            if received.value == 0:
                break
            chunks.append(buffer.raw[:received.value])
            remaining -= received.value
        payload = b"".join(chunks)
        if len(payload) != size.value:
            raise OSError("configured input changed while it was being read")
        return payload
    finally:
        close(handle)


def regular_at(parent: int, name: str) -> bool:
    try:
        handle = _relative_handle(parent, name, directory=False)
    except FileNotFoundError:
        return False
    try:
        return True
    finally:
        close(handle)


def _write_all(handle: int, payload: bytes, *, flush: bool = True) -> None:
    offset = 0
    while offset < len(payload):
        chunk = payload[offset:offset + 1024 * 1024]
        written = wintypes.DWORD()
        buffer = ctypes.create_string_buffer(chunk)
        if not _kernel32.WriteFile(handle, buffer, len(chunk), ctypes.byref(written), None):
            raise _winerror()
        if written.value == 0:
            raise OSError("Windows handle write made no progress")
        offset += written.value
    if flush and not _kernel32.FlushFileBuffers(handle):
        raise _winerror()


def append_bounded_at(parent: int, name: str, payload: bytes, maximum: int) -> bool:
    handle = _relative_handle(parent, name, directory=False, open_if=True, append=True)
    try:
        size = ctypes.c_longlong()
        if not _kernel32.GetFileSizeEx(handle, ctypes.byref(size)):
            raise _winerror()
        if size.value + len(payload) > maximum:
            return False
        # There is exactly one daemon audit writer. FILE_APPEND_DATA makes the
        # kernel choose EOF, while avoiding an O(n^2) read/rewrite cycle.
        _write_all(handle, payload, flush=False)
        return True
    finally:
        close(handle)


def _rename(handle: int, parent: int, target: str) -> None:
    encoded = target.encode("utf-16-le")
    name_offset = _FILE_RENAME_HEADER.FileNameLength.offset + ctypes.sizeof(wintypes.DWORD)
    storage = ctypes.create_string_buffer(name_offset + len(encoded))
    header = ctypes.cast(storage, ctypes.POINTER(_FILE_RENAME_HEADER)).contents
    header.ReplaceIfExists = True
    header.RootDirectory = parent
    header.FileNameLength = len(encoded)
    ctypes.memmove(ctypes.addressof(storage) + name_offset, encoded, len(encoded))
    status_block = _IO_STATUS_BLOCK()
    status = int(_ntdll.NtSetInformationFile(
        handle, ctypes.byref(status_block), storage, len(storage), FILE_RENAME_INFORMATION,
    ))
    _raise_status(status, f"replace relative file {target}")


def _delete_on_close(handle: int) -> None:
    value = wintypes.BOOLEAN(True)
    status_block = _IO_STATUS_BLOCK()
    status = int(_ntdll.NtSetInformationFile(
        handle, ctypes.byref(status_block), ctypes.byref(value), ctypes.sizeof(value),
        FILE_DISPOSITION_INFORMATION,
    ))
    _raise_status(status, "delete temporary relative file")


def atomic_write_at(parent: int, name: str, payload: bytes, temporary: str) -> None:
    handle = _relative_handle(parent, temporary, directory=False, create=True, writable=True)
    renamed = False
    try:
        _write_all(handle, payload)
        _rename(handle, parent, name)
        renamed = True
    finally:
        if not renamed:
            try:
                _delete_on_close(handle)
            except OSError:
                pass
        close(handle)
