from __future__ import annotations

import re
from datetime import datetime, timezone
from typing import Any


RESOURCE_COMMAND = re.compile(r"^(resource\.(?:start|stop|restart))/([A-Za-z0-9_.-]{1,128})$")
CLIENT_COMMAND = re.compile(r"^(client\.launch)/(client-([1-8]))$")


def utc_text() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def parse_resource_command(command: str) -> tuple[str, str] | None:
    match = RESOURCE_COMMAND.fullmatch(command)
    return (match.group(1), match.group(2)) if match else None


def parse_client_command(command: str) -> tuple[str, str, int] | None:
    match = CLIENT_COMMAND.fullmatch(command)
    return (match.group(1), match.group(2), int(match.group(3))) if match else None


def mutation_result(
    command: str,
    session_id: str,
    target: str,
    *,
    status: str = "pass",
    code: str | None = None,
    message: str | None = None,
) -> dict[str, Any]:
    diagnostic = []
    if code is not None and message is not None:
        diagnostic.append({
            "code": code,
            "severity": "error" if status == "fail" else "warning",
            "message": message[:1024],
            "path": ".",
        })
    if command.startswith("resource."):
        capability = "resource.lifecycle"
        scope = (
            "command-submitted" if status == "pass"
            else "outcome-unknown" if code == "MUTATION_OUTCOME_UNKNOWN"
            else "not-submitted"
        )
    elif command.startswith("client."):
        capability = "client.launch"
        scope = (
            "process-launched" if status == "pass"
            else "outcome-unknown" if code == "MUTATION_OUTCOME_UNKNOWN"
            else "not-launched"
        )
    else:
        capability, scope = "scenario.execute", "authorization-only"
    return {
        "schemaVersion": "1.0.0",
        "command": command,
        "status": status,
        "summary": {
            "errors": sum(item["severity"] == "error" for item in diagnostic),
            "warnings": sum(item["severity"] == "warning" for item in diagnostic),
        },
        "diagnostics": diagnostic,
        "operation": {
            "sessionId": session_id,
            "capability": capability,
            "target": target,
            "scope": scope,
            "grantedEvidenceLabels": [],
            "idempotent": command in {"resource.start", "resource.stop", "scenario.authorize"},
            "completedAt": utc_text(),
        },
    }


def mutation_failure(
    command: str, code: str, message: str, target: str = "unavailable",
    session_id: str = "session:unavailable",
) -> dict[str, Any]:
    public = command if command in {"resource.start", "resource.stop", "resource.restart", "scenario.authorize", "client.launch"} else "scenario.authorize"
    return mutation_result(public, session_id, target, status="fail", code=code, message=message)
