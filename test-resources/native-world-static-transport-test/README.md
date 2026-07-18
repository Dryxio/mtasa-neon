# Native static-world v2 transport test

This metadata-only resource exercises the inert format-2
`static-world-v1` transport policy. It deliberately does not request startup
authorization: a successful publication must retain
`activation=no lease=no restart-required=no`.

Stage exactly three files below `native/`: a closed format-2
`native-world.json`, one IDE, and one IMG. For the initial Bullworth fixture,
the manifest identity is:

```json
{
  "format": 2,
  "policy": "static-world-v1",
  "pack_id": "bullworth",
  "files": {
    "ide": {"name": "world.ide", "bytes": 31760, "sha256": "0bdf5aeb17eaefe6e2f42e47d38f82d65526c580f3eecc223b7b65f8b905eeb4"},
    "img": {"name": "world.img", "bytes": 169545728, "sha256": "23e596450bf0128fec49d2c31245c5a86269266915429bffbc57d61a113b3540"}
  }
}
```

The runtime fully derives and audits the IDE/IMG inventory. `pack_id` is only
an untrusted bounded identity included in the semantic content ID; it cannot
select parser budgets, executable patches, pools, archive slots, or paths.
