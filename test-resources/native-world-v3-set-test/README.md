# Native world v3 aggregate dry-run test

This coordinator resource contains only the immutable
`native/static-world-v3-set.json` envelope. It selects one through four child
packs that must already exist in the client cache. Entries must form a unique
subsequence of this canonical order:

1. Bullworth;
2. Vice City;
3. Liberty City;
4. Carcer City.

The checked-in test configuration normally selects all four. A checkpoint test
may regenerate the envelope with a singleton or non-contiguous canonical
subsequence to prove that the server, rather than the client binary, selects
the active catalog.

On the first launch the client publishes the envelope only after it can lock
and independently re-audit every selected child cache object. The format-3
startup ticket requires a full client restart.

On the authorized launch Game SA locks the envelope and all selected child
objects simultaneously, reruns the aggregate collision and capacity planner,
and keeps the exact leases for the process lifetime. It registers only the
selected catalog; transitions to a city omitted by the envelope remain in San
Andreas. Clients lacking protocol capability `0x3D` are refused before the
resource-start tuple is emitted.

Payload files are runtime-only and are not tracked by Git.
