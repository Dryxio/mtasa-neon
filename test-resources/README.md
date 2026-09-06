# Test resources

Development resources for exercising Neon APIs: regression harnesses,
showcases, experiments, and mission scenarios. They may create fixtures,
teleport players, alter world state, or require a specific test setup. Read
the individual README and install only the resources needed for your test.

For ready-to-use server systems, see [official-resources/](../official-resources/README.md):

- [Native pedestrian traffic](../official-resources/native-ped-traffic/README.md)
- [Native vehicle traffic](../official-resources/native-vehicle-traffic/README.md)
- [All official resources and reusable libraries](../official-resources/README.md)

The moved resources retain their existing deployed names and startup
commands; their source directories have moved out of this test collection.

Mission reconstructions, generated-city experiments, and the radio carousel
with its test-vehicle commands remain here. Reusable native-task, story-world,
and entry-exit libraries are in `official-resources/`; their standalone
regression callers stay in this directory.
