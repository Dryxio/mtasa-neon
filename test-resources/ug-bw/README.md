# GTA Underground Bullworth IMG streaming prototype

This resource imports the contiguous area-0 Bullworth exterior from GTA
Underground. The area-5 dorm interior is deliberately excluded. Two isolated
ladder placements with coordinates thousands of units outside the rest of the
town are also excluded explicitly by the generator.

Bullworth is translated by `-16500` on X and `-6500` on Y, placing it at
approximately X `-9040..-7463`, Y `7044..8409`. This north-west location does
not overlap San Andreas, Liberty City, Vice City, or Carcer City. The 500-unit
aligned translation lets the original UG radar tiles be copied without
resampling.

The 952 DFFs and 166 TXDs are packed in standard IMG VER2 archives. As with
the other resident-city resources, Bullworth remains active server-side while
each client assigns its finite dynamic model/TXD slots only to the city that
client is visiting.

Its 15 radar tiles are registered for the resource lifetime, independently of
that 3D residency, so Bullworth remains visible in the shared F11 atlas.

When `city-residency-coordinator` is running, approaching Bullworth requests
this same client loader automatically. `/bullytest` remains a manual debugging
override and no longer represents the normal arrival path.

## Commands

- `/bullytest` prepares the academy and teleports to Bullworth. (`/bwtest` is
  already reserved by the extended-world boundary test.)
- `/bwback` or `/bullyback` returns to San Andreas.
- `/bwstreamstats` reports IMG/model/TXD/placement state.
- `/bwradarstats` reports the extended radar state.
- `/bwinspect [radius]` and `/bwinspectaim` identify imported placements.

## Regeneration

```sh
python3 utils/extended-world/build_ug_map.py \
    --gta /Users/salimtrouve/Downloads/ug-bw-source \
    --output test-resources/ug-bw \
    --map bw \
    --translate-x -16500 \
    --translate-y -6500

node utils/extended-world/build_ug_radar.mjs \
    --source /Users/salimtrouve/Downloads/ug-radar-samples \
    --repo /Users/salimtrouve/Documents/GitHub/mtasa-neon \
    --txd-tools /Users/salimtrouve/Documents/GitHub/gtastuff/shared \
    --map bw

python3 utils/extended-world/pack_img.py \
    --output test-resources/ug-bw/assets/bw_models.img \
    test-resources/ug-bw/assets/models

python3 utils/extended-world/pack_img.py \
    --output test-resources/ug-bw/assets/bw_textures.img \
    test-resources/ug-bw/assets/textures

python3 utils/extended-world/write_img_resource_meta.py \
    --resource test-resources/ug-bw \
    --info-name "GTA Underground Bullworth exterior" \
    --server-script server.lua \
    --client-script map_data.lua \
    --client-script radar_tiles.lua \
    --client-script client.lua \
    --archive assets/bw_models.img \
    --archive assets/bw_textures.img \
    --file-dir assets/collisions \
    --file-dir assets/radar
```

Generated assets and manifests are ignored by Git. Extracted DFF/TXD files
remain local for reproducibility but are intentionally not sent to clients as
individual resource files.
