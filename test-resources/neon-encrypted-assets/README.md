# Neon encrypted assets runtime test

This resource is completed at deployment time with three real WOSA assets. The
plaintext source models and the generated server-only key are deliberately not
committed.

The deployed `meta.xml` must contain one `<neon_assets>` descriptor, this client
script, and three auto-download `<file neon_asset="true">` entries. Pack the TXD,
DFF, and COL with the exact deployed resource name and paths shown in
`client.lua`.

Expected result: all three calls return elements, the client log contains:

```text
[neon-encrypted-assets] authenticated TXD/DFF/COL replacements loaded
```

and the server log records the same authenticated replacement checkpoint with
the connected player's name.

Also verify that a modified ciphertext, a renamed container, and a wrong package
id fail authentication without creating or replacing an engine element.
