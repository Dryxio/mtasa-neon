from __future__ import annotations

import os
import secrets
import stat
import ctypes
import errno
import sys
from pathlib import Path, PurePosixPath


_DIRECTORY_FLAGS = os.O_RDONLY | getattr(os, "O_DIRECTORY", 0) | getattr(os, "O_NOFOLLOW", 0)
_FILE_NOFOLLOW = getattr(os, "O_NOFOLLOW", 0)


def _rename_noreplace(parent: int, source: str, target: str) -> None:
    """Atomically publish a private POSIX entry without replacing a name."""
    library = ctypes.CDLL(None, use_errno=True)
    source_bytes = os.fsencode(source)
    target_bytes = os.fsencode(target)
    if sys.platform == "darwin":
        function = library.renameatx_np
        function.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
        function.restype = ctypes.c_int
        result = function(parent, source_bytes, parent, target_bytes, 0x4)  # RENAME_EXCL
    elif sys.platform.startswith("linux") and hasattr(library, "renameat2"):
        function = library.renameat2
        function.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
        function.restype = ctypes.c_int
        result = function(parent, source_bytes, parent, target_bytes, 0x1)  # RENAME_NOREPLACE
    else:
        raise OSError(errno.ENOTSUP, "atomic no-replace rename is unavailable on this platform")
    if result == 0:
        return
    error = ctypes.get_errno()
    if error in {errno.EEXIST, errno.ENOTEMPTY}:
        raise FileExistsError(error, os.strerror(error), target)
    raise OSError(error, os.strerror(error), target)


def _parts(relative: str) -> tuple[str, ...]:
    path = PurePosixPath(relative.replace("\\", "/"))
    if not relative or path.is_absolute() or "." in path.parts or ".." in path.parts:
        raise ValueError(f"unsafe anchored path: {relative}")
    return path.parts


class DirectoryAnchor:
    """Retain one real directory identity for race-safe relative I/O."""

    def __init__(self, path: Path, *, writable: bool = False):
        self.path = path.absolute()
        self._closed = False
        if os.name == "nt":
            from . import winfs

            self._handle = winfs.open_directory(self.path, writable=writable)
            self._identity = winfs.handle_identity(self._handle)
            return
        # Open the original pathname first. Resolving before O_NOFOLLOW would
        # let a final-component swap turn an untrusted symlink target into the
        # supposedly canonical root.
        self._handle = os.open(self.path, _DIRECTORY_FLAGS)
        actual = os.fstat(self._handle)
        self._identity = (actual.st_dev, actual.st_ino)
        if not stat.S_ISDIR(actual.st_mode):
            self.close()
            raise ValueError("anchored root must be a real directory")

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        if os.name == "nt":
            from . import winfs

            winfs.close(self._handle)
        else:
            os.close(self._handle)

    @property
    def identity(self) -> tuple[int, int]:
        return self._identity

    def __enter__(self) -> DirectoryAnchor:
        return self

    def __exit__(self, *_exc: object) -> None:
        self.close()

    def current(self) -> bool:
        try:
            if os.name == "nt":
                from . import winfs

                handle = winfs.open_directory(self.path)
                try:
                    return winfs.handle_identity(handle) == self._identity
                finally:
                    winfs.close(handle)
            handle = os.open(self.path, _DIRECTORY_FLAGS)
            try:
                details = os.fstat(handle)
                return (details.st_dev, details.st_ino) == self._identity
            finally:
                os.close(handle)
        except (OSError, ValueError):
            return False

    def _parent(
        self,
        parts: tuple[str, ...],
        *,
        writable: bool = False,
        expected_directories: dict[str, tuple[int, int]] | None = None,
    ) -> tuple[int, str]:
        walked: list[str] = []
        if os.name == "nt":
            from . import winfs

            handle = winfs.duplicate(self._handle)
            try:
                for name in parts[:-1]:
                    child = winfs.open_directory_at(handle, name, writable=writable)
                    walked.append(name)
                    expected = (expected_directories or {}).get("/".join(walked))
                    if expected is not None and winfs.handle_identity(child) != expected:
                        winfs.close(child)
                        raise OSError(f"anchored directory identity changed: {'/'.join(walked)}")
                    winfs.close(handle)
                    handle = child
                return handle, parts[-1]
            except Exception:
                winfs.close(handle)
                raise
        handle = os.dup(self._handle)
        try:
            for name in parts[:-1]:
                child = os.open(name, _DIRECTORY_FLAGS, dir_fd=handle)
                walked.append(name)
                expected = (expected_directories or {}).get("/".join(walked))
                if expected is not None:
                    details = os.fstat(child)
                    if (details.st_dev, details.st_ino) != expected:
                        os.close(child)
                        raise OSError(f"anchored directory identity changed: {'/'.join(walked)}")
                os.close(handle)
                handle = child
            return handle, parts[-1]
        except Exception:
            os.close(handle)
            raise

    @staticmethod
    def _close_parent(handle: int) -> None:
        if os.name == "nt":
            from . import winfs

            winfs.close(handle)
        else:
            os.close(handle)

    def read(self, relative: str, maximum: int) -> bytes:
        parts = _parts(relative)
        parent, name = self._parent(parts)
        try:
            if os.name == "nt":
                from . import winfs

                payload, identity = winfs.read_regular_at_identified(parent, name, maximum)
            else:
                descriptor = os.open(name, os.O_RDONLY | _FILE_NOFOLLOW, dir_fd=parent)
                try:
                    details = os.fstat(descriptor)
                    if not stat.S_ISREG(details.st_mode):
                        raise ValueError(f"anchored path is not a regular file: {relative}")
                    if details.st_size > maximum:
                        raise ValueError(f"anchored file exceeds {maximum} bytes: {relative}")
                    chunks: list[bytes] = []
                    remaining = details.st_size
                    while remaining:
                        chunk = os.read(descriptor, min(1024 * 1024, remaining))
                        if not chunk:
                            break
                        chunks.append(chunk)
                        remaining -= len(chunk)
                    payload = b"".join(chunks)
                    if len(payload) != details.st_size:
                        raise OSError(f"anchored file changed while being read: {relative}")
                    identity = (details.st_dev, details.st_ino)
                finally:
                    os.close(descriptor)
        finally:
            self._close_parent(parent)

        # Reopen through the retained directory tree so a mid-read file or
        # ancestor swap is observed instead of silently blessing stale bytes.
        parent, name = self._parent(parts)
        try:
            if os.name == "nt":
                from . import winfs

                current_identity = winfs.identity_at(parent, name, directory=False)
            else:
                descriptor = os.open(name, os.O_RDONLY | _FILE_NOFOLLOW, dir_fd=parent)
                try:
                    details = os.fstat(descriptor)
                    current_identity = (details.st_dev, details.st_ino)
                finally:
                    os.close(descriptor)
        finally:
            self._close_parent(parent)
        if current_identity != identity or not self.current():
            raise OSError(f"anchored file identity changed while it was verified: {relative}")
        return payload

    def entry_kind(self, relative: str) -> str:
        parts = _parts(relative)
        parent, name = self._parent(parts)
        try:
            if os.name == "nt":
                from . import winfs

                try:
                    winfs.identity_at(parent, name, directory=False)
                except FileNotFoundError:
                    return "missing"
                except ValueError:
                    try:
                        winfs.identity_at(parent, name, directory=True)
                    except ValueError:
                        return "link"
                    return "directory"
                return "file"
            try:
                details = os.stat(name, dir_fd=parent, follow_symlinks=False)
            except FileNotFoundError:
                return "missing"
            if stat.S_ISLNK(details.st_mode):
                return "link"
            if stat.S_ISREG(details.st_mode):
                return "file"
            if stat.S_ISDIR(details.st_mode):
                return "directory"
            return "special"
        except ValueError:
            return "link"
        finally:
            self._close_parent(parent)

    def entry_identity(self, relative: str, *, directory: bool = False) -> tuple[int, int]:
        parts = _parts(relative)
        parent, name = self._parent(parts)
        try:
            if os.name == "nt":
                from . import winfs

                return winfs.identity_at(parent, name, directory=directory)
            descriptor = os.open(
                name,
                _DIRECTORY_FLAGS if directory else os.O_RDONLY | _FILE_NOFOLLOW,
                dir_fd=parent,
            )
            try:
                details = os.fstat(descriptor)
                if bool(stat.S_ISDIR(details.st_mode)) != directory:
                    raise ValueError(f"anchored path has the wrong filesystem type: {relative}")
                if not directory and not stat.S_ISREG(details.st_mode):
                    raise ValueError(f"anchored path is not a regular file: {relative}")
                return details.st_dev, details.st_ino
            finally:
                os.close(descriptor)
        finally:
            self._close_parent(parent)

    def mkdir_new(self, relative: str, mode: int = 0o700) -> tuple[int, int]:
        parts = _parts(relative)
        parent, name = self._parent(parts, writable=True)
        try:
            if os.name == "nt":
                from . import winfs

                handle = winfs.open_directory_at(parent, name, create=True)
                try:
                    return winfs.handle_identity(handle)
                finally:
                    winfs.close(handle)
            else:
                temporary = f".{name}.{secrets.token_hex(16)}.mkdir"
                os.mkdir(temporary, mode, dir_fd=parent)
                try:
                    descriptor = os.open(temporary, _DIRECTORY_FLAGS, dir_fd=parent)
                    try:
                        details = os.fstat(descriptor)
                        identity = (details.st_dev, details.st_ino)
                    finally:
                        os.close(descriptor)
                    _rename_noreplace(parent, temporary, name)
                    return identity
                finally:
                    try:
                        os.rmdir(temporary, dir_fd=parent)
                    except FileNotFoundError:
                        pass
        finally:
            self._close_parent(parent)

    def ensure_directory(self, relative: str, mode: int = 0o700) -> None:
        parts = _parts(relative)
        if os.name == "nt":
            from . import winfs

            handle = winfs.duplicate(self._handle)
            try:
                for name in parts:
                    try:
                        child = winfs.open_directory_at(handle, name, writable=True)
                    except FileNotFoundError:
                        child = winfs.open_directory_at(handle, name, create=True, writable=True)
                    winfs.close(handle)
                    handle = child
            finally:
                winfs.close(handle)
            return
        handle = os.dup(self._handle)
        try:
            for name in parts:
                try:
                    child = os.open(name, _DIRECTORY_FLAGS, dir_fd=handle)
                except FileNotFoundError:
                    os.mkdir(name, mode, dir_fd=handle)
                    child = os.open(name, _DIRECTORY_FLAGS, dir_fd=handle)
                os.close(handle)
                handle = child
        finally:
            os.close(handle)

    def write_new(
        self,
        relative: str,
        payload: bytes,
        mode: int = 0o600,
        *,
        expected_directories: dict[str, tuple[int, int]] | None = None,
    ) -> tuple[int, int]:
        parts = _parts(relative)
        parent, name = self._parent(
            parts, writable=True, expected_directories=expected_directories,
        )
        try:
            if os.name == "nt":
                from . import winfs

                return winfs.write_new_at(parent, name, payload)
            temporary = f".{name}.{secrets.token_hex(16)}.write"
            descriptor = os.open(
                temporary,
                os.O_WRONLY | os.O_CREAT | os.O_EXCL | _FILE_NOFOLLOW,
                mode,
                dir_fd=parent,
            )
            try:
                with os.fdopen(descriptor, "wb", closefd=False) as stream:
                    stream.write(payload)
                    stream.flush()
                    os.fsync(stream.fileno())
                details = os.fstat(descriptor)
                identity = (details.st_dev, details.st_ino)
                _rename_noreplace(parent, temporary, name)
                return identity
            finally:
                os.close(descriptor)
                try:
                    os.unlink(temporary, dir_fd=parent)
                except FileNotFoundError:
                    pass
        finally:
            self._close_parent(parent)

    def replace(self, relative: str, payload: bytes, mode: int = 0o600) -> tuple[int, int]:
        parts = _parts(relative)
        parent, name = self._parent(parts, writable=True)
        temporary = f".{name}.{secrets.token_hex(8)}.tmp"
        try:
            if os.name == "nt":
                from . import winfs

                return winfs.atomic_write_at(parent, name, payload, temporary)
            descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL | _FILE_NOFOLLOW, mode, dir_fd=parent)
            try:
                with os.fdopen(descriptor, "wb", closefd=False) as stream:
                    stream.write(payload)
                    stream.flush()
                    os.fsync(stream.fileno())
                details = os.fstat(descriptor)
                identity = (details.st_dev, details.st_ino)
            finally:
                os.close(descriptor)
            os.replace(temporary, name, src_dir_fd=parent, dst_dir_fd=parent)
            return identity
        finally:
            if os.name != "nt":
                try:
                    os.unlink(temporary, dir_fd=parent)
                except FileNotFoundError:
                    pass
            self._close_parent(parent)

    def unlink_if_identity(
        self,
        relative: str,
        expected: tuple[int, int],
        *,
        directory: bool = False,
        expected_directories: dict[str, tuple[int, int]] | None = None,
    ) -> bool:
        """Remove only the object identity owned by the caller.

        Windows deletes the opened object by handle. POSIX has no portable
        unlink-by-handle primitive, so the directory entry is first moved to
        an unpredictable private name, revalidated there, and only then
        removed. A raced replacement is restored instead of deleted.
        """
        parts = _parts(relative)
        parent, name = self._parent(
            parts, writable=True, expected_directories=expected_directories,
        )
        try:
            if os.name == "nt":
                from . import winfs

                return winfs.unlink_if_identity_at(parent, name, expected, directory=directory)
            try:
                details = os.stat(name, dir_fd=parent, follow_symlinks=False)
            except FileNotFoundError:
                return True
            expected_type = stat.S_ISDIR(details.st_mode) if directory else stat.S_ISREG(details.st_mode)
            if not expected_type or (details.st_dev, details.st_ino) != expected:
                return False

            quarantine = f".{name}.{secrets.token_hex(16)}.rollback"
            os.rename(name, quarantine, src_dir_fd=parent, dst_dir_fd=parent)

            def restore_quarantine() -> None:
                try:
                    os.stat(name, dir_fd=parent, follow_symlinks=False)
                except FileNotFoundError:
                    os.rename(quarantine, name, src_dir_fd=parent, dst_dir_fd=parent)
                else:
                    raise OSError(
                        f"raced replacement was preserved as {quarantine}; original name is occupied"
                    )

            try:
                moved = os.stat(quarantine, dir_fd=parent, follow_symlinks=False)
                moved_type = stat.S_ISDIR(moved.st_mode) if directory else stat.S_ISREG(moved.st_mode)
                if not moved_type or (moved.st_dev, moved.st_ino) != expected:
                    restore_quarantine()
                    return False
                (os.rmdir if directory else os.unlink)(quarantine, dir_fd=parent)
                return True
            except Exception:
                restore_quarantine()
                raise
        finally:
            self._close_parent(parent)

    def unlink(self, relative: str, *, directory: bool = False) -> None:
        parts = _parts(relative)
        parent, name = self._parent(parts, writable=True)
        try:
            if os.name == "nt":
                from . import winfs

                (winfs.rmdir_at if directory else winfs.unlink_at)(parent, name)
            else:
                try:
                    (os.rmdir if directory else os.unlink)(name, dir_fd=parent)
                except FileNotFoundError:
                    pass
        finally:
            self._close_parent(parent)
