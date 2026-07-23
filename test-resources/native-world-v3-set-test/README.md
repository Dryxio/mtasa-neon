# Native world v3 aggregate dry-run test

This coordinator resource contains only the immutable
`native/static-world-v3-set.json` envelope. Its four child packs must already
exist in the client cache in this exact canonical order:

1. Bullworth;
2. Vice City;
3. Liberty City;
4. Carcer City.

On the first launch the client publishes the envelope only after it can lock
and independently re-audit all four exact child cache objects. The format-3
startup ticket requires a full client restart.

On the authorized launch Game SA locks the envelope and all four child objects
simultaneously, reruns the aggregate collision and capacity planner, releases
all five leases, terminalizes the one-shot ticket as `aggregate-dry-run`, and
continues with stock GTA behavior. This resource must not install model stores,
archives, IPLs, hooks, or native-world payload bytes into GTA.

Payload files are runtime-only and are not tracked by Git.
