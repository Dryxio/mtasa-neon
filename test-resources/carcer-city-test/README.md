# Carcer City complete IMG test

This generated MTA resource imports the complete Clarksland and Lac Point
exterior from the Carcer City demo, including the subway geometry. It uses only
IPL `inst` entries whose area/interior field is exactly `0` and deliberately
shares the same resident-IMG loading path as Liberty City and Vice City. The
vehicle spawn sections and interior IPLs are not map geometry and are not
imported.

The test placement adds `6000` to X. This puts Carcer City in empty extended
world space without overlapping Liberty City or Vice City. All three cities
remain present server-side; each client keeps only the city it is visiting in
GTA's finite dynamic model/TXD slots.

Its 63 radar tiles are registered for the resource lifetime, independently of
that 3D residency, so Carcer remains visible in the shared F11 atlas.

When `city-residency-coordinator` is running, approaching Carcer City requests
this same client loader automatically. `/cctest` remains a manual debugging
override and no longer represents the normal arrival path.

Commands:

- `/cctest` (or `/carcertest`) prepares Carcer City, then creates an Infernus.
- `/ccback` returns to San Andreas.
- `/ccstreamstats` reports the resident IMG/model/TXD/placement state.
- `/ccradarstats` reports the extended radar tile state.
- `/ccinspect [radius]` prints the nearest imported instances to chat and F8.
- `/ccinspectaim` identifies the imported instance under the crosshair.
- `/ccprobe <index> status|object|original` compares one building with the same
  runtime model instantiated as an MTA object.

Generate the local resource:

```sh
python3 utils/extended-world/build_carcer_city.py \
    --archive /Users/salimtrouve/Downloads/GTA_Carcer_City_Demo.zip \
    --translate-x 6000 \
    --output test-resources/carcer-city-test

python3 utils/extended-world/extract_carcer_radar.py \
    --archive /Users/salimtrouve/Downloads/GTA_Carcer_City_Demo.zip \
    --output test-resources/carcer-city-test

python3 utils/extended-world/pack_img.py \
    --output test-resources/carcer-city-test/assets/carcer_models.img \
    test-resources/carcer-city-test/assets/models

python3 utils/extended-world/pack_img.py \
    --output test-resources/carcer-city-test/assets/carcer_textures.img \
    test-resources/carcer-city-test/assets/textures

python3 utils/extended-world/write_img_resource_meta.py \
    --resource test-resources/carcer-city-test \
    --info-name "Carcer City complete IMG test" \
    --server-script server.lua \
    --client-script map_data.lua \
    --client-script radar_tiles.lua \
    --client-script client.lua \
    --archive assets/carcer_models.img \
    --archive assets/carcer_textures.img \
    --file-dir assets/collisions \
    --file-dir assets/radar
```

`assets/`, `map_data.lua`, `radar_tiles.lua`, and `meta.xml` are generated and
ignored by Git so the repository does not version the demo's game assets. The
extracted DFF/TXD directories stay available locally for reproducibility but
are intentionally absent from `meta.xml`; clients download the two packed IMG
archives instead.
