# Native-world Bullworth radar

This client-only resource registers Bullworth's 15 reviewed TXD tiles without
loading the legacy Lua 3D map. Start it beside a native-world pack set that
contains Bullworth:

```text
start native-world-radar-bullworth
```

Use `/nwradarbw` for runtime registry/cache statistics. Stopping or restarting
the resource removes only its own cells. The generated `assets/` directory is
intentionally ignored; rebuild it with:

```sh
python3 utils/extended-world/build_native_world_radar_resources.py
```

The source tiles remain the locally generated `test-resources/ug-bw` radar
catalog. The resource requires Neon's extended-radar client API.
