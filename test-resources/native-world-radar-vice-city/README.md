# Native-world Vice City radar

This client-only resource registers Vice City's 80 reviewed TXD tiles without
loading the legacy Lua 3D map. Start it beside a native-world pack set that
contains Vice City:

```text
start native-world-radar-vice-city
```

Use `/nwradarvc` for runtime registry/cache statistics. Stopping or restarting
the resource removes only its own cells. The generated `assets/` directory is
intentionally ignored; rebuild it with:

```sh
python3 utils/extended-world/build_native_world_radar_resources.py
```

The source tiles remain the locally generated `test-resources/ug-vc` radar
catalog. The resource requires Neon's extended-radar client API.
