# Managed break effect regression harness

Start the resource and run:

```text
/breaktest all
```

Focused cases:

```text
/breaktest basic
/breaktest cache
/breaktest profile
/breaktest invalid
/breaktest stress [count]
/breaktest status
/breaktest reset
```

The harness exercises generic fracture creation from streamed RenderWare object geometry, managed element identity, fragment/triangle introspection, pause state, deterministic geometry-cache reuse, managed object durability profiles, invalid arguments, multiple simultaneous effects and cleanup.

The `profile` case covers `setObjectBreakProfile`, health get/set/reset, profile introspection and zero-health transition into a managed fracture effect.

`createObjectBreakEffect` is intentionally separate from the existing native `breakObject` API. `setObjectBreakProfile` layers GTA's native object-damage calls onto the managed fracture path: the legacy function keeps GTA SA's native break semantics, while profiled objects can use ordinary runtime geometry without requiring the DFF breakable plugin.
