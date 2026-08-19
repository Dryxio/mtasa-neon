# Managed break explosion regression harness

Focused client-side coverage for explosion-driven `setObjectBreakProfile` damage.

## Commands

- `/breakexptest` runs the automated regression sequence.
- `/breakexplosiontarget [model]` spawns one 250-health managed target in front of the player for a real rocket-launcher or Rhino-shell check.
- `/breakexpreset` removes harness objects.

## Automated coverage

The sequence uses real `createExplosion` calls so damage goes through GTA's explosion implementation rather than calling the managed damage code directly.

It checks:

- an object without a managed profile does not create a managed break effect;
- a near rocket applies one 300-point managed hit, catching duplicate native `ObjectDamage` charging;
- rocket radial falloff;
- no damage outside the explosion radius;
- weak-rocket 0.2 damage scaling;
- tank-shell explosion damage;
- `damaging=false` remains non-damaging;
- an explosion can drive managed health to zero and create a `break-effect`.

Expected damage follows GTA's object explosion radial factor: `min(1, 2 * (radius - distance) / radius) * 300`, with the weak rocket multiplying damage by `0.2`.

## Manual validation

Run `/breakexplosiontarget`, then hit the target with the GTA rocket launcher. Repeat with a Rhino cannon. The object should fracture from the explosion position even when the source model's vanilla object data would otherwise make it explosion-proof.