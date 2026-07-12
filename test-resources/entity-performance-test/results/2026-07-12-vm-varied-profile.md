# 2026-07-12 Windows VM varied-model entity profile

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
