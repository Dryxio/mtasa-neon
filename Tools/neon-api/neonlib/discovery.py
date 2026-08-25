from __future__ import annotations

import re
import unicodedata
from collections.abc import Iterable
from dataclasses import dataclass
from typing import Any


# Search stays dependency-free and deliberately transparent. These aliases are
# development vocabulary, not API facts: they help an agent reach the exact
# catalogue entry, whose contract and evidence remain the authority.
ALIAS_GROUPS = (
    ("alpha", "opacity", "transparent", "transparency", "invisible"),
    ("visibility", "visible", "hide", "hidden", "showing"),
    ("armor", "armour", "armure"),
    ("audio", "sound", "music"),
    ("bird", "oiseau"),
    ("browser", "web", "url"),
    ("camera", "view", "viewport"),
    ("car", "automobile", "vehicle", "voiture"),
    ("character", "npc", "ped", "pedestrian", "actor"),
    ("click", "mouse", "cursor"),
    ("collision", "collide", "hitbox"),
    ("coordinate", "coordinates", "position", "location"),
    ("create", "add", "spawn"),
    ("damage", "hurt", "injure"),
    ("database", "db", "sql"),
    ("delete", "destroy", "remove"),
    ("display", "draw", "render", "show"),
    ("download", "fetch", "request"),
    ("drive", "driving"),
    ("event", "callback", "handler"),
    ("freeze", "frozen", "pause", "paused"),
    ("get", "find", "list", "query", "read", "retrieve"),
    ("health", "hp", "vie"),
    ("input", "key", "keyboard", "keypress"),
    ("join", "connect", "connection"),
    ("leave", "disconnect", "quit"),
    ("model", "skin", "appearance"),
    ("move", "movement", "walk", "walking"),
    ("navigate", "navigation", "path", "pathfinding", "route", "routing"),
    ("object", "prop"),
    ("player", "user", "gamer", "joueur"),
    ("rope", "cable", "winch"),
    ("http", "https", "web"),
    ("playback", "replay"),
    ("seabed", "seafloor", "oceanfloor", "ocean", "sea", "floor"),
    ("set", "change", "configure", "modify", "update"),
    ("shoot", "fire", "weapon"),
    ("text", "label", "message"),
    ("timer", "delay", "interval", "schedule", "timeout"),
    ("tire", "tyre"),
    ("world", "map"),
)

STOP_WORDS = {
    "a", "an", "and", "api", "avec", "can", "comment", "dans", "de", "des", "do", "du", "en", "est", "et",
    "for", "function", "how", "i", "in", "is", "je", "la", "le", "les", "lua", "me", "mon", "mta", "my",
    "make", "neon", "of", "on", "or", "please", "pour", "that", "the", "this", "to", "un", "une", "use", "using",
    "veut", "want", "with",
}

CAMEL_BOUNDARY = re.compile(r"(?<=[a-z0-9])(?=[A-Z])|(?<=[A-Z])(?=[A-Z][a-z])")
WORD_RE = re.compile(r"[a-z0-9]+")
COMPOUND_TOKENS = {"raycast": ("ray", "cast"), "seafloor": ("sea", "floor"), "oceanfloor": ("ocean", "floor")}

ACTION_PREFIXES = {
    "add": ("add", "create"),
    "change": ("set", "change", "update"),
    "configure": ("set", "configure"),
    "create": ("create", "add", "spawn"),
    "delay": ("set", "start", "create"),
    "delete": ("delete", "destroy", "remove"),
    "destroy": ("destroy", "delete", "remove"),
    "destructible": ("set", "create"),
    "display": ("display", "draw", "render", "show"),
    "download": ("fetch", "download", "request"),
    "draw": ("draw", "render", "show"),
    "fetch": ("fetch", "request"),
    "find": ("find", "get", "is"),
    "get": ("get", "is", "has"),
    "list": ("get", "list"),
    "modify": ("set", "modify", "update"),
    "query": ("query", "get", "find"),
    "read": ("get", "read"),
    "remove": ("remove", "delete", "destroy"),
    "request": ("fetch", "request"),
    "schedule": ("set", "start", "create"),
    "set": ("set", "enable", "disable"),
    "show": ("show", "draw", "render", "display"),
    "spawn": ("spawn", "create"),
    "update": ("set", "update"),
}


def _fold(value: str) -> str:
    normalized = unicodedata.normalize("NFKD", value)
    return "".join(character for character in normalized if not unicodedata.combining(character)).casefold()


def _stem(token: str) -> str:
    if len(token) > 5 and token.endswith("ies"):
        return token[:-3] + "y"
    for suffix in ("ing", "ers", "ed"):
        if len(token) > len(suffix) + 3 and token.endswith(suffix):
            return token[: -len(suffix)]
    if len(token) > 4 and token.endswith("s") and not token.endswith("ss"):
        return token[:-1]
    return token


def tokenize(value: str, *, drop_stop_words: bool = False) -> tuple[str, ...]:
    separated = CAMEL_BOUNDARY.sub(" ", value)
    raw_tokens = WORD_RE.findall(_fold(separated))
    expanded = (part for token in raw_tokens for part in COMPOUND_TOKENS.get(token, (token,)))
    tokens = tuple(_stem(token) for token in expanded)
    if drop_stop_words:
        tokens = tuple(token for token in tokens if token not in STOP_WORDS)
    return tokens


ALIASES: dict[str, frozenset[str]] = {}
for group in ALIAS_GROUPS:
    normalized = frozenset(_stem(token) for token in group)
    for token in normalized:
        ALIASES[token] = normalized


def _values(value: Any) -> Iterable[str]:
    if isinstance(value, str):
        yield value
    elif isinstance(value, list):
        for item in value:
            yield from _values(item)
    elif isinstance(value, dict):
        for key in sorted(value):
            yield from _values(value[key])


def _member_values(symbol: dict[str, Any]) -> Iterable[str]:
    for field in ("methods", "properties"):
        for member in symbol.get(field, []):
            for key in (
                "name", "globalFunctions", "inheritedGlobalFunctions", "setters", "getters",
                "inheritedSetters", "inheritedGetters", "nativeFunction", "nativeSetter", "nativeGetter",
            ):
                yield from _values(member.get(key))
    yield from _values(symbol.get("oopOnlyMethods"))


def _semantic_hints(symbol: dict[str, Any]) -> tuple[str, ...]:
    hints: set[str] = set()
    name = symbol.get("name", "")
    category = symbol.get("category", "")
    if category == "population":
        hints.update(("ambient", "civilian", "npc"))
        if "Vehicle" in name:
            hints.add("traffic")
    if category == "fracture":
        hints.update(("breakable", "destructible", "durability"))
    if "Tyre" in name:
        hints.add("tire")
    if "Playback" in name:
        hints.add("replay")
    if "StraightLineDistance" in name:
        hints.update(("ahead", "distance", "straight"))
    if name == "setTimer":
        hints.update(("callback", "delay", "schedule"))
    return tuple(sorted(hints))


def _field_tokens(values: Iterable[str]) -> frozenset[str]:
    return frozenset(token for value in values for token in tokenize(value))


def symbol_search_fields(symbol: dict[str, Any]) -> tuple[frozenset[str], ...]:
    contracts = symbol.get("contracts", [])
    return (
        _field_tokens((symbol.get("name", ""),)),
        _field_tokens(_member_values(symbol)),
        _field_tokens((symbol.get("category", ""), *symbol.get("parents", []), *symbol.get("inheritedParents", []), *_semantic_hints(symbol))),
        _field_tokens((symbol.get("description", ""), *[contract.get("description", "") for contract in contracts])),
        _field_tokens((
            *[parameter.get("name", "") for parameter in symbol.get("parameters", [])],
            *[parameter.get("type", "") for parameter in symbol.get("parameters", [])],
            *[parameter.get("description", "") for parameter in symbol.get("parameters", [])],
            *[returned.get("name", "") for returned in symbol.get("returns", [])],
            *[returned.get("type", "") for returned in symbol.get("returns", [])],
            *symbol.get("values", []), *symbol.get("inheritedValues", []),
        )),
        _field_tokens((
            *[contract.get("signature", "") for contract in contracts],
            *[contract.get("example", "") for contract in contracts],
            *[note for contract in contracts for note in contract.get("notes", [])],
            *[contract.get("sourcePath", "") for contract in contracts],
        )),
    )


def discovery_keywords(symbol: dict[str, Any], limit: int = 16) -> list[str]:
    fields = symbol_search_fields(symbol)
    preferred = [*sorted(fields[0]), *sorted(fields[2]), *sorted(fields[1]), *sorted(fields[3])]
    keywords: list[str] = []
    for token in preferred:
        if len(token) < 2 or token in STOP_WORDS or token in keywords:
            continue
        keywords.append(token)
        if len(keywords) == limit:
            break
    return keywords


def _edit_distance_at_most_one(left: str, right: str) -> bool:
    if abs(len(left) - len(right)) > 1:
        return False
    if left == right:
        return True
    if len(left) > len(right):
        left, right = right, left
    if len(left) == len(right):
        differences = sum(a != b for a, b in zip(left, right))
        return differences <= 1
    index_left = index_right = differences = 0
    while index_left < len(left) and index_right < len(right):
        if left[index_left] == right[index_right]:
            index_left += 1
        else:
            differences += 1
            if differences > 1:
                return False
        index_right += 1
    return True


def _token_score(query: str, fields: tuple[frozenset[str], ...]) -> int:
    weights = (24, 18, 14, 9, 6, 3)
    aliases = ALIASES.get(query, frozenset((query,)))
    best = 0
    for index, tokens in enumerate(fields):
        weight = weights[index]
        if query in tokens:
            best = max(best, weight)
            continue
        if any(alias in tokens for alias in aliases if alias != query):
            best = max(best, weight * 3 // 4)
            continue
        if len(query) >= 4 and any(token.startswith(query) for token in tokens if len(token) >= 4):
            best = max(best, weight * 2 // 3)
            continue
        if len(query) >= 5 and index <= 3 and any(_edit_distance_at_most_one(query, token) for token in tokens if len(token) >= 5):
            best = max(best, weight // 2)
    return best


@dataclass(frozen=True)
class SearchMatch:
    score: int
    coverage: int
    symbol: dict[str, Any]


def rank_symbol(symbol: dict[str, Any], query: str) -> SearchMatch | None:
    query_tokens = tokenize(query, drop_stop_words=True)
    if not query_tokens:
        return None
    fields = symbol_search_fields(symbol)
    scores = tuple(_token_score(token, fields) for token in query_tokens)
    coverage = sum(score > 0 for score in scores)
    compact_name = "".join(tokenize(symbol.get("name", "")))
    collection_intent = "list" in query_tokens and "elementbytype" in compact_name
    if collection_intent:
        # The element type is a runtime string, so a generic catalogue entry
        # cannot enumerate every possible plural noun in its own contract.
        coverage = len(query_tokens)
    required = len(query_tokens) if len(query_tokens) <= 2 else (len(query_tokens) * 2 + 2) // 3
    if coverage < required:
        return None

    folded_name = " ".join(tokenize(symbol.get("name", "")))
    folded_query = " ".join(query_tokens)
    compact_name = folded_name.replace(" ", "")
    compact_query = folded_query.replace(" ", "")
    phrase_bonus = (
        120 if compact_name == compact_query
        else 100 if compact_name.endswith(compact_query)
        else 70 if compact_name.startswith(compact_query)
        else 0
    )
    # Generic collection requests are otherwise dominated by APIs that merely
    # mention the requested entity in their name. This is a structural intent,
    # not a hand-authored answer for one entity type.
    if collection_intent:
        phrase_bonus += 80
    raw_query_tokens = tokenize(query)
    state_change_terms = ALIASES["alpha"] | ALIASES["freeze"] | {"enable", "disable", "visible", "shootable"}
    if "make" in raw_query_tokens and state_change_terms.intersection(query_tokens) and compact_name.startswith("set"):
        phrase_bonus += 90
    for action in query_tokens:
        prefixes = ACTION_PREFIXES.get(action, ())
        if any(compact_name.startswith(prefix) for prefix in prefixes):
            phrase_bonus += 65
            break
    coverage_bonus = coverage * 20 + (40 if coverage == len(query_tokens) else 0)
    state_bonus = {"verified": 8, "runtime-only": 5, "documented-only": 3, "opaque": 1}.get(symbol.get("state"), 0)
    return SearchMatch(sum(scores) + phrase_bonus + coverage_bonus + state_bonus, coverage, symbol)


def search_symbols(symbols: Iterable[dict[str, Any]], query: str) -> list[dict[str, Any]]:
    matches = [match for symbol in symbols if (match := rank_symbol(symbol, query)) is not None]
    matches.sort(
        key=lambda match: (
            -match.score, -match.coverage, match.symbol["name"].casefold(), match.symbol["name"], match.symbol["kind"],
        )
    )
    return [match.symbol for match in matches]
