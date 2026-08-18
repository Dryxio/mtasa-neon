# Managed fire showcase

A short cinematic demo resource for the managed fire element API.

## Run

```text
start fire-showcase
/fireshow
```

Use `/fireshow stop` to abort and clean everything immediately.

The sequence is automatic and lasts about 27 seconds:

1. 68 managed fires form a vertical **NEON** sign, exceeding GTA's native 60-fire pool.
2. A synchronized strength wave travels across the sign.
3. One existing fire is highlighted, physically leaves the `O`, and moves toward an Infernus.
4. The exact same fire switches to `setFireTarget(vehicle)` and follows the moving car.
5. The camera pulls back for the final managed-fire capability card.

The resource temporarily stages the player in an isolated dimension at Verdant Meadows, hides/freezes the player during the cinematic, then restores position, rotation, dimension, interior, alpha and frozen state on completion or `/fireshow stop`.

## Recording notes

- Record at 16:9 if possible; captions are positioned for a standard widescreen frame.
- The resource deliberately uses no custom assets, shaders or textures: the visual is entirely managed fires + the normal GTA vehicle.
- For Discord, a 27-second capture works as-is. Trim the first/last second if you want a tighter clip.
- The important visual claim is not simply that MTA can create fire. The demo shows that a created fire has persistent identity and can be individually mutated and targeted without extinguishing/recreating it.
