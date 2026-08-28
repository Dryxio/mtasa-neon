from __future__ import annotations

import hmac
import hashlib
import os
import secrets
import select
import signal
import socket
import stat
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timedelta, timezone
from pathlib import Path, PurePosixPath
from typing import Any

from .catalogue import catalogue_semantic_issues
from .jsonio import JsonDocumentError, canonical_json, sha256_bytes
from .mutation import mutation_failure, mutation_result, parse_client_command, parse_resource_command
from .project import resolve_project_components
from .proof import (
    PROBE_RESOURCE,
    REPORT_FILE,
    arm_runtime_probe,
    client_executable,
    create_probe_config,
    load_probe_report,
    proof_failure,
    prove_runtime,
    verify_runtime_probe,
)
from .runtime import compare_runtime_snapshot
from .schema import SchemaStore, schema_major


CAPABILITIES = ["artifacts.read", "diagnostics.read", "knowledge.read", "project.read", "runtime.observe"]
MUTATION_CAPABILITIES = {"resource.lifecycle", "scenario.execute", "client.launch"}
MAX_REQUEST_BYTES = 64 * 1024
MAX_RESPONSE_BYTES = 16 * 1024 * 1024
MAX_SESSION_BYTES = 256 * 1024
MAX_AUDIT_BYTES = 256 * 1024


class SupervisorMutationOutcomeUnknown(ValueError):
    """The peer may have applied a non-idempotent command before transport failure."""

    def __init__(self, message: str, session_id: str):
        super().__init__(message)
        self.session_id = session_id


_CHILDREN: dict[int, subprocess.Popen[Any]] = {}
_DIRECTORY_ANCHORS: dict[int, tuple[Path, int, int]] = {}
_HAS_DIRECTORY_FD = os.name != "nt" and os.open in os.supports_dir_fd


def _require_secure_filesystem() -> None:
    if not _HAS_DIRECTORY_FD and os.name != "nt":
        # A pathname check followed by a pathname reopen cannot uphold the
        # supervisor's anti-TOCTOU contract. Unknown platforms fail closed.
        raise OSError("this platform has no secure handle-relative filesystem backend")


def _diagnostic(code: str, message: str, path: str = ".") -> dict[str, Any]:
    bounded = message if len(message) <= 1024 else message[:1000] + "…[truncated]"
    return {"code": code, "severity": "error", "message": bounded, "path": path[:512]}


def _utc_now() -> datetime:
    return datetime.now(timezone.utc).replace(microsecond=0)


def _utc_text(value: datetime) -> str:
    return value.isoformat().replace("+00:00", "Z")


def _authorization(token: str, document: dict[str, Any]) -> str:
    return hmac.digest(bytes.fromhex(token), canonical_json(document).encode("utf-8"), "sha256").hex()


def _server_executable(root: Path) -> tuple[Path, str]:
    original_root = root.absolute()
    if original_root.is_symlink():
        raise ValueError("MTA server root must be a real directory")
    root = original_root.resolve()
    if not root.is_dir():
        raise ValueError("MTA server root must be a real directory")
    names = ("MTA Server64.exe", "MTA Server.exe") if os.name == "nt" else ("mta-server64", "mta-server")
    executable = next((root / name for name in names if (root / name).is_file() and not (root / name).is_symlink()), None)
    if executable is None:
        raise ValueError("MTA server root has no approved server executable")
    digest = hashlib.sha256()
    size = 0
    with executable.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            size += len(chunk)
            if size > 512 * 1024 * 1024:
                raise ValueError("MTA server executable exceeds 536870912 bytes")
            digest.update(chunk)
    return executable, digest.hexdigest()


def _write_server_input(stream: Any, line: bytes) -> int:
    """Attempt exactly one non-blocking pipe write; mutations are never retried."""
    try:
        return os.write(stream.fileno(), line)
    except BlockingIOError:
        return 0


def _submit_guardian_command(
    connection: socket.socket, sequence: int, action: str, resource: str,
) -> tuple[str, int]:
    """Submit one authenticated-channel command and require its exact acknowledgement."""
    request = {"sequence": sequence, "action": action, "resource": resource}
    payload = canonical_json(request).encode("utf-8")
    if len(payload) > 4096:
        raise ValueError("driver guardian request exceeds 4096 bytes")
    connection.settimeout(3)
    connection.sendall(payload)
    from .scenario import load_json_text

    response = load_json_text(_receive_line(connection, 4096).decode("utf-8"))
    if (
        not isinstance(response, dict) or set(response) != {"sequence", "status", "written"}
        or response.get("sequence") != sequence
        or response.get("status") not in {"submitted", "backpressure", "partial", "unavailable"}
        or isinstance(response.get("written"), bool) or not isinstance(response.get("written"), int)
    ):
        raise ValueError("driver guardian acknowledgement is invalid")
    expected = len(f"{action} {resource}\n".encode("ascii"))
    written = response["written"]
    if (
        written < 0 or written > expected
        or (response["status"] == "submitted" and written != expected)
        or (response["status"] == "partial" and not 0 < written < expected)
        or (response["status"] in {"backpressure", "unavailable"} and written != 0)
    ):
        raise ValueError("driver guardian acknowledgement is inconsistent")
    return response["status"], written


def _query_guardian_alive(connection: socket.socket, sequence: int) -> bool:
    request = {"sequence": sequence, "action": "status", "resource": PROBE_RESOURCE}
    connection.settimeout(3)
    connection.sendall(canonical_json(request).encode("utf-8"))
    from .scenario import load_json_text

    response = load_json_text(_receive_line(connection, 4096).decode("utf-8"))
    if (
        not isinstance(response, dict) or set(response) != {"sequence", "status", "written"}
        or response.get("sequence") != sequence or response.get("status") not in {"alive", "unavailable"}
        or response.get("written") != 0
    ):
        raise ValueError("driver guardian liveness acknowledgement is invalid")
    return response["status"] == "alive"


def _terminate_server_process(process: subprocess.Popen[Any]) -> None:
    """Stop only the server process tree owned by the private driver guardian."""
    if process.poll() is not None:
        process.wait()
        return
    if os.name == "nt":
        process.terminate()
    else:
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            return
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        if os.name == "nt":
            process.kill()
        else:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        process.wait(timeout=2)


def _open_driver_fallback(pid: int) -> tuple[str, Any]:
    """Retain an identity-bound kill path if the guardian itself disappears."""
    if os.name == "nt":
        from . import winfs

        return "windows-handle", winfs.open_process_for_termination(pid)
    if hasattr(os, "pidfd_open") and hasattr(signal, "pidfd_send_signal"):
        return "pidfd", os.pidfd_open(pid, 0)
    if hasattr(select, "kqueue"):
        queue = select.kqueue()
        event = select.kevent(
            pid, filter=select.KQ_FILTER_PROC, flags=select.KQ_EV_ADD | select.KQ_EV_ENABLE,
            fflags=select.KQ_NOTE_EXIT,
        )
        queue.control([event], 0, 0)
        return "kqueue", queue
    raise OSError("platform cannot retain an identity-bound MTA process fallback")


def _terminate_driver_fallback(fallback: tuple[str, Any] | None, pid: int) -> None:
    if fallback is None:
        return
    kind, handle = fallback
    if kind == "windows-handle":
        from . import winfs

        winfs.terminate_process_handle(handle)
        return
    if kind == "pidfd":
        if select.select([handle], [], [], 0)[0]:
            return
        signal.pidfd_send_signal(handle, signal.SIGTERM)
        if not select.select([handle], [], [], 3)[0]:
            signal.pidfd_send_signal(handle, signal.SIGKILL)
            select.select([handle], [], [], 2)
        return
    if kind == "kqueue":
        if handle.control(None, 1, 0):
            return
        try:
            os.killpg(pid, signal.SIGTERM)
        except ProcessLookupError:
            return
        if not handle.control(None, 1, 3):
            try:
                os.killpg(pid, signal.SIGKILL)
            except ProcessLookupError:
                return
            handle.control(None, 1, 2)


def _close_driver_fallback(fallback: tuple[str, Any] | None) -> None:
    if fallback is None:
        return
    kind, handle = fallback
    if kind == "windows-handle":
        from . import winfs

        winfs.close(handle)
    else:
        handle.close() if kind == "kqueue" else os.close(handle)


def run_driver_guardian(
    server_root: Path, expected_sha256: str, host: str, port: int, token: str,
) -> int:
    """Own MTA and stop it when the supervisor-side lifeline disappears."""
    connection: socket.socket | None = None
    server_process: subprocess.Popen[bytes] | None = None
    try:
        if host != "127.0.0.1" or not 1 <= port <= 65535:
            raise ValueError("driver guardian transport is not bounded loopback")
        if len(token) != 64 or any(character not in "0123456789abcdef" for character in token):
            raise ValueError("driver guardian token is invalid")
        connection = socket.create_connection((host, port), timeout=3)
        connection.settimeout(0.5)
        executable, executable_sha256 = _server_executable(server_root)
        if not hmac.compare_digest(executable_sha256, expected_sha256):
            raise ValueError("MTA server executable changed before guardian launch")
        child_options: dict[str, Any] = {
            "cwd": server_root, "stdin": subprocess.PIPE, "stdout": subprocess.DEVNULL,
            "stderr": subprocess.DEVNULL, "close_fds": True, "bufsize": 0,
        }
        if os.name == "nt":
            child_options["creationflags"] = subprocess.CREATE_NEW_PROCESS_GROUP
        else:
            child_options["start_new_session"] = True
        server_process = subprocess.Popen([os.fspath(executable)], **child_options)
        if server_process.stdin is None:
            raise ValueError("MTA server input pipe is unavailable")
        try:
            if os.name == "nt":
                from . import winfs

                winfs.set_pipe_nonblocking(server_process.stdin.fileno())
            else:
                os.set_blocking(server_process.stdin.fileno(), False)
        except (AttributeError, OSError) as exc:
            raise ValueError("platform cannot provide bounded non-blocking MTA server input") from exc
        stabilization_deadline = time.monotonic() + 1
        while time.monotonic() < stabilization_deadline:
            if server_process.poll() is not None:
                raise ValueError(f"MTA server exited during startup with code {server_process.returncode}")
            time.sleep(0.02)
        _, current_sha256 = _server_executable(server_root)
        if not hmac.compare_digest(current_sha256, expected_sha256):
            raise ValueError("MTA server executable changed during guardian launch")

        ready = {
            "type": "ready", "pid": server_process.pid, "executable": executable.name,
            "executableSha256": executable_sha256,
        }
        ready["authorization"] = _authorization(token, ready)
        connection.sendall(canonical_json(ready).encode("utf-8"))
        while True:
            server_exit = server_process.returncode if server_process.poll() is not None else None
            try:
                payload = _receive_line(connection, 4096)
            except socket.timeout:
                continue
            if not payload:
                break
            from .scenario import load_json_text

            request = load_json_text(payload.decode("utf-8"))
            if (
                not isinstance(request, dict) or set(request) != {"sequence", "action", "resource"}
                or isinstance(request.get("sequence"), bool) or not isinstance(request.get("sequence"), int)
                or not 1 <= request["sequence"] <= 2147483647
                or not isinstance(request.get("action"), str)
                or not isinstance(request.get("resource"), str)
                or (
                    not (request["action"] == "status" and request["resource"] == PROBE_RESOURCE)
                    and parse_resource_command(f"resource.{request['action']}/{request['resource']}") is None
                )
            ):
                raise ValueError("driver guardian request is invalid")
            response: dict[str, Any] = {"sequence": request["sequence"]}
            server_exit = server_process.returncode if server_process.poll() is not None else None
            if request["action"] == "status":
                response["status"] = "alive" if server_exit is None else "unavailable"
                response["written"] = 0
            elif server_exit is not None or server_process.stdin is None:
                response["status"] = "unavailable"
                response["written"] = 0
            else:
                line = f"{request['action']} {request['resource']}\n".encode("ascii")
                written = _write_server_input(server_process.stdin, line)
                response["written"] = written
                response["status"] = (
                    "backpressure" if written == 0
                    else "partial" if written != len(line)
                    else "submitted"
                )
            connection.sendall(canonical_json(response).encode("utf-8"))
    except (JsonDocumentError, OSError, UnicodeError, ValueError) as exc:
        if connection is not None:
            try:
                failure = {"type": "error", "message": str(exc)[:1024] or "driver guardian failed"}
                failure["authorization"] = _authorization(token, failure)
                connection.sendall(canonical_json(failure).encode("utf-8"))
            except OSError:
                pass
        return 1
    finally:
        if connection is not None:
            connection.close()
        if server_process is not None:
            _terminate_server_process(server_process)
    return 0


def _response_envelope(
    token: str, session_id: str, challenge: str, nonce: str, command: str, result: dict[str, Any],
) -> dict[str, Any]:
    signed = {
        "sessionId": session_id, "challenge": challenge, "nonce": nonce,
        "command": command, "result": result,
    }
    return {"result": result, "authorization": _authorization(token, signed)}


def _receive_line(connection: socket.socket, maximum: int) -> bytes:
    payload = bytearray()
    while len(payload) <= maximum:
        chunk = connection.recv(min(4096, maximum + 1 - len(payload)))
        if not chunk:
            break
        payload.extend(chunk)
        if payload.endswith(b"\n"):
            break
    if not payload.endswith(b"\n") or len(payload) > maximum:
        raise ValueError("bounded protocol document is incomplete or oversized")
    return bytes(payload)


def _atomic_write_at(directory_fd: int, name: str, payload: bytes, mode: int = 0o600) -> None:
    temporary = f".{name}.{secrets.token_hex(12)}.tmp"
    if not _HAS_DIRECTORY_FD:
        _require_secure_filesystem()
        from . import winfs

        winfs.atomic_write_at(directory_fd, name, payload, temporary)
        return
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    descriptor = os.open(temporary, flags, mode, dir_fd=directory_fd)
    try:
        os.fchmod(descriptor, mode)
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, name, src_dir_fd=directory_fd, dst_dir_fd=directory_fd)
        os.fsync(directory_fd)
    finally:
        try:
            os.unlink(temporary, dir_fd=directory_fd)
        except FileNotFoundError:
            pass


def _open_directory(path: Path) -> int:
    if not _HAS_DIRECTORY_FD:
        _require_secure_filesystem()
        from . import winfs

        descriptor = winfs.open_directory(path)
        _DIRECTORY_ANCHORS[descriptor] = (path.resolve(), 0, 0)
        return descriptor
    flags = os.O_RDONLY
    if hasattr(os, "O_DIRECTORY"):
        flags |= os.O_DIRECTORY
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    descriptor = os.open(path, flags)
    if not stat.S_ISDIR(os.fstat(descriptor).st_mode):
        os.close(descriptor)
        raise ValueError("approved path is not a directory")
    metadata = os.fstat(descriptor)
    _DIRECTORY_ANCHORS[descriptor] = (path.resolve(), metadata.st_dev, metadata.st_ino)
    return descriptor


def _open_directory_at(root_fd: int, relative: str) -> int:
    pure = PurePosixPath(relative)
    if not pure.parts or pure.is_absolute() or "." in pure.parts or ".." in pure.parts:
        raise ValueError("directory path is not a safe relative path")
    if not _HAS_DIRECTORY_FD:
        _require_secure_filesystem()
        from . import winfs

        current = winfs.duplicate(root_fd)
        try:
            for part in pure.parts:
                following = winfs.open_directory_at(current, part)
                winfs.close(current)
                current = following
            root_path = _DIRECTORY_ANCHORS[root_fd][0]
            _DIRECTORY_ANCHORS[current] = (root_path.joinpath(*pure.parts), 0, 0)
            return current
        except Exception:
            winfs.close(current)
            raise
    current = os.dup(root_fd)
    flags = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW
    try:
        for part in pure.parts:
            following = os.open(part, flags, dir_fd=current)
            os.close(current)
            current = following
        metadata = os.fstat(current)
        root_path = _DIRECTORY_ANCHORS[root_fd][0]
        candidate = root_path.joinpath(*pure.parts)
        _DIRECTORY_ANCHORS[current] = (candidate, metadata.st_dev, metadata.st_ino)
        return current
    except Exception:
        os.close(current)
        raise


def _ensure_directory_at(root_fd: int, relative: str, mode: int = 0o700) -> int:
    pure = PurePosixPath(relative)
    if not pure.parts or pure.is_absolute() or "." in pure.parts or ".." in pure.parts:
        raise ValueError("directory path is not a safe relative path")
    if not _HAS_DIRECTORY_FD:
        _require_secure_filesystem()
        from . import winfs

        current = winfs.duplicate(root_fd)
        try:
            for part in pure.parts:
                try:
                    following = winfs.open_directory_at(current, part)
                except FileNotFoundError:
                    try:
                        following = winfs.open_directory_at(current, part, create=True)
                    except FileExistsError:
                        following = winfs.open_directory_at(current, part)
                winfs.close(current)
                current = following
            root_path = _DIRECTORY_ANCHORS[root_fd][0]
            _DIRECTORY_ANCHORS[current] = (root_path.joinpath(*pure.parts), 0, 0)
            return current
        except Exception:
            winfs.close(current)
            raise
    current = os.dup(root_fd)
    flags = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW
    try:
        for part in pure.parts:
            try:
                following = os.open(part, flags, dir_fd=current)
            except FileNotFoundError:
                os.mkdir(part, mode=mode, dir_fd=current)
                following = os.open(part, flags, dir_fd=current)
            os.close(current)
            current = following
        metadata = os.fstat(current)
        root_path = _DIRECTORY_ANCHORS[root_fd][0]
        candidate = root_path.joinpath(*pure.parts)
        _DIRECTORY_ANCHORS[current] = (candidate, metadata.st_dev, metadata.st_ino)
        return current
    except Exception:
        os.close(current)
        raise


def _create_directory_at(parent_fd: int, name: str, mode: int = 0o700) -> int:
    if not name or "/" in name or "\\" in name or name in {".", ".."}:
        raise ValueError("child directory name is unsafe")
    if not _HAS_DIRECTORY_FD:
        _require_secure_filesystem()
        from . import winfs

        child = winfs.open_directory_at(parent_fd, name, create=True)
        parent_path = _DIRECTORY_ANCHORS[parent_fd][0]
        _DIRECTORY_ANCHORS[child] = (parent_path / name, 0, 0)
        return child
    os.mkdir(name, mode=mode, dir_fd=parent_fd)
    return _open_directory_at(parent_fd, name)


def _checked_anchor(descriptor: int) -> Path:
    path, device, inode = _DIRECTORY_ANCHORS[descriptor]
    if path.is_symlink() or not path.is_dir():
        raise ValueError("approved directory path became unsafe")
    metadata = path.stat()
    if metadata.st_dev != device or metadata.st_ino != inode:
        raise ValueError("approved directory identity changed")
    return path


def _close_directory(descriptor: int) -> None:
    _DIRECTORY_ANCHORS.pop(descriptor, None)
    if not _HAS_DIRECTORY_FD:
        _require_secure_filesystem()
        from . import winfs

        winfs.close(descriptor)
    else:
        os.close(descriptor)


def _duplicate_directory(descriptor: int) -> int:
    if not _HAS_DIRECTORY_FD:
        _require_secure_filesystem()
        from . import winfs

        duplicate = winfs.duplicate(descriptor)
    else:
        duplicate = os.dup(descriptor)
    path, _, _ = _DIRECTORY_ANCHORS[descriptor]
    if _HAS_DIRECTORY_FD:
        metadata = os.fstat(duplicate)
        identity = (metadata.st_dev, metadata.st_ino)
    else:
        identity = (0, 0)
    _DIRECTORY_ANCHORS[duplicate] = (path, *identity)
    return duplicate


def _read_regular_at(root_fd: int, relative: str, maximum: int) -> bytes:
    pure = PurePosixPath(relative)
    if not pure.parts or pure.is_absolute() or "." in pure.parts or ".." in pure.parts:
        raise ValueError("configured input path is not a safe relative path")
    if not _HAS_DIRECTORY_FD:
        _require_secure_filesystem()
        from . import winfs

        current = winfs.duplicate(root_fd)
        try:
            for part in pure.parts[:-1]:
                following = winfs.open_directory_at(current, part)
                winfs.close(current)
                current = following
            return winfs.read_regular_at(current, pure.parts[-1], maximum)
        finally:
            winfs.close(current)
    current = os.dup(root_fd)
    try:
        directory_flags = os.O_RDONLY
        if hasattr(os, "O_DIRECTORY"):
            directory_flags |= os.O_DIRECTORY
        if hasattr(os, "O_NOFOLLOW"):
            directory_flags |= os.O_NOFOLLOW
        for part in pure.parts[:-1]:
            following = os.open(part, directory_flags, dir_fd=current)
            os.close(current)
            current = following
        file_flags = os.O_RDONLY
        if hasattr(os, "O_NOFOLLOW"):
            file_flags |= os.O_NOFOLLOW
        descriptor = os.open(pure.parts[-1], file_flags, dir_fd=current)
        try:
            metadata = os.fstat(descriptor)
            if not stat.S_ISREG(metadata.st_mode):
                raise ValueError("configured input is not a regular file")
            if metadata.st_size > maximum:
                raise ValueError(f"configured input exceeds {maximum} bytes")
            chunks: list[bytes] = []
            size = 0
            while True:
                chunk = os.read(descriptor, min(1024 * 1024, maximum + 1 - size))
                if not chunk:
                    break
                chunks.append(chunk)
                size += len(chunk)
                if size > maximum:
                    raise ValueError(f"configured input exceeds {maximum} bytes")
            return b"".join(chunks)
        finally:
            os.close(descriptor)
    finally:
        os.close(current)


def _regular_at(root_fd: int, relative: str) -> bool:
    try:
        _read_regular_at(root_fd, relative, 16 * 1024 * 1024)
        return True
    except (OSError, ValueError):
        return False


def _regular_file_at(root_fd: int, relative: str) -> bool:
    pure = PurePosixPath(relative)
    if not pure.parts or pure.is_absolute() or "." in pure.parts or ".." in pure.parts:
        raise ValueError("configured input path is not a safe relative path")
    if not _HAS_DIRECTORY_FD:
        _require_secure_filesystem()
        from . import winfs

        current = winfs.duplicate(root_fd)
        try:
            for part in pure.parts[:-1]:
                following = winfs.open_directory_at(current, part)
                winfs.close(current)
                current = following
            return winfs.regular_at(current, pure.parts[-1])
        except FileNotFoundError:
            return False
        finally:
            winfs.close(current)
    current = os.dup(root_fd)
    try:
        flags = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW
        for part in pure.parts[:-1]:
            try:
                following = os.open(part, flags, dir_fd=current)
            except FileNotFoundError:
                return False
            os.close(current)
            current = following
        file_flags = os.O_RDONLY | os.O_NOFOLLOW
        try:
            descriptor = os.open(pure.parts[-1], file_flags, dir_fd=current)
        except FileNotFoundError:
            return False
        try:
            return stat.S_ISREG(os.fstat(descriptor).st_mode)
        finally:
            os.close(descriptor)
    finally:
        os.close(current)


def _joined_relative(base: PurePosixPath, value: str) -> str:
    child = PurePosixPath(value.replace("\\", "/"))
    if not child.parts or child.is_absolute() or "." in child.parts or ".." in child.parts:
        raise ValueError(f"component path is unsafe: {value}")
    combined = base.joinpath(child)
    return combined.as_posix()


def _resolve_project_anchored(
    workspace_fd: int,
    project_relative: str,
    project_payload: bytes,
    catalogue_relative: str,
    catalogue_payload: bytes,
    project: dict[str, Any],
    schema_store: SchemaStore,
) -> dict[str, Any]:
    """Resolve a contract from a private copy populated only by anchored reads.

    The general project checker intentionally works with Path objects. Feeding
    it live workspace paths would reintroduce check/reopen races after the
    supervisor had pinned its top-level inputs. This private tree contains only
    bytes obtained through the approved workspace handle and is never exposed
    as a runtime capability.
    """
    from .project import META_ASSET_TAGS, _parse_meta_document

    def write_shadow(root: Path, relative: str, payload: bytes) -> None:
        pure = PurePosixPath(relative)
        if not pure.parts or pure.is_absolute() or "." in pure.parts or ".." in pure.parts:
            raise ValueError("shadow contract path is unsafe")
        target = root.joinpath(*pure.parts)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)

    def read_optional(relative: str, maximum: int) -> bytes | None:
        try:
            return _read_regular_at(workspace_fd, relative, maximum)
        except FileNotFoundError:
            return None

    project_base = PurePosixPath(project_relative).parent
    copied_references = 0
    with tempfile.TemporaryDirectory(prefix="neon-supervisor-contract-") as temporary:
        shadow = Path(temporary)
        write_shadow(shadow, project_relative, project_payload)
        write_shadow(shadow, catalogue_relative, catalogue_payload)
        entries = [
            *(('resource', entry) for entry in project.get("resources", [])),
            *(('module', entry) for entry in project.get("modules", [])),
        ]
        for kind, entry in entries:
            component_relative = _joined_relative(project_base, entry["path"])
            try:
                component_fd = _open_directory_at(workspace_fd, component_relative)
            except FileNotFoundError:
                continue
            else:
                _close_directory(component_fd)
            shadow.joinpath(*PurePosixPath(component_relative).parts).mkdir(parents=True, exist_ok=True)

            manifest_name = entry.get("manifest")
            if manifest_name:
                manifest_relative = _joined_relative(PurePosixPath(component_relative), manifest_name)
                manifest_payload = read_optional(manifest_relative, 4 * 1024 * 1024)
                if manifest_payload is not None:
                    write_shadow(shadow, manifest_relative, manifest_payload)

            binary_name = entry.get("binary")
            if binary_name:
                binary_relative = _joined_relative(PurePosixPath(component_relative), binary_name)
                binary_payload = read_optional(binary_relative, 512 * 1024 * 1024)
                if binary_payload is not None:
                    write_shadow(shadow, binary_relative, binary_payload)

            if kind != "resource":
                continue
            meta_name = entry.get("meta", "meta.xml")
            meta_relative = _joined_relative(PurePosixPath(component_relative), meta_name)
            meta_payload = read_optional(meta_relative, 4 * 1024 * 1024)
            if meta_payload is None:
                continue
            write_shadow(shadow, meta_relative, meta_payload)
            try:
                meta = _parse_meta_document(meta_payload)
            except ValueError:
                continue
            references: list[tuple[str, bool]] = []
            for tag in META_ASSET_TAGS:
                references.extend((item.get("src") or "", False) for item in meta.findall(tag, "meta", 1))
            references.extend((item.get("src") or "", True) for item in meta.findall("script", "meta", 1))
            for source, needs_payload in references:
                if not source:
                    continue
                copied_references += 1
                if copied_references > 4096:
                    raise ValueError("component contract references more than 4096 files")
                try:
                    source_relative = _joined_relative(PurePosixPath(component_relative), source)
                except ValueError:
                    continue
                if needs_payload:
                    source_payload = read_optional(source_relative, 16 * 1024 * 1024)
                    if source_payload is not None:
                        write_shadow(shadow, source_relative, source_payload)
                elif _regular_file_at(workspace_fd, source_relative):
                    write_shadow(shadow, source_relative, b"")

        shadow_project = shadow.joinpath(*PurePosixPath(project_relative).parts)
        shadow_catalogue = shadow.joinpath(*PurePosixPath(catalogue_relative).parts)
        return resolve_project_components(shadow_project, schema_store, shadow_catalogue)


def _relative(workspace: Path, value: Path) -> PurePosixPath:
    if not value.is_absolute():
        candidate = PurePosixPath(value.as_posix().replace("\\", "/"))
    else:
        absolute = value.absolute()
        candidates = [absolute]
        text = absolute.as_posix()
        if text == "/var" or text.startswith("/var/") or text == "/tmp" or text.startswith("/tmp/"):
            candidates.append(Path("/private" + text))
        elif text == "/private/var" or text.startswith("/private/var/") or text == "/private/tmp" or text.startswith("/private/tmp/"):
            candidates.append(Path(text.removeprefix("/private")))
        candidate = None
        for absolute_candidate in candidates:
            for root in (workspace.absolute(), workspace.resolve()):
                try:
                    candidate = PurePosixPath(absolute_candidate.relative_to(root).as_posix())
                    break
                except ValueError:
                    continue
            if candidate is not None:
                break
        if candidate is None:
            raise ValueError(f"path is outside the approved workspace: {value}")
    if not candidate.parts or candidate.is_absolute() or ".." in candidate.parts or "." in candidate.parts:
        raise ValueError(f"path must be workspace-relative without traversal: {value}")
    return candidate


def _inside(workspace: Path, value: Path, *, allow_missing: bool = False) -> tuple[Path, str]:
    original = workspace.absolute()
    root = workspace.resolve()
    relative = _relative(original, value)
    candidate = root.joinpath(*relative.parts)
    current = root
    for part in relative.parts:
        current /= part
        if current.is_symlink():
            raise ValueError(f"path contains a symbolic link: {relative.as_posix()}")
        if not current.exists() and allow_missing:
            break
    try:
        candidate.resolve(strict=False).relative_to(root)
    except (OSError, ValueError) as exc:
        raise ValueError(f"path escapes workspace: {relative.as_posix()}") from exc
    return candidate, relative.as_posix()


def _open_session_record(
    workspace: Path, session_path: Path, schema_store: SchemaStore,
) -> tuple[dict[str, Any], Path, str, int, int, str]:
    path, relative = _inside(workspace, session_path)
    pure = PurePosixPath(relative)
    workspace_fd = _open_directory(workspace.resolve())
    directory_fd: int | None = None
    try:
        directory_fd = (
            _open_directory_at(workspace_fd, PurePosixPath(*pure.parts[:-1]).as_posix())
            if len(pure.parts) > 1 else _duplicate_directory(workspace_fd)
        )
        payload = _read_regular_at(directory_fd, pure.name, MAX_SESSION_BYTES)
        from .scenario import load_json_text

        session = load_json_text(payload.decode("utf-8"))
        issues = schema_store.validate("neon-supervisor-session", session)
        if issues:
            details = "; ".join(f"{item.pointer[:256]}: {item.message[:256]}" for item in issues[:16])
            if len(issues) > 16:
                details += f"; {len(issues) - 16} additional issues omitted"
            raise ValueError("invalid supervisor session: " + details)
        if schema_major(session["schemaVersion"]) != 1:
            raise ValueError(f"unsupported supervisor session schema {session['schemaVersion']}")
        if session["state"] != "active" or "token" not in session:
            raise ValueError(f"supervisor session is {session['state']}")
        return session, path, relative, workspace_fd, directory_fd, pure.name
    except Exception:
        if directory_fd is not None:
            _close_directory(directory_fd)
        _close_directory(workspace_fd)
        raise


def _load_session(workspace: Path, session_path: Path, schema_store: SchemaStore) -> tuple[dict[str, Any], Path, str]:
    session, path, relative, workspace_fd, directory_fd, _ = _open_session_record(workspace, session_path, schema_store)
    _close_directory(directory_fd)
    _close_directory(workspace_fd)
    return session, path, relative


def _public_session(session: dict[str, Any], relative: str, snapshot_available: bool, state: str | None = None) -> dict[str, Any]:
    return {
        "sessionId": session["sessionId"], "state": state or session["state"], "profile": session["profile"],
        "createdAt": session["createdAt"], "expiresAt": session["expiresAt"],
        "capabilities": session["capabilities"], "sessionPath": relative, "snapshotAvailable": snapshot_available,
    }


def _reap_children() -> None:
    for pid, process in list(_CHILDREN.items()):
        if process.poll() is not None:
            process.wait()
            _CHILDREN.pop(pid, None)


def _wait_for_session_process(session: dict[str, Any], timeout: float = 2.0) -> None:
    process = _CHILDREN.get(session.get("pid"))
    if process is not None:
        try:
            process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            pass
    _reap_children()


def _supervisor_result(command: str, session: dict[str, Any], relative: str, available: bool, *, state: str | None = None) -> dict[str, Any]:
    return {
        "schemaVersion": "1.0.0", "command": command, "status": "pass",
        "summary": {"errors": 0, "warnings": 0}, "diagnostics": [],
        "session": _public_session(session, relative, available, state),
    }


def supervisor_failure(command: str, code: str, message: str) -> dict[str, Any]:
    return {
        "schemaVersion": "1.0.0", "command": command, "status": "fail",
        "summary": {"errors": 1, "warnings": 0},
        "diagnostics": [_diagnostic(code, message)],
        "session": {
            "sessionId": "session:unavailable", "state": "closed", "profile": "unknown",
            "createdAt": "unavailable", "expiresAt": "unavailable", "capabilities": [],
            "sessionPath": "unavailable/session.json", "snapshotAvailable": False,
        },
    }


def start_supervisor(
    workspace: Path,
    project_path: Path,
    catalogue_path: Path | None,
    snapshot_path: Path,
    session_root: Path,
    ttl_seconds: int,
    tool_path: Path,
    schema_store: SchemaStore,
    enabled_capabilities: tuple[str, ...] = (),
    server_root: Path | None = None,
    client_root: Path | None = None,
    connect_port: int = 22003,
    test_client_adapter: bool = False,
) -> dict[str, Any]:
    _reap_children()
    workspace_original = workspace.absolute()
    workspace = workspace.resolve()
    workspace_fd = _open_directory(workspace)
    root_fd: int | None = None
    session_fd: int | None = None
    try:
        enabled = set(enabled_capabilities)
        if not enabled.issubset(MUTATION_CAPABILITIES):
            raise ValueError("requested supervisor capability is not allowlisted")
        if "resource.lifecycle" in enabled and server_root is None:
            raise ValueError("resource.lifecycle requires an explicitly approved MTA server root")
        if "client.launch" in enabled and "resource.lifecycle" not in enabled:
            raise ValueError("client.launch requires resource.lifecycle in the same bounded session")
        if "client.launch" in enabled and client_root is None:
            raise ValueError("client.launch requires an explicitly approved MTA client root")
        if server_root is not None and "resource.lifecycle" not in enabled:
            raise ValueError("an MTA server root may only be supplied with resource.lifecycle")
        if client_root is not None and "client.launch" not in enabled:
            raise ValueError("an MTA client root may only be supplied with client.launch")
        if isinstance(connect_port, bool) or not isinstance(connect_port, int) or not 1 <= connect_port <= 65535:
            raise ValueError("client connection port must be an integer from 1 to 65535")
        approved_server_root = server_root.resolve() if server_root is not None else None
        approved_client_root = client_root.resolve() if client_root is not None else None
        if approved_server_root is not None:
            _server_executable(approved_server_root)
        if approved_client_root is not None:
            client_executable(approved_client_root, test_client_adapter)
        project_file, project_relative = _inside(workspace_original, project_path)
        _, snapshot_relative = _inside(workspace_original, snapshot_path, allow_missing=True)
        _, root_relative = _inside(workspace_original, session_root, allow_missing=True)
        from .scenario import load_json_text

        project_payload = _read_regular_at(workspace_fd, project_relative, 4 * 1024 * 1024)
        project = load_json_text(project_payload.decode("utf-8"))
        project_issues = schema_store.validate("neon-project", project)
        if project_issues or schema_major(project.get("schemaVersion", "")) != 1:
            raise ValueError("project contract is invalid or unsupported")
        selected_catalogue = (
            catalogue_path if catalogue_path is not None
            else Path(PurePosixPath(project_relative).parent.as_posix()) / project["catalogue"]
        )
        catalogue_file, catalogue_relative = _inside(workspace_original, selected_catalogue)
        catalogue_payload = _read_regular_at(workspace_fd, catalogue_relative, 16 * 1024 * 1024)
        catalogue = load_json_text(catalogue_payload.decode("utf-8"))
        catalogue_issues = schema_store.validate("neon-api", catalogue)
        semantic_issues = catalogue_semantic_issues(catalogue) if not catalogue_issues else []
        if catalogue_issues or semantic_issues or schema_major(catalogue.get("schemaVersion", "")) != 1:
            raise ValueError("catalogue contract is invalid or unsupported")
        resolved = _resolve_project_anchored(
            workspace_fd, project_relative, project_payload, catalogue_relative,
            catalogue_payload, project, schema_store,
        )
        if resolved["status"] != "pass":
            raise ValueError("project must pass static resolution before a supervisor session can start")
        if "client.launch" in enabled:
            if project["profile"] not in {"neon-pair", "neon-multiclient"}:
                raise ValueError("client.launch requires a neon-pair or neon-multiclient project profile")
            verify_runtime_probe(approved_server_root)
        if (
            _read_regular_at(workspace_fd, project_relative, 4 * 1024 * 1024) != project_payload
            or _read_regular_at(workspace_fd, catalogue_relative, 16 * 1024 * 1024) != catalogue_payload
        ):
            raise ValueError("project or catalogue changed during supervisor startup")

        root_fd = _ensure_directory_at(workspace_fd, root_relative)
        session_id = f"session:{secrets.token_hex(12)}"
        session_name = session_id.replace(":", "-")
        session_fd = _create_directory_at(root_fd, session_name)
        session_relative = f"{root_relative}/{session_name}"
        session_directory = workspace.joinpath(*PurePosixPath(session_relative).parts)
        command = [
            sys.executable, os.fspath(tool_path), "_supervisor-daemon",
            "--workspace", os.fspath(workspace), "--session-directory", os.fspath(session_directory),
            "--session-id", session_id, "--project", project_relative, "--catalogue", catalogue_relative,
            "--snapshot", snapshot_relative, "--ttl", str(ttl_seconds),
        ]
        for capability in sorted(enabled):
            command.extend(("--capability", capability))
        if approved_server_root is not None:
            command.extend(("--server-root", os.fspath(approved_server_root)))
        if approved_client_root is not None:
            command.extend(("--client-root", os.fspath(approved_client_root), "--connect-port", str(connect_port)))
        if test_client_adapter:
            command.append("--test-client-adapter")
        kwargs: dict[str, Any] = {
            "cwd": workspace, "stdin": subprocess.DEVNULL, "stdout": subprocess.DEVNULL, "stderr": subprocess.DEVNULL,
            "close_fds": True,
        }
        if os.name == "nt":
            kwargs["creationflags"] = subprocess.CREATE_NEW_PROCESS_GROUP | subprocess.DETACHED_PROCESS
        else:
            kwargs["start_new_session"] = True
        process = subprocess.Popen(command, **kwargs)
        _CHILDREN[process.pid] = process
        # A cold Windows server can take longer than the guardian's transport
        # handshake while DLLs and databases are first loaded. Keep startup
        # bounded, but leave enough headroom for the daemon to report either a
        # signed guardian failure or its active session contract.
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            try:
                session_payload = _read_regular_at(session_fd, "session.json", MAX_SESSION_BYTES)
            except FileNotFoundError:
                session_payload = None
            if session_payload is not None:
                session = load_json_text(session_payload.decode("utf-8"))
                issues = schema_store.validate("neon-supervisor-session", session)
                if issues or session.get("state") != "active" or "token" not in session:
                    raise ValueError("supervisor produced an invalid active session")
                relative = f"{session_relative}/session.json"
                return _supervisor_result(
                    "supervisor.start", session, relative, _regular_at(workspace_fd, snapshot_relative),
                )
            try:
                error_payload = _read_regular_at(session_fd, "startup-error.json", MAX_SESSION_BYTES)
            except FileNotFoundError:
                error_payload = None
            if error_payload is not None:
                error = load_json_text(error_payload.decode("utf-8"))
                raise ValueError(error.get("message", "supervisor failed to start"))
            if process.poll() is not None:
                raise ValueError(f"supervisor exited during startup with code {process.returncode}")
            time.sleep(0.05)
        process.terminate()
        raise ValueError("supervisor did not become ready within 20 seconds")
    finally:
        if session_fd is not None:
            _close_directory(session_fd)
        if root_fd is not None:
            _close_directory(root_fd)
        _close_directory(workspace_fd)


def request_supervisor(
    workspace: Path, session_path: Path, command: str, schema_store: SchemaStore,
    timeout_ms: int = 10000,
) -> dict[str, Any]:
    if isinstance(timeout_ms, bool) or not isinstance(timeout_ms, int) or not 1 <= timeout_ms <= 600000:
        raise ValueError("supervisor timeout must be an integer from 1 to 600000 milliseconds")
    if (
        command not in {"status", "runtime.compare", "runtime.prove", "shutdown", "scenario.authorize"}
        and parse_resource_command(command) is None and parse_client_command(command) is None
    ):
        raise ValueError("supervisor command is not allowlisted")
    session, _, _, workspace_fd, directory_fd, session_name = _open_session_record(
        workspace, session_path, schema_store,
    )
    try:
        return _request_supervisor_loaded(
            command, schema_store, session, directory_fd, session_name, timeout_ms,
        )
    finally:
        _close_directory(directory_fd)
        _close_directory(workspace_fd)


def _request_supervisor_loaded(
    command: str,
    schema_store: SchemaStore,
    session: dict[str, Any],
    directory_fd: int,
    session_name: str,
    timeout_ms: int,
) -> dict[str, Any]:
    expires = datetime.strptime(session["expiresAt"], "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)
    if _utc_now() >= expires:
        process = _CHILDREN.get(session["pid"])
        if process is not None:
            try:
                process.wait(timeout=1)
            except subprocess.TimeoutExpired:
                pass
        _reap_children()
        _revoke_session_at(directory_fd, session_name, session, "expired")
        raise ValueError("supervisor session has expired")
    transport = session["transport"]
    if transport["kind"] != "loopback-tcp" or transport["host"] != "127.0.0.1":
        raise ValueError("supervisor transport is not loopback-only")
    nonce = secrets.token_hex(32)
    challenge = ""
    deadline = time.monotonic() + timeout_ms / 1000
    request_started = False

    def remaining() -> float:
        available = deadline - time.monotonic()
        if available <= 0:
            raise TimeoutError(f"supervisor request exceeded {timeout_ms} milliseconds")
        return available

    try:
        with socket.create_connection(("127.0.0.1", transport["port"]), timeout=min(3, remaining())) as connection:
            connection.settimeout(remaining())
            from .scenario import load_json_text

            challenge_document = load_json_text(_receive_line(connection, 1024).decode("utf-8"))
            if (
                not isinstance(challenge_document, dict)
                or set(challenge_document) != {"schemaVersion", "sessionId", "challenge"}
                or challenge_document.get("schemaVersion") != "1.0.0"
                or challenge_document.get("sessionId") != session["sessionId"]
                or not isinstance(challenge_document.get("challenge"), str)
            ):
                raise ValueError("supervisor challenge contract is invalid")
            challenge = challenge_document["challenge"]
            if len(challenge) != 64 or any(character not in "0123456789abcdef" for character in challenge):
                raise ValueError("supervisor challenge is invalid")
            signed_request = {
                "schemaVersion": "1.0.0", "sessionId": session["sessionId"],
                "challenge": challenge, "nonce": nonce, "command": command,
            }
            request = {**signed_request, "authorization": _authorization(session["token"], signed_request)}
            payload = canonical_json(request).encode("utf-8")
            if len(payload) > MAX_REQUEST_BYTES:
                raise ValueError("supervisor request is too large")
            # From this point onward a transport failure is deliberately
            # ambiguous: the daemon may authenticate and apply the operation
            # even when its response cannot reach this client.
            connection.settimeout(remaining())
            request_started = True
            connection.sendall(payload)
            connection.shutdown(socket.SHUT_WR)
            chunks: list[bytes] = []
            size = 0
            while True:
                connection.settimeout(remaining())
                chunk = connection.recv(65536)
                if not chunk:
                    break
                size += len(chunk)
                if size > MAX_RESPONSE_BYTES:
                    raise ValueError("supervisor response exceeds 16777216 bytes")
                chunks.append(chunk)
    except (JsonDocumentError, OSError, UnicodeError, ValueError) as exc:
        state = "expired" if _utc_now() >= expires else "closed"
        _revoke_session_at(directory_fd, session_name, session, state)
        # Windows intentionally keeps directory handles open for the lifetime
        # of the daemon. Wait for its bounded poll loop after revocation so the
        # caller can immediately clean or reuse its test workspace.
        _wait_for_session_process(session)
        if request_started and (
            command == "scenario.authorize" or parse_resource_command(command) is not None
            or parse_client_command(command) is not None
        ):
            raise SupervisorMutationOutcomeUnknown(
                f"mutation outcome is unknown after transport failure; session was revoked and the command must not be retried automatically: {exc}",
                session["sessionId"],
            ) from exc
        raise
    try:
        envelope = load_json_text(b"".join(chunks).decode("utf-8"))
        if not isinstance(envelope, dict) or set(envelope) != {"result", "authorization"}:
            raise ValueError("supervisor response envelope is invalid")
        response = envelope["result"]
        if not isinstance(response, dict) or not isinstance(envelope["authorization"], str):
            raise ValueError("supervisor response contract is invalid")
        signed_response = {
            "sessionId": session["sessionId"], "challenge": challenge, "nonce": nonce,
            "command": command, "result": response,
        }
        expected_authorization = _authorization(session["token"], signed_response)
        if not hmac.compare_digest(envelope["authorization"], expected_authorization):
            raise ValueError("supervisor response authorization is invalid")
        expected_command = {
            "status": "supervisor.status", "shutdown": "supervisor.stop",
            "runtime.compare": "runtime.compare", "runtime.prove": "runtime.prove",
            "scenario.authorize": "scenario.authorize",
        }.get(command)
        if expected_command is None and parse_resource_command(command) is not None:
            expected_command = parse_resource_command(command)[0]
        if expected_command is None and parse_client_command(command) is not None:
            expected_command = "client.launch"
        if expected_command is None or response.get("command") != expected_command:
            raise ValueError("supervisor response command does not match the request")
        schema = (
            "neon-runtime-compare-result" if command == "runtime.compare"
            else "neon-proof-result" if command == "runtime.prove"
            else "neon-mutation-result" if command == "scenario.authorize" or parse_resource_command(command) is not None or parse_client_command(command) is not None
            else "neon-supervisor-result"
        )
        issues = schema_store.validate(schema, response)
        if issues:
            raise ValueError("supervisor response violates its advertised schema")
        identity = response.get("comparison", response.get("proof", response.get("session", response.get("operation", {})))).get("sessionId")
        if identity != session["sessionId"]:
            raise ValueError("supervisor response session does not match the request")
    except (AttributeError, JsonDocumentError, TypeError, UnicodeError, ValueError) as exc:
        state = "expired" if _utc_now() >= expires else "closed"
        _revoke_session_at(directory_fd, session_name, session, state)
        _wait_for_session_process(session)
        if command == "scenario.authorize" or parse_resource_command(command) is not None or parse_client_command(command) is not None:
            raise SupervisorMutationOutcomeUnknown(
                f"mutation outcome is unknown after an invalid authenticated response; session was revoked and the command must not be retried automatically: {exc}",
                session["sessionId"],
            ) from exc
        raise
    if command == "shutdown":
        process = _CHILDREN.get(session["pid"])
        if process is not None:
            try:
                process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                pass
        _reap_children()
    return response


def _revoke_session_at(directory_fd: int, session_name: str, session: dict[str, Any], state: str) -> None:
    revoked = dict(session)
    revoked["state"] = state
    revoked.pop("token", None)
    _atomic_write_at(directory_fd, session_name, canonical_json(revoked).encode("utf-8"))


def _audit(directory_fd: int, session_id: str, command: str, status: str, result: dict[str, Any]) -> None:
    record = {
        "schemaVersion": "1.0.0", "sessionId": session_id, "observedAt": _utc_text(_utc_now()),
        "command": command, "status": status,
        "resultSha256": sha256_bytes(canonical_json(result).encode("utf-8")),
    }
    payload = canonical_json(record).encode("utf-8")

    def mark_truncated() -> None:
        marker = {
            "schemaVersion": "1.0.0", "sessionId": session_id,
            "observedAt": _utc_text(_utc_now()), "status": "truncated",
            "maximumBytes": MAX_AUDIT_BYTES,
        }
        _atomic_write_at(directory_fd, "audit-truncated.json", canonical_json(marker).encode("utf-8"))

    if not _HAS_DIRECTORY_FD:
        _require_secure_filesystem()
        from . import winfs

        if not winfs.append_bounded_at(directory_fd, "audit.jsonl", payload, MAX_AUDIT_BYTES):
            mark_truncated()
        return
    flags = os.O_WRONLY | os.O_APPEND | os.O_CREAT
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    descriptor = os.open("audit.jsonl", flags, 0o600, dir_fd=directory_fd)
    if not stat.S_ISREG(os.fstat(descriptor).st_mode):
        os.close(descriptor)
        raise ValueError("supervisor audit path is unsafe")
    with os.fdopen(descriptor, "ab") as stream:
        if os.fstat(stream.fileno()).st_size + len(payload) > MAX_AUDIT_BYTES:
            mark_truncated()
            return
        stream.write(payload)
        stream.flush()
        os.fsync(stream.fileno())


def _failure_compare(code: str, message: str, project: dict[str, Any], session: dict[str, Any]) -> dict[str, Any]:
    result = {
        "schemaVersion": "1.0.0", "command": "runtime.compare", "status": "fail",
        "summary": {"errors": 1, "warnings": 0, "observations": 0, "functions": 0, "events": 0, "resources": 0, "modules": 0},
        "diagnostics": [_diagnostic(code, message)],
        "comparison": {
            "sessionId": session["sessionId"], "profile": project["profile"], "scope": "observation-only",
            "grantedEvidenceLabels": [], "observations": 0,
            "catalogueSha256": session["catalogue"]["sha256"], "projectSha256": session["project"]["sha256"],
            "snapshotSha256": "0" * 64, "runtimes": [],
        },
    }
    return result


def runtime_compare_failure(code: str, message: str) -> dict[str, Any]:
    return _failure_compare(
        code, message, {"profile": "unknown"},
        {
            "sessionId": "session:unavailable",
            "project": {"sha256": "0" * 64},
            "catalogue": {"sha256": "0" * 64},
        },
    )


def _write_session(directory_fd: int, session: dict[str, Any]) -> None:
    _atomic_write_at(directory_fd, "session.json", canonical_json(session).encode("utf-8"))


def _session_record_is_active(
    directory_fd: int, expected: dict[str, Any], schema_store: SchemaStore,
) -> bool:
    try:
        from .scenario import load_json_text

        record = load_json_text(_read_regular_at(directory_fd, "session.json", MAX_SESSION_BYTES).decode("utf-8"))
        return bool(
            isinstance(record, dict)
            and not schema_store.validate("neon-supervisor-session", record)
            and record.get("state") == "active"
            and record.get("sessionId") == expected.get("sessionId")
            and isinstance(record.get("token"), str)
            and isinstance(expected.get("token"), str)
            and hmac.compare_digest(record["token"], expected["token"])
            # An active session is an immutable capability contract. Checking
            # only its bearer would let a local file replacement silently
            # change the pinned inputs while the daemon answered for the old
            # contract.
            and hmac.compare_digest(canonical_json(record), canonical_json(expected))
        )
    except (JsonDocumentError, OSError, UnicodeError, ValueError):
        return False


def run_supervisor_daemon(
    workspace: Path,
    session_directory: Path,
    session_id: str,
    project_relative: str,
    catalogue_relative: str,
    snapshot_relative: str,
    ttl_seconds: int,
    schema_store: SchemaStore,
    enabled_capabilities: tuple[str, ...] = (),
    server_root: Path | None = None,
    client_root: Path | None = None,
    connect_port: int = 22003,
    test_client_adapter: bool = False,
) -> int:
    listener: socket.socket | None = None
    workspace_fd: int | None = None
    session_fd: int | None = None
    guardian_process: subprocess.Popen[bytes] | None = None
    guardian_connection: socket.socket | None = None
    guardian_listener: socket.socket | None = None
    driver_fallback: tuple[str, Any] | None = None
    driver_pid: int | None = None
    driver_sequence = 0
    client_processes: dict[str, tuple[subprocess.Popen[Any], int | None]] = {}
    try:
        workspace = workspace.resolve()
        if not session_directory.is_dir() or session_directory.is_symlink():
            raise ValueError("session directory is unsafe")
        session_directory.resolve().relative_to(workspace)
        session_relative = _relative(workspace, session_directory).as_posix()
        workspace_fd = _open_directory(workspace)
        session_fd = _open_directory_at(workspace_fd, session_relative)
        project_path, _ = _inside(workspace, Path(project_relative))
        catalogue_path, _ = _inside(workspace, Path(catalogue_relative))
        _inside(workspace, Path(snapshot_relative), allow_missing=True)
        from .scenario import load_json_text

        project_payload = _read_regular_at(workspace_fd, project_relative, 4 * 1024 * 1024)
        catalogue_payload = _read_regular_at(workspace_fd, catalogue_relative, 16 * 1024 * 1024)
        project = load_json_text(project_payload.decode("utf-8"))
        catalogue = load_json_text(catalogue_payload.decode("utf-8"))
        project_api = _resolve_project_anchored(
            workspace_fd, project_relative, project_payload, catalogue_relative,
            catalogue_payload, project, schema_store,
        )
        if project_api["status"] != "pass":
            raise ValueError("project resolution failed")
        project_contract_sha256 = sha256_bytes(canonical_json(project_api).encode("utf-8"))
        project_resources = {item["name"] for item in project.get("resources", [])}
        enabled = set(enabled_capabilities)
        if not enabled.issubset(MUTATION_CAPABILITIES):
            raise ValueError("daemon capability is not allowlisted")
        if "client.launch" in enabled and (
            "resource.lifecycle" not in enabled or server_root is None or client_root is None
            or project["profile"] not in {"neon-pair", "neon-multiclient"}
        ):
            raise ValueError("client.launch daemon boundary is incomplete")
        if isinstance(connect_port, bool) or not isinstance(connect_port, int) or not 1 <= connect_port <= 65535:
            raise ValueError("client connection port is invalid")
        created = _utc_now()
        expires = created + timedelta(seconds=ttl_seconds)
        monotonic_deadline = time.monotonic() + ttl_seconds
        token = secrets.token_hex(32)
        probe_config: dict[str, Any] | None = None
        approved_client: tuple[Path, str] | None = None
        if "client.launch" in enabled:
            probe = verify_runtime_probe(server_root)
            approved_client = client_executable(client_root, test_client_adapter)
            probe_session = {
                "sessionId": session_id, "profile": project["profile"],
                "createdAt": _utc_text(created), "expiresAt": _utc_text(expires),
                "project": {"sha256": sha256_bytes(project_payload)},
                "catalogue": {"sha256": sha256_bytes(catalogue_payload)},
            }
            probe_config = create_probe_config(probe_session, secrets.token_hex(32), created)
            arm_runtime_probe(server_root, probe_config, schema_store)
            project_resources.add(PROBE_RESOURCE)
        driver: dict[str, Any] | None = None
        if "resource.lifecycle" in enabled:
            if server_root is None:
                raise ValueError("resource.lifecycle has no approved MTA server root")
            executable, executable_sha256 = _server_executable(server_root)
            guardian_listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            guardian_listener.bind(("127.0.0.1", 0))
            guardian_listener.listen(1)
            guardian_listener.settimeout(15)
            guardian_token = secrets.token_hex(32)
            guardian_command = [
                sys.executable, os.fspath(Path(sys.argv[0]).resolve()), "_driver-guardian",
                "--server-root", os.fspath(server_root), "--expected-sha256", executable_sha256,
                "--host", "127.0.0.1", "--port", str(guardian_listener.getsockname()[1]),
                "--token", guardian_token,
            ]
            guardian_options: dict[str, Any] = {
                "cwd": workspace, "stdin": subprocess.DEVNULL, "stdout": subprocess.DEVNULL,
                "stderr": subprocess.DEVNULL, "close_fds": True,
            }
            if os.name == "nt":
                guardian_options["creationflags"] = subprocess.CREATE_NEW_PROCESS_GROUP
            else:
                guardian_options["start_new_session"] = True
            guardian_process = subprocess.Popen(guardian_command, **guardian_options)
            guardian_connection, guardian_address = guardian_listener.accept()
            if guardian_address[0] != "127.0.0.1":
                raise ValueError("driver guardian did not use the bounded loopback transport")
            guardian_connection.settimeout(3)
            ready = load_json_text(_receive_line(guardian_connection, 4096).decode("utf-8"))
            if isinstance(ready, dict) and set(ready) == {"type", "message", "authorization"}:
                signed_failure = {"type": ready["type"], "message": ready["message"]}
                if (
                    ready["type"] != "error" or not isinstance(ready["message"], str)
                    or not isinstance(ready["authorization"], str)
                    or not hmac.compare_digest(
                        ready["authorization"], _authorization(guardian_token, signed_failure),
                    )
                ):
                    raise ValueError("driver guardian failure proof is invalid")
                raise ValueError(ready["message"])
            if not isinstance(ready, dict) or set(ready) != {
                "type", "pid", "executable", "executableSha256", "authorization",
            }:
                raise ValueError("driver guardian readiness contract is invalid")
            signed_ready = {key: ready[key] for key in ("type", "pid", "executable", "executableSha256")}
            if (
                ready["type"] != "ready"
                or isinstance(ready["pid"], bool) or not isinstance(ready["pid"], int) or ready["pid"] < 1
                or ready["executable"] != executable.name
                or ready["executableSha256"] != executable_sha256
                or not isinstance(ready["authorization"], str)
                or not hmac.compare_digest(ready["authorization"], _authorization(guardian_token, signed_ready))
            ):
                raise ValueError("driver guardian readiness proof is invalid")
            guardian_listener.close()
            guardian_listener = None
            driver_pid = ready["pid"]
            driver_fallback = _open_driver_fallback(driver_pid)
            driver = {
                "kind": "mta-server-stdio", "root": os.fspath(server_root.resolve()),
                "executable": executable.name, "executableSha256": executable_sha256,
                "pid": driver_pid, "guardianPid": guardian_process.pid,
            }
        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listener.bind(("127.0.0.1", 0))
        listener.listen(8)
        listener.settimeout(0.5)
        session = {
            "schemaVersion": "1.0.0", "sessionId": session_id, "state": "active", "profile": project["profile"],
            "createdAt": _utc_text(created), "expiresAt": _utc_text(expires),
            "capabilities": sorted([*CAPABILITIES, *enabled]),
            "project": {
                "path": project_relative, "sha256": sha256_bytes(project_payload),
                "contractSha256": project_contract_sha256,
            },
            "catalogue": {"path": catalogue_relative, "sha256": sha256_bytes(catalogue_payload)},
            "snapshot": {"path": snapshot_relative},
            "transport": {"kind": "loopback-tcp", "host": "127.0.0.1", "port": listener.getsockname()[1]},
            "pid": os.getpid(), "token": token,
        }
        if driver is not None:
            session["driver"] = driver
        if approved_client is not None and probe_config is not None:
            executable, executable_sha256 = approved_client
            session["client"] = {
                "root": os.fspath(client_root.resolve()), "executable": executable.name,
                "executableSha256": executable_sha256,
                "connectUri": f"mtasa://127.0.0.1:{connect_port}",
            }
            session["probe"] = {
                "resource": PROBE_RESOURCE, "sourceSha256": probe["sourceSha256"],
                "challenge": probe_config["challenge"], "expectedClients": probe_config["expectedClients"],
                "report": f"{PROBE_RESOURCE}/{REPORT_FILE}",
            }
        issues = schema_store.validate("neon-supervisor-session", session)
        if issues:
            raise ValueError("generated supervisor session is invalid")
        _write_session(session_fd, session)
        relative = f"{session_relative}/session.json"
        started_result = _supervisor_result("supervisor.start", session, relative, _regular_at(workspace_fd, snapshot_relative))
        _audit(session_fd, session_id, "supervisor.start", "pass", started_result)

        def finalize_session(state: str, audit_command: str, *, persist: bool) -> None:
            session["state"] = state
            session.pop("token", None)
            if persist:
                _write_session(session_fd, session)
            final_result = _supervisor_result(
                "supervisor.stop", session, relative, _regular_at(workspace_fd, snapshot_relative), state=state,
            )
            _audit(session_fd, session_id, audit_command, "pass", final_result)

        stop = False
        while not stop:
            if not _session_record_is_active(session_fd, session, schema_store):
                finalize_session("closed", "supervisor.revoke", persist=False)
                break
            if time.monotonic() >= monotonic_deadline:
                finalize_session("expired", "supervisor.expire", persist=True)
                break
            try:
                connection, address = listener.accept()
            except socket.timeout:
                continue
            if not _session_record_is_active(session_fd, session, schema_store):
                connection.close()
                finalize_session("closed", "supervisor.revoke", persist=False)
                break
            with connection:
                requested_command = "status"
                nonce = secrets.token_hex(32)
                challenge = secrets.token_hex(32)
                try:
                    connection.settimeout(3)
                    connection.sendall(canonical_json({
                        "schemaVersion": "1.0.0", "sessionId": session_id, "challenge": challenge,
                    }).encode("utf-8"))
                    payload = bytearray()
                    while len(payload) <= MAX_REQUEST_BYTES:
                        chunk = connection.recv(65536)
                        if not chunk:
                            break
                        payload.extend(chunk)
                    if time.monotonic() >= monotonic_deadline:
                        finalize_session("expired", "supervisor.expire", persist=True)
                        break
                    if not _session_record_is_active(session_fd, session, schema_store):
                        finalize_session("closed", "supervisor.revoke", persist=False)
                        break
                    if address[0] != "127.0.0.1" or len(payload) > MAX_REQUEST_BYTES:
                        raise ValueError("request is not a bounded loopback request")
                    request = load_json_text(payload.decode("utf-8"))
                    if not isinstance(request, dict) or set(request) != {"schemaVersion", "sessionId", "challenge", "nonce", "command", "authorization"}:
                        raise ValueError("request contract is invalid")
                    if not all(isinstance(request[key], str) for key in request):
                        raise ValueError("request fields must be strings")
                    requested_command = request["command"]
                    nonce = request["nonce"]
                    if request["challenge"] != challenge:
                        raise ValueError("request challenge is invalid or replayed")
                    if len(nonce) != 64 or any(character not in "0123456789abcdef" for character in nonce):
                        raise ValueError("request nonce is invalid")
                    if len(request["authorization"]) != 64:
                        raise ValueError("request authorization is invalid")
                    if not isinstance(request["schemaVersion"], str) or not isinstance(request["sessionId"], str):
                        raise ValueError("request session identity is invalid")
                    if schema_major(request["schemaVersion"]) != 1 or request["sessionId"] != session_id:
                        raise ValueError("request session identity is invalid")
                    signed_request = {
                        key: request[key] for key in ("schemaVersion", "sessionId", "challenge", "nonce", "command")
                    }
                    if not hmac.compare_digest(request["authorization"], _authorization(token, signed_request)):
                        raise ValueError("request authorization is invalid")
                    command = request["command"]
                    if not isinstance(command, str):
                        raise ValueError("request command is invalid")
                    if time.monotonic() >= monotonic_deadline:
                        finalize_session("expired", "supervisor.expire", persist=True)
                        break
                    if not _session_record_is_active(session_fd, session, schema_store):
                        finalize_session("closed", "supervisor.revoke", persist=False)
                        break
                    available = _regular_at(workspace_fd, snapshot_relative)
                    if command == "status":
                        result = _supervisor_result("supervisor.status", session, relative, available)
                    elif command == "runtime.compare":
                        try:
                            _inside(workspace, Path(snapshot_relative), allow_missing=True)
                            current_project_payload = _read_regular_at(workspace_fd, project_relative, 4 * 1024 * 1024)
                            current_catalogue_payload = _read_regular_at(workspace_fd, catalogue_relative, 16 * 1024 * 1024)
                        except (OSError, ValueError):
                            result = _failure_compare("SUPERVISOR_INPUT_UNSAFE", "configured input path became unsafe after session start", project, session)
                        else:
                            result = None
                        if result is not None:
                            pass
                        elif sha256_bytes(current_project_payload) != session["project"]["sha256"] or sha256_bytes(current_catalogue_payload) != session["catalogue"]["sha256"]:
                            result = _failure_compare("SUPERVISOR_INPUT_DRIFT", "project or catalogue changed after session start", project, session)
                        else:
                            current_project_api = _resolve_project_anchored(
                                workspace_fd, project_relative, current_project_payload,
                                catalogue_relative, current_catalogue_payload, project, schema_store,
                            )
                            current_contract_sha256 = sha256_bytes(canonical_json(current_project_api).encode("utf-8"))
                            if current_project_api["status"] != "pass" or current_contract_sha256 != session["project"]["contractSha256"]:
                                result = _failure_compare("SUPERVISOR_INPUT_DRIFT", "project component contracts changed after session start", project, session)
                            else:
                                try:
                                    snapshot_payload = _read_regular_at(workspace_fd, snapshot_relative, 16 * 1024 * 1024)
                                except FileNotFoundError:
                                    result = _failure_compare("RUNTIME_SNAPSHOT_UNAVAILABLE", "configured runtime snapshot is not available as a regular file", project, session)
                                except (OSError, ValueError):
                                    result = _failure_compare("SUPERVISOR_INPUT_UNSAFE", "configured runtime snapshot path is unsafe", project, session)
                                else:
                                    result = compare_runtime_snapshot(
                                        None, project, session["project"]["sha256"], catalogue,
                                        session["catalogue"]["sha256"], current_project_api, session_id, schema_store,
                                        session["createdAt"], session["expiresAt"], snapshot_payload,
                                    )
                        result_issues = schema_store.validate("neon-runtime-compare-result", result)
                        if result_issues:
                            raise ValueError("runtime comparator produced an invalid result")
                    elif command == "runtime.prove":
                        driver_sequence += 1
                        if "client.launch" not in session["capabilities"] or probe_config is None or server_root is None:
                            result = proof_failure(
                                "SUPERVISOR_CAPABILITY_DENIED", "client.launch is not enabled for authenticated runtime proof", session,
                            )
                        elif (
                            guardian_process is None or guardian_process.poll() is not None
                            or guardian_connection is None
                        ):
                            result = proof_failure(
                                "PROBE_SERVER_PROCESS_EXITED",
                                "the supervisor-owned MTA server is not alive at proof time",
                                session,
                            )
                        elif not _query_guardian_alive(guardian_connection, driver_sequence):
                            result = proof_failure(
                                "PROBE_SERVER_PROCESS_EXITED",
                                "the supervisor-owned MTA server is not alive at proof time",
                                session,
                            )
                        elif set(client_processes) != {
                            f"client-{ordinal}" for ordinal in range(1, probe_config["expectedClients"] + 1)
                        }:
                            result = proof_failure(
                                "PROBE_CLIENT_ROLES_NOT_LAUNCHED",
                                "every client role in the selected topology must be launched by this supervisor before proof",
                                session,
                            )
                        elif any(process.poll() is not None for process, _ in client_processes.values()):
                            result = proof_failure(
                                "PROBE_CLIENT_PROCESS_EXITED",
                                "a supervisor-launched client role exited before authenticated proof completed",
                                session,
                            )
                        else:
                            try:
                                report = load_probe_report(server_root)
                            except FileNotFoundError:
                                result = proof_failure("PROBE_NOT_READY", "authenticated runtime probe report is not ready", session)
                            except (JsonDocumentError, OSError, UnicodeError, ValueError) as exc:
                                result = proof_failure("PROBE_REPORT_UNSAFE", str(exc), session)
                            else:
                                result, snapshot = prove_runtime(
                                    report, probe_config, probe_config["secret"], session,
                                    project, catalogue, project_api, schema_store,
                                )
                                if result["status"] == "pass" and snapshot is not None:
                                    snapshot_path = PurePosixPath(snapshot_relative)
                                    parent = snapshot_path.parent.as_posix()
                                    snapshot_fd = workspace_fd if parent == "." else _ensure_directory_at(workspace_fd, parent)
                                    try:
                                        _atomic_write_at(
                                            snapshot_fd, snapshot_path.name,
                                            canonical_json(snapshot).encode("utf-8"),
                                        )
                                    finally:
                                        if snapshot_fd != workspace_fd:
                                            _close_directory(snapshot_fd)
                        result_issues = schema_store.validate("neon-proof-result", result)
                        evidence = result.get("proof", {}).get("evidence")
                        if evidence is not None:
                            result_issues.extend(schema_store.validate("neon-evidence", evidence))
                        if result_issues:
                            raise ValueError("runtime proof adapter produced an invalid result")
                    elif command == "scenario.authorize":
                        if "scenario.execute" not in session["capabilities"]:
                            result = mutation_failure(command, "SUPERVISOR_CAPABILITY_DENIED", "scenario.execute is not enabled", session_id=session_id)
                        else:
                            result = mutation_result("scenario.authorize", session_id, project["name"])
                        if schema_store.validate("neon-mutation-result", result):
                            raise ValueError("scenario authorization produced an invalid result")
                    elif parse_resource_command(command) is not None:
                        public_command, resource_name = parse_resource_command(command)
                        if "resource.lifecycle" not in session["capabilities"]:
                            result = mutation_failure(public_command, "SUPERVISOR_CAPABILITY_DENIED", "resource.lifecycle is not enabled", resource_name, session_id)
                        elif resource_name not in project_resources:
                            result = mutation_failure(public_command, "RESOURCE_TARGET_UNDECLARED", "resource is not declared by the pinned project", resource_name, session_id)
                        elif guardian_process is None or guardian_process.poll() is not None or guardian_connection is None:
                            if driver_pid is not None:
                                _terminate_driver_fallback(driver_fallback, driver_pid)
                                _close_driver_fallback(driver_fallback)
                                driver_fallback = None
                                driver_pid = None
                            result = mutation_failure(public_command, "MTA_SERVER_UNAVAILABLE", "approved MTA server process is not running", resource_name, session_id)
                        else:
                            console_action = public_command.removeprefix("resource.")
                            driver_sequence += 1
                            try:
                                driver_status, _ = _submit_guardian_command(
                                    guardian_connection, driver_sequence, console_action, resource_name,
                                )
                            except (JsonDocumentError, OSError, UnicodeError, ValueError) as exc:
                                driver_status = "unknown"
                                driver_message = str(exc)
                            if driver_status == "backpressure":
                                result = mutation_failure(
                                    public_command, "MTA_SERVER_INPUT_BACKPRESSURE",
                                    "approved MTA server input is saturated; Neon did not submit or retry the command",
                                    resource_name, session_id,
                                )
                            elif driver_status == "unavailable":
                                result = mutation_failure(
                                    public_command, "MTA_SERVER_UNAVAILABLE",
                                    "approved MTA server process is not running", resource_name, session_id,
                                )
                            elif driver_status in {"partial", "unknown"}:
                                result = mutation_failure(
                                    public_command, "MUTATION_OUTCOME_UNKNOWN",
                                    "MTA server command acknowledgement was incomplete; runtime outcome is unknown and the session must not be reused"
                                    + (f": {driver_message}" if driver_status == "unknown" else ""),
                                    resource_name, session_id,
                                )
                                session["state"] = "closed"
                                session.pop("token", None)
                                _write_session(session_fd, session)
                                stop = True
                            else:
                                result = mutation_result(
                                    public_command, session_id, resource_name, status="pass",
                                    code="RESOURCE_COMMAND_SUBMITTED",
                                    message="Neon submitted the bounded command to the approved MTA server input; processing and runtime state are not claimed until observed",
                                )
                        result_issues = schema_store.validate("neon-mutation-result", result)
                        if result_issues:
                            raise ValueError("mutation adapter produced an invalid result")
                    elif parse_client_command(command) is not None:
                        public_command, role, ordinal = parse_client_command(command)
                        if "client.launch" not in session["capabilities"] or approved_client is None or client_root is None:
                            result = mutation_failure(public_command, "SUPERVISOR_CAPABILITY_DENIED", "client.launch is not enabled", role, session_id)
                        elif probe_config is None or ordinal > probe_config["expectedClients"]:
                            result = mutation_failure(public_command, "CLIENT_ROLE_OUT_OF_SCOPE", "client role exceeds the selected runtime topology", role, session_id)
                        elif role in client_processes:
                            result = mutation_failure(public_command, "CLIENT_ROLE_ALREADY_LAUNCHED", "client role was already launched in this session", role, session_id)
                        else:
                            executable, expected_sha256 = approved_client
                            current_executable, current_sha256 = client_executable(client_root, test_client_adapter)
                            if current_executable != executable or not hmac.compare_digest(current_sha256, expected_sha256):
                                result = mutation_failure(public_command, "CLIENT_EXECUTABLE_DRIFT", "approved client executable changed after session start", role, session_id)
                            else:
                                arguments = [os.fspath(executable)]
                                if ordinal > 1:
                                    arguments.append(f"-cl{ordinal}")
                                arguments.append(session["client"]["connectUri"])
                                options: dict[str, Any] = {
                                    "cwd": client_root, "stdin": subprocess.DEVNULL,
                                    "stdout": subprocess.DEVNULL, "stderr": subprocess.DEVNULL,
                                    "close_fds": True,
                                }
                                if os.name == "nt":
                                    from . import winfs

                                    # The launcher must not execute long enough
                                    # to escape through a child before the job
                                    # object owns its complete process tree.
                                    options["creationflags"] = winfs.CREATE_SUSPENDED
                                else:
                                    options["start_new_session"] = True
                                process = subprocess.Popen(arguments, **options)
                                job_handle: int | None = None
                                try:
                                    launched_executable, launched_sha256 = client_executable(client_root, test_client_adapter)
                                    if launched_executable != executable or not hmac.compare_digest(
                                        launched_sha256, expected_sha256,
                                    ):
                                        raise ValueError("approved client executable changed while it was being launched")
                                    if os.name == "nt":
                                        from . import winfs

                                        job_handle = winfs.create_kill_on_close_job(int(process._handle))
                                        winfs.resume_suspended_process(process.pid)
                                except Exception:
                                    if job_handle is not None:
                                        from . import winfs

                                        winfs.close(job_handle)
                                    if process.poll() is None:
                                        try:
                                            process.terminate()
                                            process.wait(timeout=3)
                                        except (OSError, subprocess.TimeoutExpired):
                                            if process.poll() is None:
                                                process.kill()
                                                process.wait(timeout=2)
                                    raise
                                client_processes[role] = (process, job_handle)
                                result = mutation_result(
                                    public_command, session_id, role, status="pass",
                                    code="CLIENT_PROCESS_LAUNCHED",
                                    message="Neon launched the twice-verified executable path inside the approved Windows client root; connection and in-game state are not claimed until authenticated proof succeeds",
                                )
                        if schema_store.validate("neon-mutation-result", result):
                            raise ValueError("client launch adapter produced an invalid result")
                    elif command == "shutdown":
                        session["state"] = "closed"
                        session.pop("token", None)
                        _write_session(session_fd, session)
                        result = _supervisor_result("supervisor.stop", session, relative, available, state="closed")
                        stop = True
                    else:
                        raise ValueError("command is not read-only allowlisted")
                except Exception as exc:
                    if requested_command == "runtime.compare":
                        result = _failure_compare("SUPERVISOR_REQUEST_REJECTED", str(exc), project, session)
                    elif requested_command == "runtime.prove":
                        result = proof_failure("SUPERVISOR_REQUEST_REJECTED", str(exc), session)
                    elif (
                        requested_command == "scenario.authorize" or parse_resource_command(requested_command) is not None
                        or parse_client_command(requested_command) is not None
                    ):
                        parsed = parse_resource_command(requested_command)
                        parsed_client = parse_client_command(requested_command)
                        public_command = parsed[0] if parsed is not None else parsed_client[0] if parsed_client is not None else "scenario.authorize"
                        target = parsed[1] if parsed is not None else parsed_client[1] if parsed_client is not None else project.get("name", "unavailable")
                        result = mutation_failure(public_command, "SUPERVISOR_REQUEST_REJECTED", str(exc), target, session_id)
                    else:
                        public_command = "supervisor.stop" if requested_command == "shutdown" else "supervisor.status"
                        result = {
                            "schemaVersion": "1.0.0", "command": public_command, "status": "fail",
                            "summary": {"errors": 1, "warnings": 0},
                            "diagnostics": [_diagnostic("SUPERVISOR_REQUEST_REJECTED", str(exc))],
                            "session": _public_session(session, relative, _regular_at(workspace_fd, snapshot_relative)),
                        }
                    command = "rejected"
                if requested_command != "shutdown" and not stop:
                    if time.monotonic() >= monotonic_deadline:
                        finalize_session("expired", "supervisor.expire", persist=True)
                        break
                    if not _session_record_is_active(session_fd, session, schema_store):
                        finalize_session("closed", "supervisor.revoke", persist=False)
                        break
                try:
                    envelope = _response_envelope(token, session_id, challenge, nonce, requested_command, result)
                    response = canonical_json(envelope).encode("utf-8")
                    if len(response) > MAX_RESPONSE_BYTES:
                        result = (
                            _failure_compare("SUPERVISOR_RESPONSE_TOO_LARGE", "bounded supervisor response limit exceeded", project, session)
                            if requested_command == "runtime.compare"
                            else proof_failure("SUPERVISOR_RESPONSE_TOO_LARGE", "bounded supervisor response limit exceeded", session)
                            if requested_command == "runtime.prove"
                            else mutation_failure(
                                parse_resource_command(requested_command)[0] if parse_resource_command(requested_command) else parse_client_command(requested_command)[0] if parse_client_command(requested_command) else "scenario.authorize",
                                "SUPERVISOR_RESPONSE_TOO_LARGE", "bounded supervisor response limit exceeded",
                                parse_resource_command(requested_command)[1] if parse_resource_command(requested_command) else parse_client_command(requested_command)[1] if parse_client_command(requested_command) else project.get("name", "unavailable"),
                                session_id,
                            ) if requested_command == "scenario.authorize" or parse_resource_command(requested_command) is not None or parse_client_command(requested_command) is not None
                            else supervisor_failure("supervisor.status", "SUPERVISOR_RESPONSE_TOO_LARGE", "bounded supervisor response limit exceeded")
                        )
                        envelope = _response_envelope(token, session_id, challenge, nonce, requested_command, result)
                        response = canonical_json(envelope).encode("utf-8")
                    _audit(session_fd, session_id, command, result["status"], result)
                    connection.sendall(response)
                except (BrokenPipeError, ConnectionError, OSError, TimeoutError):
                    # A local peer may disappear at any byte boundary. Its transport failure
                    # must not terminate or revoke the supervisor's bounded session.
                    continue
        return 0
    except Exception as exc:
        if session_fd is not None:
            try:
                _atomic_write_at(session_fd, "startup-error.json", canonical_json({"message": str(exc)}).encode("utf-8"))
            except OSError:
                pass
        return 1
    finally:
        if listener is not None:
            listener.close()
        if guardian_listener is not None:
            guardian_listener.close()
        if guardian_connection is not None:
            guardian_connection.close()
        if guardian_process is not None and guardian_process.poll() is None:
            try:
                guardian_process.wait(timeout=4)
            except subprocess.TimeoutExpired:
                guardian_process.terminate()
                try:
                    guardian_process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    guardian_process.kill()
                    guardian_process.wait(timeout=2)
        if driver_pid is not None:
            _terminate_driver_fallback(driver_fallback, driver_pid)
        _close_driver_fallback(driver_fallback)
        for process, job_handle in client_processes.values():
            if job_handle is not None:
                from . import winfs

                winfs.close(job_handle)
            if process.poll() is None and os.name != "nt":
                try:
                    os.killpg(process.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
            if process.poll() is None:
                if os.name == "nt":
                    process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    if os.name == "nt":
                        process.kill()
                    else:
                        try:
                            os.killpg(process.pid, signal.SIGKILL)
                        except ProcessLookupError:
                            pass
                    process.wait(timeout=2)
        if session_fd is not None:
            _close_directory(session_fd)
        if workspace_fd is not None:
            _close_directory(workspace_fd)
