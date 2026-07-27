# Native-world Liberty City radar

This client-only resource registers Liberty City's 81 reviewed and translated
TXD tiles without loading the legacy Lua 3D map. Start it beside a native-world
pack set that contains Liberty City:

```text
start native-world-radar-liberty-city
```

Use `/nwradarlc` for runtime registry/cache statistics. Stopping or restarting
the resource removes only its own cells. The generated `assets/` directory is
intentionally ignored; rebuild it with:

```sh
python3 utils/extended-world/build_native_world_radar_resources.py
```

The source tiles remain the locally generated `test-resources/ug-lc` radar
catalog. The resource requires Neon's extended-radar client API.
