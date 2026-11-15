# LANDFIRE existing vegetation type names (LF 2022, CONUS)

`evt-lf2022-conus-names.json` maps each LANDFIRE 2022 EVT code (the pixel value of the public
image service `Landfire_LF2022/LF2022_EVT_CONUS/ImageServer`, edcintl.cr.usgs.gov and lfps.usgs.gov)
to its vegetation type name, for the code seen anywhere in the lower 48 states at about 1.2 km a pixel:
798 of the 830 classes (rare classes seen nowhere at that size have no entry; the trees fall back to
the climate rule for them).

**How it was made** (2026-11-15, three requests, no bulk download): the image services publish no
attribute table or colour map, so one raw export (codes) and one rendered export (colours) of the
same 4000 x 2600 grid were paired, and the legend (its labels in code order, with their colours)
was aligned to the codes in order by colour, forward and backward; only codes where both agree are
kept, and every code with a colour of its own matched its label.

**Source and licence**: LANDFIRE (U.S. Department of the Interior, U.S. Geological Survey, and the
U.S. Department of Agriculture Forest Service), LANDFIRE 2022 Existing Vegetation Type. US public
domain. Credit: "LANDFIRE, 2022 Existing Vegetation Type (EVT), U.S. Department of the Interior,
Geological Survey, and U.S. Department of Agriculture."
