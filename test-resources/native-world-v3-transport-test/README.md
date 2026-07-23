# Native world v3 Carcer transport test

This resource exercises the canonical static-world-v3 transport and cache
checkpoint with the generated Carcer City proof pack.

The payload is intentionally not tracked in Git. Generate it with
`utils/extended-world/build_native_world_v3.py`, then deploy these exact files
under the runtime resource's `native` directory:

- `native-world.json`
- `world.ide`
- `world.lod`
- `w000.img` through `w003.img`

Child-pack format 3 has no `startup` attribute. A successful test downloads, audits, and
atomically publishes the multi-IMG object while preserving stock GTA behavior:
`activation=no`, `lease=no`, and no client restart request.
