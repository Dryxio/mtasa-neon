# Native world v3 hot-switch set test

This coordinator selects the reviewed four-pack catalog in the canonical
Bullworth, Vice City, Liberty City, Carcer City order. Its set identity is
intentionally different from the three-pack `native-world-v3-set-test`
coordinator, so the headless lifecycle harness can prove that a detached
process admits a different catalog without restarting GTA.

Start all four publish-only child resources before this coordinator. This
resource owns only the aggregate authorization envelope; the large child
payloads remain runtime test data.
