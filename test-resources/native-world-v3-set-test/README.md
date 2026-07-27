# Native world v3 aggregate dry-run test

This coordinator resource contains only the immutable
`native/static-world-v3-set.json` envelope. It selects one through eight unique
child packs that must already exist in the client cache. The server-provided
order is part of `setId`; it is not sorted or inferred by the client.

The checked-in test configuration normally selects the four reviewed city
packs. A checkpoint test may regenerate the envelope with another ordered set
to prove that the server, rather than the client binary, selects the active
catalog.

On the first launch the client publishes the envelope only after it can lock
and independently re-audit every selected child cache object. The format-3
startup ticket requires a full client restart.

On the authorized launch Game SA locks the envelope and all selected child
objects simultaneously, reruns the aggregate collision and capacity planner,
and keeps the exact leases for the process lifetime. It registers only the
selected catalog; transitions to a city omitted by the envelope remain in San
Andreas. The server requires exact DM netcode epoch `0x1DF`; older clients are
refused before the format-3 authorization tuple is emitted. Generic sets and
extended Z share the network modules' highest effective bitstream capability,
`0x3D`, only after that incompatible epoch gate has succeeded.

Payload files are runtime-only and are not tracked by Git.
