# Native world v3 aggregate dry-run test

This coordinator resource contains only the immutable
`native/static-world-v3-set.json` envelope. It selects one through eight unique
child packs that must already exist in the client cache. The server-provided
order is part of `setId`; it is not sorted or inferred by the client.

The checked-in test configuration normally selects the four reviewed city
packs. A checkpoint test may regenerate the envelope with another ordered set
to prove that the server, rather than the client binary, selects the active
catalog.

On an ordinary content-neutral launch, Game SA installs only the native-world
foundation: expanded model stores, the stock `LoadCdDirectory` interception,
and reserved LOD storage. After the server publishes the envelope, the client
locks and independently re-audits every selected child cache object. The first
format-3 ticket can then cross the one-shot runtime admission fence in that
same GTA process; no authorization restart is required.

The runtime admission transaction rebases the content-neutral baseline,
reruns the aggregate collision and capacity planner, grows the streaming
buffer, revalidates the exact session and cache leases, and only then commits
the registrar. The leases remain held for the process lifetime. It registers
only the selected catalog; transitions to a city omitted by the envelope
remain in San Andreas. The server requires exact DM netcode epoch `0x1E5`;
older clients are refused before the format-3 authorization tuple is emitted.
Generic sets and extended Z share the network modules' highest effective
bitstream capability, `0x3D`, only after that incompatible epoch gate has
succeeded.

Payload files are runtime-only and are not tracked by Git.
