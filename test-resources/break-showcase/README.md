# Managed object fracture showcase

Run:

```text
start break-showcase
/breakshow
```

Use `/breakshow stop` to abort and restore the player immediately.

The sequence is intentionally UI-free: it stages several ordinary GTA object models, fractures them in a travelling wave with different fragment counts/impulses, then destroys a denser mixed-scale finale while the camera moves around the debris.

No custom DFF, TXD, shader or fracture metadata is required. Every fragment is generated from the object's live RenderWare geometry, keeps the source UV/material texture data, and is simulated by Neon's managed break-effect system rather than GTA SA's fixed native breakable pool.
