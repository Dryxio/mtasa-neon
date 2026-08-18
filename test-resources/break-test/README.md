# Managed break effect regression harness

Start the resource and run:

```text
/breaktest all
```

Focused cases:

```text
/breaktest basic
/breaktest cache
/breaktest invalid
/breaktest stress [count]
/breaktest status
/breaktest reset
```

The harness exercises generic fracture creation from streamed RenderWare object geometry, managed element identity, fragment/triangle introspection, pause state, deterministic geometry-cache reuse, invalid arguments, multiple simultaneous effects and cleanup.

`createObjectBreakEffect` is intentionally separate from the existing native `breakObject` API. The legacy function keeps GTA SA's native break semantics; the managed API works from ordinary runtime object geometry and does not require the DFF breakable plugin.
