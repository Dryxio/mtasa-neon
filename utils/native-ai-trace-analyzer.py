#!/usr/bin/env python3
"""Find the first causal divergence in a Neon native-AI harness run."""

from __future__ import annotations

import argparse
import json
import math
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any, Iterable


SCHEMA = "neon.native_ai.telemetry"
HARNESS_SCHEMA = "neon.native_ai.harness"
POPULATION_SCHEMA = "neon.ped_traffic.population"
POPULATION_SCHEMA_VERSION = 1
# This is an analyzer liveness bound, not a GTA population constant. A probe
# still pending this long after newer telemetry was emitted is useful evidence
# of a stuck orchestration path, regardless of the configured runtime timeout.
POPULATION_STALE_CHECK_MS = 5000.0
SPATIAL_MAX_DISTANCE = 1.0
SPATIAL_MAX_HEADING_DEGREES = 45.0
PAIR_WINDOW_MS = 750.0
ROTATION_CONVERGENCE_DEGREES = 2.0
ROTATION_RENDER_GRACE_SLACK_MS = 100.0
ROTATION_PREVIOUS_TARGET_DEGREES = 15.0
ROTATION_ONE_BEHIND_MIN_DEGREES = 45.0
ROTATION_ONE_BEHIND_EVALUATION_MS = 50.0
MELEE_CAUSAL_STAGES = (
    "action_dispatched",
    "owner_native_damage_attempt",
    "server_validated_forward",
    "victim_injection_result",
    "final_observation",
)


@dataclass
class Finding:
    timestamp_ms: float
    stage: str
    message: str
    evidence: dict[str, Any]
    severity: str = "error"


def parse_wall_time(value: Any) -> float:
    if not isinstance(value, str):
        return math.inf
    try:
        return datetime.fromisoformat(value.replace("Z", "+00:00")).timestamp() * 1000.0
    except ValueError:
        return math.inf


def read_jsonl(path: Path, source: str) -> tuple[list[dict[str, Any]], list[Finding]]:
    records: list[dict[str, Any]] = []
    findings: list[Finding] = []
    if not path.exists():
        findings.append(Finding(-math.inf, "input", f"Missing {source} trace", {"path": str(path)}))
        return records, findings

    with path.open("r", encoding="utf-8", errors="replace") as stream:
        for line_number, line in enumerate(stream, 1):
            if not line.strip():
                continue
            try:
                record = json.loads(line)
            except json.JSONDecodeError as error:
                findings.append(
                    Finding(
                        -math.inf,
                        "jsonl",
                        f"Invalid JSONL in {source} at line {line_number}",
                        {"path": str(path), "line": line_number, "error": str(error)},
                    )
                )
                continue
            # MTA's toJSON wraps keyed Lua tables in a one-element JSON array.
            # Accept existing harness traces while the writer migrates to plain
            # object-per-line JSONL.
            if isinstance(record, list) and len(record) == 1 and isinstance(record[0], dict):
                record = record[0]
            if isinstance(record, dict):
                record["_source"] = source
                record["_line"] = line_number
                record["_timestamp_ms"] = parse_wall_time(record.get("wall_utc"))
                records.append(record)
            else:
                findings.append(
                    Finding(
                        -math.inf,
                        "jsonl",
                        f"JSONL record in {source} at line {line_number} is not an object",
                        {"path": str(path), "line": line_number, "value_type": type(record).__name__},
                    )
                )
    return records, findings


def trace_context(record: dict[str, Any]) -> dict[str, Any]:
    trace = record.get("trace")
    return trace if isinstance(trace, dict) else record


def run_id(record: dict[str, Any]) -> Any:
    return trace_context(record).get("run_id")


def select_run(records: Iterable[dict[str, Any]], requested: str | None) -> str | None:
    if requested and requested != "latest":
        return requested
    candidates = [record for record in records if run_id(record)]
    if not candidates:
        return None
    latest = max(
        candidates,
        key=lambda item: item.get("_timestamp_ms", -math.inf) if math.isfinite(item.get("_timestamp_ms", math.inf)) else -math.inf,
    )
    return str(run_id(latest))


def identity(record: dict[str, Any]) -> tuple[Any, ...]:
    trace = trace_context(record)
    ped = record.get("ped") if isinstance(record.get("ped"), dict) else {}
    return (
        trace.get("actor_id"),
        ped.get("traffic_id"),
        ped.get("mta_element_id"),
        ped.get("owner_epoch"),
    )


def actor_label(record: dict[str, Any]) -> str:
    trace = trace_context(record)
    ped = record.get("ped") if isinstance(record.get("ped"), dict) else {}
    return str(trace.get("actor_id") or record.get("actor_id") or ped.get("traffic_id") or ped.get("mta_element_id") or "unknown")


def packet(record: dict[str, Any]) -> dict[str, Any]:
    value = record.get("packet")
    return value if isinstance(value, dict) else {}


def sample(record: dict[str, Any]) -> dict[str, Any]:
    value = record.get("sample")
    return value if isinstance(value, dict) else {}


def vector_distance(left: Any, right: Any) -> float | None:
    if not (isinstance(left, list) and isinstance(right, list) and len(left) == 3 and len(right) == 3):
        return None
    if not all(isinstance(value, (int, float)) and math.isfinite(value) for value in left + right):
        return None
    return math.sqrt(sum((float(a) - float(b)) ** 2 for a, b in zip(left, right)))


def heading_delta(left: Any, right: Any) -> float | None:
    if not isinstance(left, (int, float)) or not isinstance(right, (int, float)):
        return None
    if not math.isfinite(float(left)) or not math.isfinite(float(right)):
        return None
    # Native telemetry headings are radians. Return degrees so diagnostics and
    # thresholds cannot silently mix the two units.
    radians = abs((float(left) - float(right) + math.pi) % (2.0 * math.pi) - math.pi)
    return math.degrees(radians)


def record_evidence(record: dict[str, Any]) -> dict[str, Any]:
    return {
        "source": record.get("_source"),
        "line": record.get("_line"),
        "wall_utc": record.get("wall_utc"),
        "client": record.get("client_identity"),
        "event": record.get("event"),
        "actor": actor_label(record),
        "action_id": trace_context(record).get("action_id") or record.get("action_id"),
        "packet": packet(record),
        "rotation": record.get("rotation") if isinstance(record.get("rotation"), dict) else None,
    }


def population_evidence(record: dict[str, Any]) -> dict[str, Any]:
    return {
        "source": record.get("_source"),
        "line": record.get("_line"),
        "wall_utc": record.get("wall_utc"),
        "monotonic_ms": record.get("monotonic_ms"),
        "event_sequence": record.get("event_sequence"),
        "event": record.get("event"),
        "world_revision": record.get("world_revision"),
        "player_id": record.get("player_id"),
        "request_id": record.get("request_id"),
        "visibility_check_id": record.get("visibility_check_id"),
        "traffic_id": record.get("traffic_id"),
        "group_id": record.get("group_id"),
    }


def population_clock(record: dict[str, Any]) -> float:
    value = record.get("monotonic_ms")
    if isinstance(value, (int, float)) and math.isfinite(value):
        return float(value)
    value = record.get("_timestamp_ms", math.inf)
    return float(value) if isinstance(value, (int, float)) and math.isfinite(value) else math.inf


def finite_number(value: Any) -> float | None:
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        return None
    return float(value)


def nested_number(record: dict[str, Any], container: str, key: str) -> float | None:
    value = record.get(container)
    return finite_number(value.get(key)) if isinstance(value, dict) else None


def counter_dict(counter: Counter[str]) -> dict[str, int]:
    return dict(sorted(counter.items()))


def analyze_sequences(records: list[dict[str, Any]]) -> list[Finding]:
    findings: list[Finding] = []
    groups: dict[tuple[Any, Any], list[dict[str, Any]]] = defaultdict(list)
    for record in records:
        if record.get("schema") == SCHEMA:
            groups[(record.get("client_identity"), record.get("process_id"))].append(record)
    for key, group in groups.items():
        previous = 0
        for record in sorted(group, key=lambda item: item.get("event_sequence", 0)):
            sequence = record.get("event_sequence")
            if not isinstance(sequence, int):
                continue
            if previous and sequence != previous + 1:
                findings.append(
                    Finding(
                        record["_timestamp_ms"],
                        "writer",
                        f"Telemetry sequence gap for {key[0]} process {key[1]}",
                        {**record_evidence(record), "previous": previous, "current": sequence},
                    )
                )
            previous = sequence
            if record.get("dropped_before", 0):
                findings.append(
                    Finding(
                        record["_timestamp_ms"],
                        "writer",
                        f"Telemetry dropped {record['dropped_before']} records",
                        record_evidence(record),
                    )
                )
    return findings


def analyze_ownership(records: list[dict[str, Any]]) -> list[Finding]:
    findings: list[Finding] = []
    epochs: dict[tuple[Any, ...], int] = {}
    collision_holds: dict[tuple[Any, ...], dict[str, Any]] = {}
    for record in sorted(records, key=lambda item: item["_timestamp_ms"]):
        event = record.get("event")
        ped = record.get("ped") if isinstance(record.get("ped"), dict) else {}
        ped_key = (
            record.get("client_identity"),
            record.get("process_id"),
            ped.get("traffic_id"),
            ped.get("mta_element_id"),
        )
        if event == "owner_collision_hold_started":
            if ped_key in collision_holds:
                findings.append(
                    Finding(record["_timestamp_ms"], "owner_collision", "Owner collision hold started twice", record_evidence(record))
                )
            collision_holds[ped_key] = record
        elif event == "owner_collision_hold_released":
            if collision_holds.pop(ped_key, None) is None:
                findings.append(
                    Finding(
                        record["_timestamp_ms"],
                        "owner_collision",
                        "Owner collision hold released without a matching start",
                        record_evidence(record),
                        "warning",
                    )
                )
        elif event == "packet_serialize" and ped_key in collision_holds:
            held_state = collision_holds[ped_key].get("state")
            current_state = record.get("state")
            held_position = held_state.get("position") if isinstance(held_state, dict) else None
            current_position = current_state.get("position") if isinstance(current_state, dict) else None
            delta = vector_distance(held_position, current_position)
            if delta is not None and delta > 0.05:
                findings.append(
                    Finding(
                        record["_timestamp_ms"],
                        "owner_collision",
                        "Authoritative ped moved while collision hold was active",
                        {**record_evidence(record), "hold": record_evidence(collision_holds[ped_key]), "position_delta": delta},
                    )
                )
            task = record.get("task")
            ancestry = task.get("ancestry") if isinstance(task, dict) else None
            airborne = isinstance(ancestry, list) and any(
                isinstance(entry, dict)
                and ("IN_AIR" in str(entry.get("name") or "") or "STUCK_IN_AIR" in str(entry.get("name") or ""))
                for entry in ancestry
            )
            if airborne:
                findings.append(
                    Finding(
                        record["_timestamp_ms"],
                        "owner_collision",
                        "Authoritative ped serialized an airborne task while collision hold was active",
                        {**record_evidence(record), "hold": record_evidence(collision_holds[ped_key])},
                    )
                )
        if event == "packet_serialize" and not ped.get("is_syncer"):
            findings.append(Finding(record["_timestamp_ms"], "owner", "A non-syncer serialized native AI", record_evidence(record)))
        if event in {"packet_receive", "observer_apply"} and ped.get("is_syncer"):
            findings.append(Finding(record["_timestamp_ms"], "owner", "The syncer consumed an observer record", record_evidence(record)))
        epoch = ped.get("owner_epoch")
        if isinstance(epoch, int):
            key = (record.get("client_identity"),) + identity(record)[:3]
            previous = epochs.get(key)
            if previous is not None and epoch < previous:
                findings.append(
                    Finding(
                        record["_timestamp_ms"],
                        "handoff",
                        "Owner epoch regressed",
                        {**record_evidence(record), "previous_epoch": previous, "epoch": epoch},
                    )
                )
            epochs[key] = max(previous or epoch, epoch)
    return findings


def analyze_receive_apply(records: list[dict[str, Any]]) -> list[Finding]:
    findings: list[Finding] = []
    grouped: dict[tuple[Any, Any, Any], list[dict[str, Any]]] = defaultdict(list)
    for record in records:
        if record.get("event") in {"packet_receive", "observer_apply"}:
            grouped[(record.get("client_identity"), record.get("process_id"), packet(record).get("local_sequence"))].append(record)
    for key, group in grouped.items():
        receives = [record for record in group if record.get("event") == "packet_receive"]
        applies = [record for record in group if record.get("event") == "observer_apply"]
        if len(receives) != 1 or len(applies) != 1:
            record = min(group, key=lambda item: item["_timestamp_ms"])
            findings.append(
                Finding(
                    record["_timestamp_ms"],
                    "observer_apply",
                    "Receive/apply cardinality mismatch",
                    {**record_evidence(record), "key": key, "receives": len(receives), "applies": len(applies)},
                )
            )
            continue
        if packet(receives[0]).get("sample_key") != packet(applies[0]).get("sample_key"):
            findings.append(
                Finding(
                    applies[0]["_timestamp_ms"],
                    "observer_apply",
                    "Observer applied a different local sample",
                    {"receive": record_evidence(receives[0]), "apply": record_evidence(applies[0])},
                )
            )
    return findings


def nearest_candidate(source: dict[str, Any], candidates: list[dict[str, Any]]) -> dict[str, Any] | None:
    source_time = source["_timestamp_ms"]
    eligible = [candidate for candidate in candidates if -100.0 <= candidate["_timestamp_ms"] - source_time <= PAIR_WINDOW_MS]
    return min(eligible, key=lambda candidate: abs(candidate["_timestamp_ms"] - source_time), default=None)


def analyze_transport(records: list[dict[str, Any]]) -> list[Finding]:
    findings: list[Finding] = []
    ignored_actions = {None, "", "prepare", "scenario-pass", "scenario-fail"}
    sends = [
        record
        for record in records
        if record.get("event") == "packet_serialize" and trace_context(record).get("action_id") not in ignored_actions
    ]
    applies_by_actor_lane: dict[tuple[str, str], list[dict[str, Any]]] = defaultdict(list)
    exact_applies: dict[tuple[str, str], list[dict[str, Any]]] = defaultdict(list)
    for record in records:
        if record.get("event") != "observer_apply":
            continue
        lane = str(packet(record).get("lane") or "none")
        applies_by_actor_lane[(actor_label(record), lane)].append(record)
        key = packet(record).get("sample_key")
        if key:
            exact_applies[(actor_label(record), str(key))].append(record)

    for source in sends:
        lane = str(packet(source).get("lane") or "none")
        actor = actor_label(source)
        target: dict[str, Any] | None = None
        key = packet(source).get("sample_key")
        if key and ("animation" in lane or lane == "animation_fast"):
            target = nearest_candidate(source, exact_applies.get((actor, str(key)), []))
            if target is None:
                findings.append(
                    Finding(source["_timestamp_ms"], "network", "Fast animation was not applied by an observer", record_evidence(source))
                )
                continue
        else:
            target = nearest_candidate(source, applies_by_actor_lane.get((actor, lane), []))
            if target is None:
                continue  # Streaming scope is not yet represented in the wire trace.

        source_sample = sample(source)
        target_sample = sample(target)
        distance = vector_distance(source_sample.get("position"), target_sample.get("position"))
        if distance is not None and distance > SPATIAL_MAX_DISTANCE:
            findings.append(
                Finding(
                    target["_timestamp_ms"],
                    "presentation",
                    f"Observer position differs by {distance:.3f} m",
                    {"owner": record_evidence(source), "observer": record_evidence(target), "distance": distance},
                )
            )
        delta = heading_delta(source_sample.get("heading"), target_sample.get("heading"))
        if delta is not None and delta > SPATIAL_MAX_HEADING_DEGREES:
            findings.append(
                Finding(
                    target["_timestamp_ms"],
                    "presentation",
                    f"Observer heading differs by {delta:.1f} degrees",
                    {"owner": record_evidence(source), "observer": record_evidence(target), "heading_delta": delta},
                )
            )
        owner_anim = source_sample.get("animation")
        observer_anim = target_sample.get("animation")
        if isinstance(owner_anim, dict) and isinstance(observer_anim, dict):
            owner_semantic = (owner_anim.get("mode"), owner_anim.get("group"), owner_anim.get("id"))
            observer_semantic = (observer_anim.get("mode"), observer_anim.get("group"), observer_anim.get("id"))
            if owner_semantic != observer_semantic:
                findings.append(
                    Finding(
                        target["_timestamp_ms"],
                        "presentation",
                        "Observer animation semantic mismatch",
                        {"owner": record_evidence(source), "observer": record_evidence(target), "owner_animation": owner_semantic,
                         "observer_animation": observer_semantic},
                    )
                )
    return findings


def analyze_assertions(records: list[dict[str, Any]]) -> list[Finding]:
    findings: list[Finding] = []
    for record in records:
        if record.get("schema") != HARNESS_SCHEMA:
            continue
        assertion = record.get("assertion")
        if isinstance(assertion, dict) and assertion.get("passed") is False:
            name = str(assertion.get("name", "unnamed"))
            if name == "scenario-timeout" or name.startswith("stage-present:"):
                continue  # The causal-chain check below reports the first missing stage.
            findings.append(
                Finding(
                    record["_timestamp_ms"],
                    "assertion",
                    f"Harness assertion failed: {name}",
                    {"source": record.get("_source"), "line": record.get("_line"), "wall_utc": record.get("wall_utc"),
                     "actor": record.get("actor_id"), "action_id": record.get("action_id"), "assertion": assertion},
                )
            )
    return findings


def analyze_harness_pipeline(records: list[dict[str, Any]]) -> list[Finding]:
    harness = [record for record in records if record.get("schema") == HARNESS_SCHEMA]
    if not harness:
        return []
    scenario = next((record.get("scenario_id") for record in harness if record.get("scenario_id")), None)
    if scenario != "remote-melee-group-v1":
        return []
    events = {str(record.get("event")) for record in harness}
    for index, stage in enumerate(MELEE_CAUSAL_STAGES):
        if stage not in events:
            later = next((candidate for candidate in MELEE_CAUSAL_STAGES[index + 1:] if candidate in events), None)
            terminal = next((record for record in reversed(harness) if record.get("event") == "run_end"), harness[-1])
            return [
                Finding(
                    terminal["_timestamp_ms"],
                    "causal_chain",
                    f"First missing native-damage stage: {stage}",
                    {
                        "scenario_id": scenario,
                        "missing_stage": stage,
                        "next_observed_stage": later,
                        "observed_stages": [candidate for candidate in MELEE_CAUSAL_STAGES if candidate in events],
                    },
                )
            ]
    return []


def analyze_rotation_pipeline(records: list[dict[str, Any]]) -> list[Finding]:
    harness = [record for record in records if record.get("schema") == HARNESS_SCHEMA]
    scenario = next((record.get("scenario_id") for record in harness if record.get("scenario_id")), None)
    if scenario not in {
        "native-group-rotation-v1",
        "native-group-rotation-handoff-v1",
        "isolated-ped-rotation-v2",
        "isolated-ped-rotation-handoff-v2",
    }:
        return []

    findings: list[Finding] = []
    actions = sorted(
        (record for record in harness if record.get("event") == "rotation_action_dispatched"),
        key=lambda record: (record.get("rotation_index", math.inf), record["_timestamp_ms"]),
    )
    causal_samples: list[dict[str, Any]] = []
    for action in actions:
        action_id = str(action.get("action_id") or "")
        target_degrees = action.get("target_heading")
        if not action_id or not isinstance(target_degrees, (int, float)):
            findings.append(
                Finding(action["_timestamp_ms"], "rotation_dispatch", "Rotation action has no stable ID or target", record_evidence(action))
            )
            continue
        target_radians = math.radians(float(target_degrees))

        if str(scenario).startswith("isolated-ped-rotation"):
            owner_hold = next(
                (
                    record
                    for record in harness
                    if record.get("event") == "rotation_owner_held" and str(record.get("action_id") or "") == action_id
                ),
                None,
            )
            if owner_hold is None:
                findings.append(
                    Finding(
                        action["_timestamp_ms"],
                        "rotation_owner_hold",
                        f"Owner did not prove a stable native heading for {action_id}",
                        {"action": record_evidence(action)},
                    )
                )
                continue

        action_records = [
            record
            for record in records
            if trace_context(record).get("action_id") == action_id and actor_label(record) == "ped-2"
        ]
        owner_candidates = [
            record
            for record in action_records
            if record.get("event") == "packet_serialize"
            and (record.get("ped") if isinstance(record.get("ped"), dict) else {}).get("is_syncer") is True
            and heading_delta(sample(record).get("heading"), target_radians) is not None
        ]
        owner = next(
            (
                record
                for record in owner_candidates
                if heading_delta(sample(record).get("heading"), target_radians) <= ROTATION_CONVERGENCE_DEGREES
            ),
            None,
        )
        if owner is None:
            findings.append(
                Finding(
                    action["_timestamp_ms"],
                    "rotation_owner_serialize",
                    f"Owner never serialized {target_degrees:.1f} degree heading for {action_id}",
                    {"action": record_evidence(action), "owner_candidates": [record_evidence(record) for record in owner_candidates[:3]]},
                )
            )
            continue

        owner_identity = owner.get("client_identity")
        receive_candidates = [
            record
            for record in action_records
            if record.get("event") == "packet_receive"
            and record.get("client_identity") != owner_identity
            and heading_delta(sample(record).get("heading"), target_radians) is not None
        ]
        received = next(
            (
                record
                for record in receive_candidates
                if heading_delta(sample(record).get("heading"), target_radians) <= ROTATION_CONVERGENCE_DEGREES
            ),
            None,
        )
        if received is None:
            findings.append(
                Finding(
                    owner["_timestamp_ms"],
                    "rotation_observer_receive",
                    f"Observer never received {target_degrees:.1f} degree heading for {action_id}",
                    {"owner": record_evidence(owner), "receive_candidates": [record_evidence(record) for record in receive_candidates[:3]]},
                )
            )
            continue

        receive_key = packet(received).get("sample_key")
        receive_sequence = packet(received).get("local_sequence")
        receive_monotonic = received.get("monotonic_ms")
        exact_post_process = [
            record
            for record in action_records
            if record.get("event") == "rotation_post_process"
            and record.get("client_identity") == received.get("client_identity")
            and isinstance(record.get("rotation"), dict)
            and record["rotation"].get("has_network_sample") is True
            and (not receive_key or record["rotation"].get("last_receive_sample_key") == receive_key)
            and (receive_sequence is None or record["rotation"].get("last_receive_sequence") == receive_sequence)
            and (
                not isinstance(receive_monotonic, (int, float))
                or not isinstance(record.get("monotonic_ms"), (int, float))
                or record["monotonic_ms"] >= receive_monotonic
            )
        ]
        exact_post_process.sort(key=lambda record: record.get("monotonic_ms", math.inf))
        if not exact_post_process:
            findings.append(
                Finding(
                    received["_timestamp_ms"],
                    "rotation_post_render",
                    f"Observer has no post-render sample correlated with {action_id}",
                    {"owner": record_evidence(owner), "observer_receive": record_evidence(received)},
                )
            )
            continue

        # Subsequent identical target packets may replace the local receive
        # sequence before the grace expires. They still belong to the same
        # deterministic action, so include them for convergence timing while
        # retaining the exact first-post sample above for attribution.
        post_process = [
            record
            for record in action_records
            if record.get("event") == "rotation_post_process"
            and record.get("client_identity") == received.get("client_identity")
            and isinstance(record.get("rotation"), dict)
            and record["rotation"].get("has_network_sample") is True
            and heading_delta(record["rotation"].get("network_sample_heading"), target_radians) is not None
            and heading_delta(record["rotation"].get("network_sample_heading"), target_radians) <= ROTATION_CONVERGENCE_DEGREES
            and (
                not isinstance(receive_monotonic, (int, float))
                or not isinstance(record.get("monotonic_ms"), (int, float))
                or record["monotonic_ms"] >= receive_monotonic
            )
        ]
        post_process.sort(key=lambda record: record.get("monotonic_ms", math.inf))
        first_post = exact_post_process[0]
        first_delta = heading_delta(first_post["rotation"].get("matrix_heading"), target_radians)
        converged = next(
            (
                record
                for record in post_process
                if heading_delta(record["rotation"].get("matrix_heading"), target_radians) is not None
                and heading_delta(record["rotation"].get("matrix_heading"), target_radians) <= ROTATION_CONVERGENCE_DEGREES
            ),
            None,
        )
        spatial_sync_rate = first_post["rotation"].get("spatial_sync_rate_ms")
        grace_ms = float(spatial_sync_rate) + ROTATION_RENDER_GRACE_SLACK_MS if isinstance(spatial_sync_rate, (int, float)) else None
        convergence_ms = None
        if converged is not None and isinstance(receive_monotonic, (int, float)) and isinstance(converged.get("monotonic_ms"), (int, float)):
            convergence_ms = float(converged["monotonic_ms"]) - float(receive_monotonic)
        beyond_grace = None
        if grace_ms is not None and isinstance(receive_monotonic, (int, float)):
            beyond_grace = next(
                (
                    record
                    for record in post_process
                    if isinstance(record.get("monotonic_ms"), (int, float))
                    and float(record["monotonic_ms"]) - float(receive_monotonic) >= grace_ms
                ),
                None,
            )
        wall_transit_ms = None
        if math.isfinite(owner["_timestamp_ms"]) and math.isfinite(received["_timestamp_ms"]):
            wall_transit_ms = received["_timestamp_ms"] - owner["_timestamp_ms"]
        one_behind_post = None
        if isinstance(receive_monotonic, (int, float)):
            one_behind_post = next(
                (
                    record
                    for record in post_process
                    if isinstance(record.get("monotonic_ms"), (int, float))
                    and float(record["monotonic_ms"]) - float(receive_monotonic) >= ROTATION_ONE_BEHIND_EVALUATION_MS
                ),
                None,
            )

        causal_samples.append(
            {
                "action": action,
                "action_id": action_id,
                "target_degrees": float(target_degrees),
                "target_radians": target_radians,
                "owner": owner,
                "received": received,
                "first_post": first_post,
                "first_delta_degrees": first_delta,
                "one_behind_post": one_behind_post,
                "converged": converged,
                "convergence_ms": convergence_ms,
                "grace_ms": grace_ms,
                "wall_transit_ms": wall_transit_ms,
            }
        )

        beyond_delta = (
            heading_delta(beyond_grace["rotation"].get("matrix_heading"), target_radians) if beyond_grace is not None else None
        )
        if beyond_grace is not None and beyond_delta is not None and beyond_delta > ROTATION_CONVERGENCE_DEGREES and (
            convergence_ms is None or grace_ms is None or convergence_ms > grace_ms
        ):
            findings.append(
                Finding(
                    beyond_grace["_timestamp_ms"],
                    "rotation_post_render",
                    f"Observer matrix remained {beyond_delta:.1f} degrees from {target_degrees:.1f} after the post-receive grace",
                    {
                        "owner": record_evidence(owner),
                        "observer_receive": record_evidence(received),
                        "first_post_receive": record_evidence(first_post),
                        "beyond_grace": record_evidence(beyond_grace),
                        "wall_transit_ms_estimate": wall_transit_ms,
                        "render_grace_ms": grace_ms,
                        "first_post_receive_delta_degrees": first_delta,
                        "time_to_converge_ms": convergence_ms,
                        "render_delta_degrees": beyond_delta,
                    },
                )
            )

    one_behind: list[dict[str, Any]] = []
    for previous, current in zip(causal_samples, causal_samples[1:]):
        evaluation_post = current["one_behind_post"]
        if evaluation_post is None:
            continue
        matrix_heading = evaluation_post["rotation"].get("matrix_heading")
        previous_delta = heading_delta(matrix_heading, previous["target_radians"])
        current_delta = heading_delta(matrix_heading, current["target_radians"])
        if (
            previous_delta is not None
            and current_delta is not None
            and previous_delta <= ROTATION_PREVIOUS_TARGET_DEGREES
            and current_delta >= ROTATION_ONE_BEHIND_MIN_DEGREES
        ):
            one_behind.append(
                {
                    "previous_action": previous["action_id"],
                    "action": current["action_id"],
                    "previous_target_degrees": previous["target_degrees"],
                    "target_degrees": current["target_degrees"],
                    "previous_delta_degrees": previous_delta,
                    "current_delta_degrees": current_delta,
                    "wall_transit_ms_estimate": current["wall_transit_ms"],
                    "time_to_converge_ms": current["convergence_ms"],
                    "render_grace_ms": current["grace_ms"],
                    "evaluation_post_receive_render_ms": (
                        float(evaluation_post.get("monotonic_ms")) - float(current["received"].get("monotonic_ms"))
                        if isinstance(evaluation_post.get("monotonic_ms"), (int, float))
                        and isinstance(current["received"].get("monotonic_ms"), (int, float))
                        else None
                    ),
                    "evaluation_timestamp_ms": evaluation_post["_timestamp_ms"],
                    "observer_receive": record_evidence(current["received"]),
                    "first_post_receive": record_evidence(current["first_post"]),
                    "evaluation_post_receive": record_evidence(evaluation_post),
                }
            )
    if len(one_behind) >= 2:
        finding_record = one_behind[-1]
        findings.append(
            Finding(
                finding_record["evaluation_timestamp_ms"],
                "rotation_one_snapshot_behind",
                "Observer still rendered the previous target at the cadence checkpoint after two consecutive received turns",
                {
                    "matches": one_behind,
                    "previous_target_tolerance_degrees": ROTATION_PREVIOUS_TARGET_DEGREES,
                    "current_target_min_delta_degrees": ROTATION_ONE_BEHIND_MIN_DEGREES,
                    "evaluation_after_receive_ms": ROTATION_ONE_BEHIND_EVALUATION_MS,
                    "transport_boundary": "evaluated only after packet_receive; wall transit is reported separately",
                },
            )
        )

    if actions and not any(record.get("event") == "rotation_schedule_complete" for record in harness):
        terminal = next((record for record in reversed(harness) if record.get("event") == "run_end"), harness[-1])
        findings.append(
            Finding(
                terminal["_timestamp_ms"],
                "rotation_harness",
                "Predetermined rotation schedule did not complete",
                record_evidence(terminal),
            )
        )
    return findings


def analyze_population(records: list[dict[str, Any]]) -> tuple[list[Finding], dict[str, Any]]:
    population = [record for record in records if record.get("schema") == POPULATION_SCHEMA]
    findings: list[Finding] = []
    event_counts: Counter[str] = Counter()
    miss_reasons: Counter[str] = Counter()
    reject_reasons: Counter[str] = Counter()
    veto_reasons: Counter[str] = Counter()
    despawn_reasons: Counter[str] = Counter()
    spawn_classes: Counter[str] = Counter()
    spawn_gangs: Counter[str] = Counter()
    spawn_models: Counter[str] = Counter()
    profiles_by_player: dict[str, list[dict[str, Any]]] = defaultdict(list)
    convergence_by_player: dict[str, list[dict[str, Any]]] = defaultdict(list)
    requests: dict[str, dict[str, Any]] = {}
    checks: dict[str, dict[str, Any]] = {}
    live_traffic: dict[str, dict[str, Any]] = {}
    live_groups: dict[str, dict[str, Any]] = {}
    pending_group_handoffs: dict[str, dict[str, Any]] = {}
    completed_group_handoffs = 0
    urgent_group_handoffs = 0
    explicit_check_timeouts = 0
    spawned_peds = 0
    spawned_groups = 0
    spawned_group_members = 0
    despawned_peds = 0
    despawned_groups = 0

    def warning(record: dict[str, Any], stage: str, message: str, extra: dict[str, Any] | None = None) -> None:
        evidence = population_evidence(record)
        if extra:
            evidence.update(extra)
        findings.append(Finding(record.get("_timestamp_ms", math.inf), stage, message, evidence, "warning"))

    def error(record: dict[str, Any], stage: str, message: str, extra: dict[str, Any] | None = None) -> None:
        evidence = population_evidence(record)
        if extra:
            evidence.update(extra)
        findings.append(Finding(record.get("_timestamp_ms", math.inf), stage, message, evidence))

    population_by_source: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for record in population:
        population_by_source[str(record.get("_source") or "unknown")].append(record)
    for source, source_records in population_by_source.items():
        previous_sequence: int | None = None
        previous_revision: int | None = None
        for record in sorted(source_records, key=lambda item: item.get("_line", 0)):
            sequence = record.get("event_sequence")
            if not isinstance(sequence, int) or isinstance(sequence, bool):
                warning(record, "population_gap", "Population record has no integer event_sequence")
            elif previous_sequence is not None and sequence != previous_sequence + 1:
                error(
                    record,
                    "population_gap",
                    "Population event_sequence has a gap",
                    {"source": source, "previous": previous_sequence, "current": sequence},
                )
            if isinstance(sequence, int) and not isinstance(sequence, bool):
                previous_sequence = sequence
            revision = record.get("world_revision")
            if isinstance(revision, int) and not isinstance(revision, bool):
                if previous_revision is not None and revision < previous_revision:
                    error(
                        record,
                        "population_world",
                        "Population world revision regressed",
                        {"previous_revision": previous_revision, "world_revision": revision},
                    )
                previous_revision = revision

    for record in sorted(population, key=lambda item: (population_clock(item), item.get("_line", 0))):
        event = record.get("event")
        if not isinstance(event, str) or not event:
            error(record, "population_schema", "Population record has no event name")
            continue
        event_counts[event] += 1
        if record.get("schema_version") != POPULATION_SCHEMA_VERSION:
            error(
                record,
                "population_schema",
                "Unsupported population schema version",
                {"expected": POPULATION_SCHEMA_VERSION, "actual": record.get("schema_version")},
            )

        if event == "group_handoff_started":
            group_id = record.get("group_id")
            if group_id is None:
                error(record, "population_handoff", "Group handoff start has no group_id")
            else:
                group_key = str(group_id)
                if group_key in pending_group_handoffs:
                    error(
                        record,
                        "population_handoff",
                        "Group started a second handoff before the first completed",
                        {"first": population_evidence(pending_group_handoffs[group_key])},
                    )
                pending_group_handoffs[group_key] = record
                if record.get("reason") == "group-owner-left-residency":
                    urgent_group_handoffs += 1
                old_distance = finite_number(record.get("old_owner_distance"))
                new_distance = finite_number(record.get("new_owner_distance"))
                if old_distance is not None and new_distance is not None and new_distance >= old_distance:
                    error(
                        record,
                        "population_handoff",
                        "Group handoff did not select a closer owner",
                        {"old_owner_distance": old_distance, "new_owner_distance": new_distance},
                    )
        elif event == "group_handoff_assigned":
            group_id = record.get("group_id")
            group_key = str(group_id) if group_id is not None else None
            started = pending_group_handoffs.pop(group_key, None) if group_key is not None else None
            if started is None:
                warning(record, "population_handoff", "Group handoff assignment has no matching start in this trace")
            else:
                started_epoch = started.get("epoch")
                assigned_epoch = record.get("epoch")
                if isinstance(started_epoch, int) and isinstance(assigned_epoch, int) and assigned_epoch != started_epoch + 1:
                    error(
                        record,
                        "population_handoff",
                        "Group handoff epoch did not advance exactly once",
                        {"started_epoch": started_epoch, "assigned_epoch": assigned_epoch},
                    )
                completed_group_handoffs += 1

        if event == "population_profile":
            player = record.get("player_id")
            if player is None:
                error(record, "population_profile", "Population profile has no player_id")
                continue
            player_key = str(player)
            profiles_by_player[player_key].append(record)
            for field in ("target", "supported_target", "civilian_target", "gang_target", "cop_target", "dealer_target"):
                value = finite_number(record.get(field))
                if value is None:
                    warning(record, "population_profile", f"Population profile has no numeric {field}")
                elif value < 0:
                    error(record, "population_profile", f"Population profile has a negative {field}", {field: value})
            target = finite_number(record.get("target"))
            supported = finite_number(record.get("supported_target"))
            civilian = finite_number(record.get("civilian_target"))
            gang = finite_number(record.get("gang_target"))
            cop = finite_number(record.get("cop_target"))
            dealer = finite_number(record.get("dealer_target"))
            if None not in (supported, civilian, gang) and not math.isclose(supported, civilian + gang, abs_tol=0.02):
                error(
                    record,
                    "population_profile",
                    "supported_target does not equal civilian_target + gang_target",
                    {"supported_target": supported, "civilian_plus_gang": civilian + gang},
                )
            if None not in (target, supported, cop, dealer) and not math.isclose(target, supported + cop + dealer, abs_tol=0.02):
                error(
                    record,
                    "population_profile",
                    "target does not equal supported_target + cop_target + dealer_target",
                    {"target": target, "component_total": supported + cop + dealer},
                )

        if event in {"population_snapshot", "population_convergence", "candidate_request"}:
            player = record.get("player_id")
            target = nested_number(record, "targets", "total")
            live = nested_number(record, "live", "total")
            if player is not None and target is not None and live is not None:
                convergence_by_player[str(player)].append(
                    {"event": event, "clock_ms": population_clock(record), "target": target, "live": live, "gap": target - live}
                )
            for population_class in ("total", "civilian", "gang"):
                class_target = nested_number(record, "targets", population_class)
                class_live = nested_number(record, "live", population_class)
                class_deficit = nested_number(record, "deficits", population_class)
                if class_target is not None and class_target < 0:
                    error(
                        record,
                        "population_snapshot",
                        "Population target is negative",
                        {"population_class": population_class, "target": class_target},
                    )
                if class_live is not None and class_live < 0:
                    error(
                        record,
                        "population_snapshot",
                        "Population live count is negative",
                        {"population_class": population_class, "live": class_live},
                    )
                if None not in (class_target, class_live, class_deficit) and not math.isclose(
                    class_deficit, class_target - class_live, abs_tol=0.02
                ):
                    error(
                        record,
                        "population_snapshot",
                        "Population deficit does not match target - live",
                        {
                            "population_class": population_class,
                            "target": class_target,
                            "live": class_live,
                            "deficit": class_deficit,
                        },
                    )

        if event == "candidate_request":
            request_id = record.get("request_id")
            if request_id is None:
                error(record, "population_request", "Candidate request has no request_id")
                continue
            request_key = str(request_id)
            if request_key in requests:
                error(
                    record,
                    "population_request",
                    "Duplicate candidate request_id",
                    {"first": population_evidence(requests[request_key]["record"])},
                )
                continue
            requests[request_key] = {"record": record, "terminal": None}
            target = nested_number(record, "targets", "total")
            live = nested_number(record, "live", "total")
            if target is None or live is None:
                warning(record, "population_request", "Candidate request lacks numeric total target/live state")
            elif live >= target:
                error(
                    record,
                    "population_request",
                    "Candidate requested while observed population was already at or above target",
                    {"target": target, "live": live},
                )
            population_class = record.get("population_class")
            if population_class == "civilian":
                deficit = nested_number(record, "deficits", "civilian")
                if deficit is not None and deficit <= 0:
                    error(
                        record,
                        "population_request",
                        "Civilian candidate requested without a positive civilian deficit",
                        {"deficit": deficit},
                    )
            elif population_class == "gang":
                deficit = nested_number(record, "deficits", "gang")
                if deficit is not None and deficit <= 0:
                    error(
                        record,
                        "population_request",
                        "Gang candidate requested without a positive gang deficit",
                        {"deficit": deficit},
                    )

        if event in {"candidate_miss", "candidate_rejected", "spawn", "group_spawn"}:
            request_id = record.get("request_id")
            request_key = str(request_id) if request_id is not None else None
            if request_key is None:
                error(record, "population_request", f"{event} has no request_id")
            elif request_key not in requests:
                warning(
                    record,
                    "population_gap",
                    f"{event} references a request absent from this trace",
                    {"request_id": request_id},
                )
            else:
                previous = requests[request_key]["terminal"]
                if previous is not None:
                    error(
                        record,
                        "population_request",
                        "Candidate request has multiple terminal outcomes",
                        {"previous_terminal": previous, "terminal": event},
                    )
                else:
                    requests[request_key]["terminal"] = event
                    requests[request_key]["terminal_record"] = record

        if event == "candidate_miss":
            miss_reasons[str(record.get("reason") or "unspecified")] += 1
        elif event == "candidate_rejected":
            reason = str(record.get("reason") or "unspecified")
            reject_reasons[reason] += 1
            if "visible" in reason or "veto" in reason:
                veto_reasons[reason] += 1
            if "timeout" in reason:
                explicit_check_timeouts += 1

        if event == "visibility_check_started":
            check_id = record.get("visibility_check_id")
            if check_id is None:
                error(record, "population_visibility", "Visibility check has no visibility_check_id")
                continue
            check_key = str(check_id)
            if check_key in checks:
                error(
                    record,
                    "population_visibility",
                    "Duplicate visibility_check_id",
                    {"first": population_evidence(checks[check_key]["record"])},
                )
                continue
            voters = record.get("voters")
            checks[check_key] = {
                "record": record,
                "kind": str(record.get("kind") or "unknown"),
                "expected_voters": (
                    int(voters) if isinstance(voters, int) and not isinstance(voters, bool) and voters >= 0 else None
                ),
                "votes": {},
                "terminal": None,
            }
        elif event == "visibility_vote":
            check_id = record.get("visibility_check_id")
            check_key = str(check_id) if check_id is not None else None
            if check_key is None or check_key not in checks:
                warning(record, "population_gap", "Visibility vote references a check absent from this trace")
            else:
                if record.get("player_id") is None:
                    error(record, "population_visibility", "Visibility vote has no player_id")
                    continue
                player_key = str(record.get("player_id"))
                if player_key in checks[check_key]["votes"]:
                    error(
                        record,
                        "population_visibility",
                        "Player voted more than once in a visibility check",
                        {"voter": player_key},
                    )
                checks[check_key]["votes"][player_key] = record.get("visible") is True
        elif event in {"removal_visibility_result", "visibility_check_result", "visibility_check_timeout"}:
            check_id = record.get("visibility_check_id")
            check_key = str(check_id) if check_id is not None else None
            if event == "visibility_check_timeout" or "timeout" in str(record.get("reason") or ""):
                explicit_check_timeouts += 1
            if check_key is None or check_key not in checks:
                warning(record, "population_gap", f"{event} references a check absent from this trace")
            elif checks[check_key]["terminal"] is not None:
                error(
                    record,
                    "population_visibility",
                    "Visibility check has multiple terminal results",
                    {"previous_terminal": checks[check_key]["terminal"]},
                )
            else:
                checks[check_key]["terminal"] = event
                checks[check_key]["terminal_record"] = record

        if event in {"spawn", "group_spawn"}:
            request_id = record.get("request_id")
            check = next(
                (
                    value
                    for value in checks.values()
                    if str(value["record"].get("request_id")) == str(request_id) and value["kind"] == "candidate"
                ),
                None,
            )
            if check is not None:
                visible_voters = [player for player, visible in check["votes"].items() if visible]
                if visible_voters:
                    error(
                        record,
                        "population_visibility",
                        "Candidate spawned after a resident visibility veto",
                        {"visible_voters": visible_voters},
                    )
                expected = check["expected_voters"]
                if expected is not None and len(check["votes"]) < expected:
                    error(
                        record,
                        "population_visibility",
                        "Candidate spawned before every expected visibility vote arrived",
                        {"expected_voters": expected, "votes": len(check["votes"])},
                    )
                if check["terminal"] is None:
                    check["terminal"] = event
                    check["terminal_record"] = record
        elif event == "candidate_rejected":
            request_id = record.get("request_id")
            check = next(
                (
                    value
                    for value in checks.values()
                    if str(value["record"].get("request_id")) == str(request_id) and value["kind"] == "candidate"
                ),
                None,
            )
            if check is not None and check["terminal"] is None:
                check["terminal"] = event
                check["terminal_record"] = record

        if event == "spawn":
            spawned_peds += 1
            traffic_id = record.get("traffic_id")
            if traffic_id is None:
                error(record, "population_lifecycle", "Spawn has no traffic_id")
            elif str(traffic_id) in live_traffic:
                error(record, "population_lifecycle", "traffic_id spawned twice without a despawn")
            else:
                live_traffic[str(traffic_id)] = record
            spawn_classes[str(record.get("population_class") or "unknown")] += 1
            spawn_gangs[str(record.get("gang") if record.get("gang") is not None else "none")] += 1
            spawn_models[str(record.get("model") if record.get("model") is not None else "unknown")] += 1
        elif event == "group_spawn":
            spawned_groups += 1
            members = finite_number(record.get("member_count"))
            if members is not None:
                spawned_group_members += int(members)
            group_id = record.get("group_id")
            if group_id is None:
                error(record, "population_lifecycle", "Group spawn has no group_id")
            elif str(group_id) in live_groups:
                error(record, "population_lifecycle", "group_id spawned twice without a despawn")
            else:
                live_groups[str(group_id)] = record
            spawn_classes["gang"] += int(members) if members is not None else 0
            spawn_gangs[str(record.get("gang") if record.get("gang") is not None else "none")] += int(members) if members is not None else 0
            models = record.get("models")
            if isinstance(models, list):
                spawn_models.update(str(model) for model in models)

        if event == "despawn":
            despawned_peds += 1
            despawn_reasons[str(record.get("reason") or "unspecified")] += 1
            traffic_id = record.get("traffic_id")
            if traffic_id is None:
                error(record, "population_lifecycle", "Despawn has no traffic_id")
            elif live_traffic.pop(str(traffic_id), None) is None:
                warning(record, "population_gap", "Despawn references a ped absent from this trace")
        elif event == "group_despawn":
            despawned_groups += 1
            despawn_reasons[str(record.get("reason") or "unspecified")] += 1
            group_id = record.get("group_id")
            if group_id is None:
                error(record, "population_lifecycle", "Group despawn has no group_id")
            elif live_groups.pop(str(group_id), None) is None:
                warning(record, "population_gap", "Group despawn references a group absent from this trace")

    clocks = [population_clock(record) for record in population if math.isfinite(population_clock(record))]
    trace_end = max(clocks, default=math.inf)
    pending_requests: list[dict[str, Any]] = []
    for request_key, request in requests.items():
        if request["terminal"] is None:
            age = trace_end - population_clock(request["record"]) if math.isfinite(trace_end) else None
            pending_requests.append({"request_id": request_key, "age_ms": age})
            warning(request["record"], "population_gap", "Candidate request has no terminal outcome in this trace", {"age_ms": age})

    pending_checks: list[dict[str, Any]] = []
    stale_checks = 0
    for check_key, check in checks.items():
        if check["terminal"] is None:
            age = trace_end - population_clock(check["record"]) if math.isfinite(trace_end) else None
            stale = age is not None and age >= POPULATION_STALE_CHECK_MS
            stale_checks += int(stale)
            pending_checks.append({"visibility_check_id": check_key, "kind": check["kind"], "age_ms": age, "stale": stale})
            message = (
                "Visibility check remained pending past the analyzer liveness bound"
                if stale
                else "Visibility check has no terminal result in this trace"
            )
            severity = "error" if stale else "warning"
            findings.append(
                Finding(
                    check["record"].get("_timestamp_ms", math.inf),
                    "population_visibility",
                    message,
                    {
                        **population_evidence(check["record"]),
                        "age_ms": age,
                        "analyzer_liveness_bound_ms": POPULATION_STALE_CHECK_MS,
                    },
                    severity,
                )
            )

    for handoff in pending_group_handoffs.values():
        age = trace_end - population_clock(handoff) if math.isfinite(trace_end) else None
        if age is not None and age >= POPULATION_STALE_CHECK_MS:
            error(
                handoff,
                "population_handoff",
                "Group handoff remained pending past the analyzer liveness bound",
                {"age_ms": age, "analyzer_liveness_bound_ms": POPULATION_STALE_CHECK_MS},
            )

    player_summaries: dict[str, Any] = {}
    for player in sorted(set(profiles_by_player) | set(convergence_by_player)):
        profiles = profiles_by_player.get(player, [])
        observations = convergence_by_player.get(player, [])
        latest_profile = profiles[-1] if profiles else {}
        latest_observation = observations[-1] if observations else None
        target_values = [value for value in (finite_number(profile.get("target")) for profile in profiles) if value is not None]
        gaps = [observation["gap"] for observation in observations]
        player_summaries[player] = {
            "profile_samples": len(profiles),
            "latest_profile": {
                key: latest_profile.get(key)
                for key in (
                    "zone_type", "time_index", "weekend", "no_cops", "target", "supported_target", "civilian_target",
                    "gang_target", "cop_target", "dealer_target", "ped_density_multiplier", "fewer_peds_multiplier",
                    "maximum_peds_in_use", "creation_distance_multiplier", "generation_distance_multiplier", "gang_weights",
                )
                if key in latest_profile
            },
            "profile_target_range": [min(target_values), max(target_values)] if target_values else None,
            "convergence": {
                "observation_count": len(observations),
                "observation_events": counter_dict(Counter(observation["event"] for observation in observations)),
                "latest": latest_observation,
                "gap_range": [min(gaps), max(gaps)] if gaps else None,
                "reached_target_observed": any(gap <= 0 for gap in gaps),
                "scope_note": "Derived only from target/live pairs emitted by the runtime; silence is not treated as convergence.",
            },
        }

    terminal_counts = Counter(
        request["terminal"] if request["terminal"] is not None else "pending" for request in requests.values()
    )
    summary = {
        "schema": POPULATION_SCHEMA,
        "records": len(population),
        "duration_ms": max(clocks) - min(clocks) if clocks else 0,
        "schema_versions": sorted({record.get("schema_version") for record in population}, key=str),
        "world_revisions": sorted(
            {record.get("world_revision") for record in population if record.get("world_revision") is not None}, key=str
        ),
        "presets": sorted({str(record.get("preset")) for record in population if record.get("preset") is not None}),
        "events": counter_dict(event_counts),
        "players": player_summaries,
        "requests": {"started": len(requests), "terminal": counter_dict(terminal_counts), "pending": pending_requests},
        "visibility_checks": {
            "started": len(checks),
            "by_kind": counter_dict(Counter(check["kind"] for check in checks.values())),
            "pending": pending_checks,
            "stale_pending": stale_checks,
            "explicit_timeouts": explicit_check_timeouts,
        },
        "group_handoffs": {
            "completed": completed_group_handoffs,
            "urgent_owner_left_residency": urgent_group_handoffs,
            "pending": [population_evidence(record) for record in pending_group_handoffs.values()],
        },
        "spawns": {
            "peds": spawned_peds,
            "groups": spawned_groups,
            "group_members": spawned_group_members,
            "classes": counter_dict(spawn_classes),
            "gangs": counter_dict(spawn_gangs),
            "models": counter_dict(spawn_models),
        },
        "despawns": {"peds": despawned_peds, "groups": despawned_groups, "reasons": counter_dict(despawn_reasons)},
        "reasons": {
            "miss": counter_dict(miss_reasons),
            "reject": counter_dict(reject_reasons),
            "veto": counter_dict(veto_reasons),
            "despawn": counter_dict(despawn_reasons),
        },
        "live_at_trace_end": {"peds_observed": len(live_traffic), "groups_observed": len(live_groups)},
    }
    population_serious = sorted(
        (finding for finding in findings if finding.severity == "error"),
        key=lambda finding: (finding.timestamp_ms, finding.stage, finding.message),
    )
    population_warnings = [finding for finding in findings if finding.severity == "warning"]
    summary["diagnostics"] = {
        "serious_count": len(population_serious),
        "warning_count": len(population_warnings),
        "first_serious_invariant_violation": (
            {
                "stage": population_serious[0].stage,
                "message": population_serious[0].message,
                "evidence": population_serious[0].evidence,
            }
            if population_serious
            else None
        ),
    }
    return findings, summary


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--primary", type=Path, help="primary client JSONL")
    parser.add_argument("--cl2", type=Path, help="secondary client JSONL")
    parser.add_argument("--server", type=Path, help="harness or population server JSONL")
    parser.add_argument(
        "--population",
        type=Path,
        action="append",
        default=[],
        help="population JSONL (repeatable; may also be supplied through --server)",
    )
    parser.add_argument("--run-id", default="latest", help="run to analyze (default: latest)")
    parser.add_argument("--json", action="store_true", help="emit a machine-readable report")
    args = parser.parse_args()

    records: list[dict[str, Any]] = []
    findings: list[Finding] = []
    inputs: list[tuple[Path, str]] = []
    for path, source in ((args.primary, "primary"), (args.cl2, "cl2"), (args.server, "server")):
        if path is not None:
            inputs.append((path, source))
    inputs.extend((path, f"population-{index}") for index, path in enumerate(args.population, 1))
    if not inputs:
        parser.error("at least one JSONL input is required")

    seen_paths: set[Path] = set()
    for path, source in inputs:
        resolved = path.expanduser().resolve()
        if resolved in seen_paths:
            continue
        seen_paths.add(resolved)
        loaded, errors = read_jsonl(path, source)
        records.extend(loaded)
        findings.extend(errors)

    all_records = records
    native_all_records = [record for record in all_records if record.get("schema") in {SCHEMA, HARNESS_SCHEMA}]
    selected = select_run(native_all_records, args.run_id)
    if selected:
        native_records = [record for record in native_all_records if str(run_id(record)) == selected]
    else:
        native_records = native_all_records
    if not selected and args.run_id != "latest":
        findings.append(Finding(-math.inf, "input", f"Run {args.run_id!r} was not found", {}))

    # Writer sequence numbers are process-global. Analyze the complete files so
    # records from unrelated actors do not look like drops after run filtering.
    findings.extend(analyze_sequences(native_all_records))
    findings.extend(analyze_ownership(native_records))
    findings.extend(analyze_receive_apply(native_records))
    findings.extend(analyze_transport(native_records))
    findings.extend(analyze_harness_pipeline(native_records))
    findings.extend(analyze_rotation_pipeline(native_records))
    findings.extend(analyze_assertions(native_records))
    population_findings, population_summary = analyze_population(all_records)
    findings.extend(population_findings)
    stage_priority = {"input": 0, "jsonl": 0, "writer": 1, "causal_chain": 2, "assertion": 3}
    findings.sort(
        key=lambda finding: (
            0 if finding.severity == "error" else 1,
            finding.timestamp_ms,
            stage_priority.get(finding.stage, 4),
            finding.stage,
            finding.message,
        )
    )

    event_counts = Counter(str(record.get("event", "unknown")) for record in native_records)
    actors = sorted({actor_label(record) for record in native_records if actor_label(record) != "unknown"})
    finite_times = [record["_timestamp_ms"] for record in native_records if math.isfinite(record["_timestamp_ms"])]
    serious = [finding for finding in findings if finding.severity == "error"]
    warnings = [finding for finding in findings if finding.severity == "warning"]
    summary = {
        "run_id": selected,
        "records": len(native_records),
        "actors": actors,
        "duration_ms": max(finite_times) - min(finite_times) if finite_times else 0,
        "events": dict(event_counts),
        "finding_count": len(findings),
        "serious_finding_count": len(serious),
        "warning_count": len(warnings),
        "first_divergence": None,
        "first_serious_invariant_violation": None,
        "population": population_summary,
    }
    if serious:
        first = serious[0]
        value = {"stage": first.stage, "message": first.message, "evidence": first.evidence}
        summary["first_divergence"] = value
        summary["first_serious_invariant_violation"] = value

    if args.json:
        print(json.dumps(summary, ensure_ascii=False, indent=2))
    else:
        print(
            f"run={selected or 'unscoped'} records={len(native_records)} actors={','.join(actors) or '-'} "
            f"duration_ms={summary['duration_ms']:.0f}"
        )
        if population_summary["records"]:
            requests_summary = population_summary["requests"]
            checks_summary = population_summary["visibility_checks"]
            spawns_summary = population_summary["spawns"]
            despawns_summary = population_summary["despawns"]
            print(
                f"population records={population_summary['records']} players={len(population_summary['players'])} "
                f"requests={requests_summary['started']} pending_requests={len(requests_summary['pending'])} "
                f"checks={checks_summary['started']} pending_checks={len(checks_summary['pending'])} "
                f"stale_checks={checks_summary['stale_pending']} explicit_timeouts={checks_summary['explicit_timeouts']}"
            )
            print(
                f"population_lifecycle peds_spawned={spawns_summary['peds']} groups_spawned={spawns_summary['groups']} "
                f"group_members={spawns_summary['group_members']} peds_despawned={despawns_summary['peds']} "
                f"groups_despawned={despawns_summary['groups']}"
            )
            print(f"population_reasons {json.dumps(population_summary['reasons'], ensure_ascii=False, sort_keys=True)}")
            for player, player_summary in population_summary["players"].items():
                convergence = player_summary["convergence"]
                latest = convergence["latest"] or {}
                latest_profile = player_summary["latest_profile"]
                print(
                    f"population_player id={player} profiles={player_summary['profile_samples']} "
                    f"profile_target={latest_profile.get('target', '-')} observations={convergence['observation_count']} "
                    f"live={latest.get('live', '-')} target={latest.get('target', '-')} gap={latest.get('gap', '-')} "
                    f"reached_observed={str(convergence['reached_target_observed']).lower()}"
                )
        if serious:
            first = serious[0]
            print(f"FIRST_DIVERGENCE stage={first.stage}: {first.message}")
            print(json.dumps(first.evidence, ensure_ascii=False, indent=2))
            print(f"FIRST_SERIOUS_INVARIANT_VIOLATION stage={first.stage}: {first.message}")
        else:
            print("FIRST_DIVERGENCE none detected by the current invariants")
            print("FIRST_SERIOUS_INVARIANT_VIOLATION none detected")
        if warnings:
            print(f"warnings={len(warnings)} first_warning={warnings[0].stage}: {warnings[0].message}")
        if len(serious) > 1:
            print(f"additional_serious_findings={len(serious) - 1}")

    return 1 if serious else 0


if __name__ == "__main__":
    raise SystemExit(main())
