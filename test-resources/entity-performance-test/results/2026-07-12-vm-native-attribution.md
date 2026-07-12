# 2026-07-12 native entity attribution

This targeted pass used the varied built-in model set at origin
`(-690.64, 957.41, 12.19)`, five-second warm-ups and five-second samples. The
client ran in the foreground with `timingdebug on`. Diagnostic checkpoint
overhead makes the FPS distribution unsuitable for direct comparison with the
non-instrumented profiles; the purpose of this run is subsystem attribution.

| Stage | avg ms | p95 ms | p99 ms | worst ms |
| --- | ---: | ---: | ---: | ---: |
| baseline visible | 12.71 | 17.91 | 28.41 | 32.82 |
| vehicle 64 idle/visible/separate | 27.75 | 33.04 | 46.31 | 51.94 |
| vehicle 64 idle/hidden/separate | 19.22 | 25.61 | 31.73 | 33.35 |
| vehicle 64 idle/far/separate | 15.76 | 22.40 | 26.69 | 45.54 |
| vehicle 64 moving/visible/touching | 56.63 | 73.64 | 113.25 | 113.25 |
| ped 110 moving/visible/separate | 30.25 | 40.94 | 54.11 | 68.93 |
| ped 110 moving/hidden/separate | 26.27 | 36.63 | 43.41 | 48.02 |
| ped 110 moving/far/separate | 15.86 | 21.59 | 24.50 | 28.25 |

Representative one-second native snapshots from the measurement windows:

| Scenario | Whole frame | CWorld | Native collision | ProcessControl | Anim update | PreRender |
| --- | --- | --- | --- | --- | --- | --- |
| vehicle visible/separate | 25-36 ms | 2-3 ms | 1-2 ms, 66-68 calls | <1 ms, 56 calls | below 0.5 ms | 0-1 ms, 56 calls |
| vehicle hidden/separate | 17-20 ms | 4-6 ms | 2-3 ms, 76-79 calls | <1 ms, 56 calls | below 0.5 ms | absent |
| vehicle far/separate | 13-17 ms | about 1 ms | absent | absent | absent | absent |
| vehicle visible/touching | 51-69 ms | 21-30 ms | 19-25 ms, 139-170 calls | about 1 ms, 65-67 calls | below 0.5 ms | below 0.5 ms, 59 calls |
| ped visible/separate | 27-31 ms | 11-15 ms | 7-9 ms, 131-140 calls | 1-2 ms, 110 calls | 1-3 ms, 110 calls | 1-2 ms, 74-80 calls |
| ped hidden/separate | 21-32 ms | 10-15 ms | 7-10 ms, 129-139 calls | 0-1 ms, 110 calls | below 0.5 ms | absent |
| ped far/separate | 14-20 ms | 0-1 ms | absent | absent | absent | absent |

The measured collision-call count exceeds the entity count because
`CWorld::Process` retries entities that remain outside a safe position. For 64
touching vehicles, `CPhysical::ProcessCollision` consumed roughly 80-90% of
the measured `CWorld_Process` time. For 110 near peds it consumed roughly
55-75%. This makes native collision the first CPU optimization target for both
populations; MTA manager pulses were generally below 1 ms.

Visible separated vehicles still cost about 8.5 ms more per frame than hidden
vehicles even though measured automobile PreRender was at most about 1 ms and
their native world time was not higher. That remaining visible delta is outside
the current entity scopes. Instrument native `Render`/atomic traversal and use
an external GPU capture before deciding whether it is render-thread submission,
driver overhead, GPU work, or presentation waiting.

For peds, visible versus hidden differed by about 4 ms. The visible samples
accounted for roughly 1-3 ms of animation update and 1-2 ms of PreRender, while
near collision remained the largest component in both camera modes. A blanket
ped animation rewrite would therefore target a secondary cost before collision.

The client completed all eight stages and remained responsive. No crash was
observed.
