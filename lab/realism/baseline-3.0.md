# 3.0 realism baseline (Ultra)

Measured 22 November 2026 on the Owner's laptop at Ultra, commit 56a38e6 (main) with the 20-pose set (`lab/realism/poses.json`), moving traffic off. 16 of 20 poses were scored: three photograph hosts were down (seattle, r30-3-city, r30-5-forest) and r30 poses are replaced later. Mean over the 16: **40.0 / 100**.

The side-by-side images are in `Saved/Realism/captures/20261122-133243/` (not committed). Findings from the frames: the Dragon's foliage is near black (shade and GI), Nebraska's sky is more saturated blue than the overcast photograph and its road is too pale and yellow, ground reads as flat green paint, the cockpit is near black against a bright exterior (Coordinator review, #99).

```
REALISM lummus: 49.9 / 100  layout 28.7  tone 45.1  colour 58.6  structure 52.1  detail 89.3  sky 37.0
REALISM dragon: 31.2 / 100  layout 18.2  tone 5.8  colour 28.8  structure 54.3  detail 59.4  sky 4.0
REALISM bixby: 34.0 / 100  layout 20.1  tone 20.9  colour 31.5  structure 60.1  detail 44.0  sky 10.4
REALISM bronx: 44.3 / 100  layout 22.9  tone 53.6  colour 13.9  structure 59.8  detail 73.6  sky 32.0
REALISM ankeny: 36.2 / 100  layout 23.3  tone 31.2  colour 17.8  structure 54.4  detail 58.2  sky 16.0
REALISM rawlins: 44.1 / 100  layout 34.1  tone 33.0  colour 11.7  structure 53.5  detail 92.1  sky 22.9
REALISM nyc-lower-east: 30.5 / 100  layout 23.5  tone 16.3  colour 9.7  structure 35.5  detail 69.9  sky 18.5
REALISM nebraska: 41.6 / 100  layout 34.5  tone 19.8  colour 17.9  structure 58.0  detail 82.5  sky 13.8
REALISM detroit: 36.0 / 100  layout 29.1  tone 17.8  colour 15.3  structure 50.4  detail 66.4  sky 20.0
REALISM anchorage: 29.2 / 100  layout 22.2  tone 16.0  colour 7.7  structure 42.9  detail 59.5  sky 8.2
REALISM r30-1-mountains: 40.0 / 100  layout 28.6  tone 26.1  colour 23.5  structure 58.8  detail 62.1  sky 25.3
REALISM r30-2-plains: 55.0 / 100  layout 32.7  tone 16.3  colour 93.9  structure 73.5  detail 93.9  sky 25.4
REALISM r30-4-small-town: 47.6 / 100  layout 29.9  tone 39.3  colour 60.3  structure 60.9  detail 55.7  sky 45.8
REALISM r30-6-suburb: 43.2 / 100  layout 32.2  tone 19.8  colour 29.1  structure 69.9  detail 63.8  sky 22.7
REALISM r30-7-gravel: 31.8 / 100  layout 28.7  tone 14.8  colour 4.5  structure 54.1  detail 49.6  sky 9.4
REALISM r30-8-bronx-ny: 44.6 / 100  layout 23.6  tone 35.9  colour 27.0  structure 57.7  detail 82.4  sky 38.7
REALISM mean over 16 poses: 40.0 / 100
```
