# SA-MP 0.3.7 map loader

This client resource loads Texture Studio/Pawn maps without changing their
SA-MP model IDs. It resolves the official low range `11682..11753` and the
sparse high range `18631..19999` to resource-owned runtime slots, then creates
the complete map only after every required model has loaded successfully.

The runtime payload is bundled in this resource. To rebuild it from the
canonical SA-MP 0.3.7-R5 source package after an asset update, run:

```sh
python3 utils/samp/generate_samp_resource.py \
  --samp-dir /path/to/SA-MP/SAMP \
  --output official-resources/samp-map-loader
```

Deploy the generated resource, declare the `.pwn` file in the owner's resource
`meta.xml`, then call the client export:

```lua
local map, diagnostics = exports["samp-map-loader"]:loadSAMPMap(":my-maps/maps/interior.pwn")
```

Unload the returned handle with:

```lua
exports["samp-map-loader"]:unloadSAMPMap(map)
```

For local diagnostics, `/loadsampmap :resource/path.pwn` loads a map and
`/unloadsampmaps` releases maps created by that command.
