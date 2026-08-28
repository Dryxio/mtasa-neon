# Neon encrypted assets

This tool creates authenticated `.neonasset` containers for the Neon client.
It protects downloadable DFF, TXD, and COL files at rest; it is not a claim that
client-side assets can be made impossible to dump from a running game.

Create one content key and package id per resource package:

```sh
python -m pip install -r requirements.txt
python neon_asset_pack.py keygen --key-file neon-assets.key
```

Keep `neon-assets.key` server-only. Do not declare it as a `<file>` or script.
Use the printed package id when packing every asset and in `meta.xml`:

```sh
python neon_asset_pack.py pack \
  --key-file neon-assets.key \
  --package 00112233445566778899aabbccddeeff \
  --resource my-resource \
  --path models/taxi.dff.neonasset \
  --type dff \
  --input taxi.dff \
  --output models/taxi.dff.neonasset
```

```xml
<meta>
    <neon_assets package="00112233445566778899aabbccddeeff" keyfile="neon-assets.key" />
    <script src="client.lua" type="client" />
    <file src="models/taxi.dff.neonasset" neon_asset="true" />
</meta>
```

The client API authenticates, decrypts, loads, and applies the asset atomically:

```lua
local dff = engineReplaceEncryptedModel("models/taxi.dff.neonasset", 411)
assert(dff, "encrypted DFF replacement failed")
```

The optional third and fourth arguments control DFF alpha transparency and TXD
filtering respectively. Version 1 intentionally rejects clothing-model targets
because RenderWare retains the source TXD/DFF pointer for their lifetime.

Use `verify` to authenticate a container offline. Omitting `--output` never
writes plaintext:

```sh
python neon_asset_pack.py verify --key-file neon-assets.key \
  --package 00112233445566778899aabbccddeeff --resource my-resource \
  --path models/taxi.dff.neonasset --input models/taxi.dff.neonasset
```
