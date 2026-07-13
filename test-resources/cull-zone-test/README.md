# CULL zone native CRUD test

This opt-in resource exercises Neon's client-side GTA CULL zone API. Start the
resource and use `/cullhelp` in the client.

Primary checks:

- `/cullstats` should report approximately 1180 attribute, 36 tunnel, and 65
  mirror entries before custom creation.
- `/culltest attribute 8 40` creates a `NO_RAIN` volume around the player.
- `/culledit 24 60` moves/resizes it and changes its flags to `NO_RAIN |
  NO_POLICE`.
- `/cullenable off`, `/cullenable on`, and `/culldelete` exercise lifecycle
  changes.
- `/cullclear` removes every custom zone created by the resource.
- `/cullvisual custom 300` draws custom zones; `/cullvisual all 300` also
  draws nearby original GTA zones and their decoded flags.
- `/cullboundary tunnel 41` and `/cullboundary mirror 73` cross the original
  native capacities of 40 and 72.
- `/cullnearest attribute`, `/cullvanilladisable`, and
  `/cullvanillarestore` exercise reversible edits to original IPL entries.

Stop or restart the resource after creating zones. All custom entries must be
removed and all original entries restored by owner cleanup.
