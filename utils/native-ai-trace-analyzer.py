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
SPATIAL_MAX_DISTANCE = 1.0
SPATIAL_MAX_HEADING = 45.0
PAIR_WINDOW_MS = 750.0
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
    return str(trace.get("actor_id") or ped.get("traffic_id") or ped.get("mta_element_id") or "unknown")


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
    return abs((float(left) - float(right) + 180.0) % 360.0 - 180.0)


def record_evidence(record: dict[str, Any]) -> dict[str, Any]:
    return {
        "source": record.get("_source"),
        "line": record.get("_line"),
        "wall_utc": record.get("wall_utc"),
        "client": record.get("client_identity"),
        "event": record.get("event"),
        "actor": actor_label(record),
        "action_id": trace_context(record).get("action_id"),
        "packet": packet(record),
    }


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
    for record in sorted(records, key=lambda item: item["_timestamp_ms"]):
        event = record.get("event")
        ped = record.get("ped") if isinstance(record.get("ped"), dict) else {}
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
        if delta is not None and delta > SPATIAL_MAX_HEADING:
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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--primary", type=Path, required=True, help="primary client JSONL")
    parser.add_argument("--cl2", type=Path, required=True, help="secondary client JSONL")
    parser.add_argument("--server", type=Path, required=True, help="harness server JSONL")
    parser.add_argument("--run-id", default="latest", help="run to analyze (default: latest)")
    parser.add_argument("--json", action="store_true", help="emit a machine-readable report")
    args = parser.parse_args()

    records: list[dict[str, Any]] = []
    findings: list[Finding] = []
    for path, source in ((args.primary, "primary"), (args.cl2, "cl2"), (args.server, "server")):
        loaded, errors = read_jsonl(path, source)
        records.extend(loaded)
        findings.extend(errors)

    all_records = records
    selected = select_run(all_records, args.run_id)
    if selected:
        records = [record for record in all_records if str(run_id(record)) == selected]
    elif args.run_id != "latest":
        findings.append(Finding(-math.inf, "input", f"Run {args.run_id!r} was not found", {}))

    # Writer sequence numbers are process-global. Analyze the complete files so
    # records from unrelated actors do not look like drops after run filtering.
    findings.extend(analyze_sequences(all_records))
    findings.extend(analyze_ownership(records))
    findings.extend(analyze_receive_apply(records))
    findings.extend(analyze_transport(records))
    findings.extend(analyze_harness_pipeline(records))
    findings.extend(analyze_assertions(records))
    stage_priority = {"input": 0, "jsonl": 0, "writer": 1, "causal_chain": 2, "assertion": 3}
    findings.sort(key=lambda finding: (finding.timestamp_ms, stage_priority.get(finding.stage, 4), finding.stage, finding.message))

    event_counts = Counter(str(record.get("event", "unknown")) for record in records)
    actors = sorted({actor_label(record) for record in records if actor_label(record) != "unknown"})
    finite_times = [record["_timestamp_ms"] for record in records if math.isfinite(record["_timestamp_ms"])]
    summary = {
        "run_id": selected,
        "records": len(records),
        "actors": actors,
        "duration_ms": max(finite_times) - min(finite_times) if finite_times else 0,
        "events": dict(event_counts),
        "finding_count": len(findings),
        "first_divergence": None,
    }
    if findings:
        first = findings[0]
        summary["first_divergence"] = {"stage": first.stage, "message": first.message, "evidence": first.evidence}

    if args.json:
        print(json.dumps(summary, ensure_ascii=False, indent=2))
    else:
        print(f"run={selected or 'unscoped'} records={len(records)} actors={','.join(actors) or '-'} duration_ms={summary['duration_ms']:.0f}")
        if findings:
            first = findings[0]
            print(f"FIRST_DIVERGENCE stage={first.stage}: {first.message}")
            print(json.dumps(first.evidence, ensure_ascii=False, indent=2))
            if len(findings) > 1:
                print(f"additional_findings={len(findings) - 1}")
        else:
            print("FIRST_DIVERGENCE none detected by the current invariants")

    return 1 if findings else 0


if __name__ == "__main__":
    raise SystemExit(main())
