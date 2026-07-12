# 2026-07-12 Windows VM varied-model entity profile

## Reference pass at the original profile location

This is the primary varied-model comparison. It ran at origin
`(-694.34, 957.69, 12.25)`, about four metres from the homogeneous profile's
`(-698.46, 958.41, 12.31)` origin. The visible baselines were 9.49 and 9.57 ms,
respectively. MTA remained in the foreground for the complete pass from
20:59:29 through 21:08:19.

| # | Scenario | FPS | avg ms | p95 ms | p99 ms | worst ms |
| ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 1 | baseline static/visible | 105.4 | 9.49 | 10.30 | 10.77 | 13.33 |
| 2 | baseline static/hidden | 89.6 | 11.16 | 12.21 | 13.47 | 17.67 |
| 3 | vehicle 16 idle/visible/separate | 75.3 | 13.27 | 14.62 | 15.51 | 20.79 |
| 4 | vehicle 32 idle/visible/separate | 56.8 | 17.61 | 18.72 | 21.03 | 26.40 |
| 5 | vehicle 48 idle/visible/separate | 49.0 | 20.39 | 21.78 | 24.61 | 28.08 |
| 6 | vehicle 64 idle/visible/separate | 42.4 | 23.60 | 24.95 | 26.99 | 30.57 |
| 7 | vehicle 64 idle/hidden/separate | 60.3 | 16.60 | 18.14 | 19.20 | 26.13 |
| 8 | vehicle 64 idle/far/separate | 80.7 | 12.38 | 13.65 | 16.83 | 36.10 |
| 9 | vehicle 64 moving/visible/separate | 40.7 | 24.54 | 26.25 | 28.43 | 31.80 |
| 10 | vehicle 16 moving/visible/touching | 73.5 | 13.61 | 14.83 | 15.68 | 20.01 |
| 11 | vehicle 32 moving/visible/touching | 40.5 | 24.66 | 28.99 | 31.56 | 35.06 |
| 12 | vehicle 64 moving/visible/touching | 19.2 | 52.10 | 61.61 | 66.20 | 67.09 |
| 13 | vehicle 4 moving/visible/deep-contact | 93.7 | 10.67 | 11.65 | 12.47 | 17.21 |
| 14 | vehicle 8 moving/visible/deep-contact | 84.4 | 11.84 | 13.34 | 15.01 | 21.84 |
| 15 | vehicle 16 moving/visible/deep-contact | 56.5 | 17.70 | 23.49 | 32.34 | 53.75 |
| 16 | vehicle 16 moving/visible/deep-contact, collision off | 75.0 | 13.34 | 15.62 | 22.52 | 255.36 |
| 17 | ped 32 idle/visible/separate | 72.2 | 13.85 | 15.00 | 16.04 | 18.14 |
| 18 | ped 64 idle/visible/separate | 56.5 | 17.71 | 18.95 | 21.59 | 32.36 |
| 19 | ped 96 idle/visible/separate | 47.0 | 21.29 | 22.94 | 24.65 | 28.73 |
| 20 | ped 110 idle/visible/separate | 44.7 | 22.35 | 23.86 | 27.77 | 30.06 |
| 21 | ped 110 moving/visible/separate | 40.1 | 24.91 | 26.15 | 27.16 | 33.89 |
| 22 | ped 110 moving/hidden/separate | 45.5 | 21.98 | 23.54 | 25.09 | 28.55 |
| 23 | ped 110 moving/far/separate | 80.9 | 12.35 | 13.67 | 14.60 | 19.77 |
| 24 | object 128 static/visible/separate | 93.5 | 10.69 | 11.85 | 12.93 | 17.70 |
| 25 | object 512 static/visible/separate | 89.5 | 11.18 | 12.37 | 13.75 | 17.61 |
| 26 | object 900 static/visible/separate | 91.6 | 10.91 | 11.97 | 13.54 | 16.58 |
| 27 | object 1000 static/visible/separate | 91.3 | 10.95 | 11.97 | 13.13 | 18.20 |
| 28 | object 1000 static/hidden/separate | 80.6 | 12.41 | 13.47 | 14.99 | 34.15 |
| 29 | object 1000 static/far/separate | 84.5 | 11.83 | 13.01 | 14.31 | 18.06 |
| 30 | object 900 moving/visible/separate | 89.0 | 11.23 | 12.35 | 13.36 | 17.82 |
| 31 | mixed 96 idle/visible/separate | 43.5 | 23.01 | 24.71 | 26.12 | 30.63 |
| 32 | mixed 192 idle/visible/separate | 34.5 | 28.95 | 31.05 | 35.90 | 37.81 |
| 33 | mixed 192 moving/visible/separate | 28.1 | 35.58 | 37.84 | 41.62 | 45.49 |

Key average-frame comparisons with the homogeneous pass:

| Scenario | Homogeneous ms | Varied ms | Change |
| --- | ---: | ---: | ---: |
| baseline visible | 9.57 | 9.49 | -0.8% |
| vehicle 64 idle/visible/separate | 18.60 | 23.60 | +26.9% |
| vehicle 64 moving/visible/separate | 19.68 | 24.54 | +24.7% |
| vehicle 64 moving/visible/touching | 38.89 | 52.10 | +34.0% |
| vehicle 16 moving/visible/deep-contact | 157.23 | 17.70 | -88.7% |
| ped 110 moving/visible/separate | 22.78 | 24.91 | +9.4% |
| object 1000 static/visible/separate | 10.03 | 10.95 | +9.2% |
| mixed 192 moving/visible/separate | 31.24 | 35.58 | +13.9% |

The normal separated/touching rows show a real diversity penalty, especially
for vehicles. The reversed deep-contact result is also meaningful: identical
collision shapes can remain locked in one pathological unresolved stack,
whereas varied shapes separate during warm-up. Deep overlap is therefore a
collision-geometry stress case, not a general count-only slope. The 255.36 ms
collision-off worst frame is an isolated outlier; its 22.52 ms p99 is retained.

Renderer high-water reached 89 visible pointers for 64 touching vehicles,
121 for 110 idle peds, and 120 for the 512-object row. All 33 stages completed,
the client stayed responsive, and no crash was observed.

## Earlier exploratory pass on uneven terrain

Environment:

- GTA SA 1.0 US under the Neon `Release|Win32` client;
- local server at `127.0.0.1:22003`;
- server FPS limit 120 and client VSync limit 120;
- unrelated Neon stress/demo resources stopped;
- fixed origin `(-2425.19, -608.20, 132.56)` and fixed cameras;
- deterministic built-in sets: 20 vehicle, 16 ped, and 18 object models;
- all 54 models requested before the final profile;
- five-second warm-up and one ten-second measurement per stage;
- client-local entities, excluding real network traffic and remote-player
  interpolation.

Several resource restarts occurred before this pass. The complete pass below
started at 20:37:20 and finished 33/33 at 20:46:10 while MTA remained in the
foreground. Earlier incomplete attempts are excluded. This is one pass, not
three repeats.

| # | Scenario | FPS | avg ms | p95 ms | p99 ms | worst ms |
| ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 1 | baseline static/visible | 96.0 | 10.41 | 11.59 | 12.58 | 19.41 |
| 2 | baseline static/hidden | 119.8 | 8.34 | 8.77 | 9.06 | 11.01 |
| 3 | vehicle 16 idle/visible/separate | 56.7 | 17.63 | 21.11 | 24.17 | 29.80 |
| 4 | vehicle 32 idle/visible/separate | 32.5 | 30.76 | 37.66 | 55.81 | 65.93 |
| 5 | vehicle 48 idle/visible/separate | 36.2 | 27.65 | 34.51 | 44.95 | 58.31 |
| 6 | vehicle 64 idle/visible/separate | 36.8 | 27.16 | 29.49 | 31.10 | 31.82 |
| 7 | vehicle 64 idle/hidden/separate | 60.4 | 16.54 | 18.75 | 19.68 | 43.28 |
| 8 | vehicle 64 idle/far/separate | 119.1 | 8.40 | 8.83 | 10.09 | 15.98 |
| 9 | vehicle 64 moving/visible/separate | 35.1 | 28.46 | 31.33 | 33.83 | 37.45 |
| 10 | vehicle 16 moving/visible/touching | 53.2 | 18.79 | 21.29 | 22.88 | 64.85 |
| 11 | vehicle 32 moving/visible/touching | 29.6 | 33.75 | 40.67 | 44.11 | 44.57 |
| 12 | vehicle 64 moving/visible/touching | 15.7 | 63.81 | 73.47 | 79.42 | 103.25 |
| 13 | vehicle 4 moving/visible/deep-contact | 80.3 | 12.45 | 18.20 | 21.74 | 24.94 |
| 14 | vehicle 8 moving/visible/deep-contact | 46.8 | 21.37 | 32.26 | 34.34 | 42.38 |
| 15 | vehicle 16 moving/visible/deep-contact | 9.1 | 110.10 | 124.32 | 185.00 | 185.00 |
| 16 | vehicle 16 moving/visible/deep-contact, collision off | 70.9 | 14.10 | 17.10 | 20.81 | 365.09 |
| 17 | ped 32 idle/visible/separate | 64.1 | 15.61 | 17.84 | 19.92 | 30.62 |
| 18 | ped 64 idle/visible/separate | 50.2 | 19.90 | 22.12 | 24.30 | 29.85 |
| 19 | ped 96 idle/visible/separate | 43.3 | 23.08 | 25.13 | 27.97 | 30.08 |
| 20 | ped 110 idle/visible/separate | 41.2 | 24.27 | 26.09 | 27.51 | 28.67 |
| 21 | ped 110 moving/visible/separate | 31.8 | 31.48 | 33.69 | 34.33 | 35.56 |
| 22 | ped 110 moving/hidden/separate | 44.3 | 22.56 | 24.98 | 30.63 | 67.33 |
| 23 | ped 110 moving/far/separate | 119.8 | 8.35 | 8.73 | 8.95 | 12.24 |
| 24 | object 128 static/visible/separate | 88.4 | 11.31 | 12.39 | 13.98 | 16.17 |
| 25 | object 512 static/visible/separate | 83.1 | 12.04 | 13.50 | 17.15 | 40.41 |
| 26 | object 900 static/visible/separate | 85.9 | 11.64 | 12.56 | 14.08 | 17.26 |
| 27 | object 1000 static/visible/separate | 86.2 | 11.60 | 13.12 | 13.73 | 15.63 |
| 28 | object 1000 static/hidden/separate | 119.1 | 8.39 | 8.87 | 9.98 | 13.93 |
| 29 | object 1000 static/far/separate | 115.0 | 8.70 | 10.40 | 17.26 | 92.06 |
| 30 | object 900 moving/visible/separate | 120.0 | 8.33 | 8.95 | 9.51 | 13.79 |
| 31 | mixed 96 idle/visible/separate | 117.0 | 8.55 | 9.96 | 12.36 | 21.06 |
| 32 | mixed 192 idle/visible/separate | 80.5 | 12.42 | 14.17 | 16.25 | 22.41 |
| 33 | mixed 192 moving/visible/separate | 55.4 | 18.06 | 20.11 | 21.58 | 35.71 |

Renderer high-water observations:

- visible entities were 91 for the visible baseline and 33 for hidden;
- 64 separated and touching vehicles reached 139 and 141 visible pointers;
- 110 idle and moving peds reached 165 and 179 visible pointers;
- varied objects reached 164 visible pointers and 1361 streaming RwObjects,
  materially exceeding the one-model object's roughly 88-89 visible plateau;
- moving objects and mixed populations settled or moved on uneven terrain, so
  those rows are not a clean model-diversity comparison with the earlier flat
  origin.

The 365.09 ms collision-off worst frame and the 92.06 ms far-object worst frame
are isolated outliers; their p99 values are retained and should guide repeat
testing. No crash was observed and the client remained responsive.
