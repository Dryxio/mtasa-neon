# Extended native radar test

This resource maps a 3x3 sample of stock San Andreas radar TXDs onto the native
radar around world positions `(9000, 0)` and `(-9000, 0)`. The source files are
extracted locally and are intentionally not committed.

Generate the assets from a legitimate GTA San Andreas installation:

```sh
python3 utils/extended-world/extract_radar_test_tiles.py \
    --gta3-img /path/to/GTA/models/gta3.img
```

Start `extended-radar-test`, then use:

- `/radartest [x]` to teleport to a boundary (`+/-2999`, `3000`, `4095`,
  `4096`, `8999`, `9000`, or `9999`);
- `/radarstats` to print registry/cache diagnostics;
- `/radarmissing` to destroy the center TXD and verify automatic cleanup plus
  the ocean fallback;
- `/radarreload` to register all nine tiles again;
- `/radarback` to return to San Andreas.

The public grid is 40x40 for world coordinates `[-10000, 10000)`, with
500-world-unit tiles. Columns increase west-to-east and rows increase
north-to-south. The stock San Andreas 12x12 block occupies logical columns and
rows `14..25` and cannot be replaced by this API.
