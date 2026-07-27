# Native-world Carcer City radar

This client-only resource registers Carcer City's 63 reviewed TXD tiles without
loading the legacy Lua 3D map. Start it beside a native-world pack set that
contains Carcer City:

```text
start native-world-radar-carcer-city
```

Use `/nwradarcc` for runtime registry/cache statistics. Stopping or restarting
the resource removes only its own cells. The generated `assets/` directory is
intentionally ignored; rebuild it with:

```sh
python3 utils/extended-world/build_native_world_radar_resources.py
```

The source tiles remain the locally generated `test-resources/carcer-city-test`
radar catalog. The resource requires Neon's extended-radar client API.
