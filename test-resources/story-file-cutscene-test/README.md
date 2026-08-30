# Story grouped file-cutscene test

This isolated caller verifies the reusable `story-world-runtime` grouped native
file-cutscene contract without embedding mission logic. It temporarily leases
model 0 for every connected player, preserving and restoring each original
model around the run.

Start the resource, then run:

```text
storycutscenetest [name] [visibleArea]
storycutscenetestskip
```

The default name is `SWEET1A`; the optional visible area is passed directly to
the runtime after range validation. The second command exercises the
server-authorized broadcast skip and is accepted from the server console or the
declared leader. Native leader skip input exercises the other authorization
path. A successful run prints a terminal `PASS` only after observing
`loaded>started>finished>released` for every participant. Any native failure,
timeout, invalid state order, caller stop, or runtime restart uses the runtime's
safety cleanup and restores player models.
