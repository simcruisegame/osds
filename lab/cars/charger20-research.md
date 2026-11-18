# Dodge Charger R/T (LD), US 2020, 5.7 HEMI V8, 8-speed automatic

Labels: **M** = measured (magazine instrumented test), **P** = published maker spec, **S** = secondary (dealer, catalogue, database, forum), **E** = estimate (method given).

## Body and trim chosen, and why
- The [CARLA catalogue](https://carla.readthedocs.io/en/latest/catalogue_vehicles/) render of `vehicle.dodge.charger_2020` shows the 2015-2023 LD facelift sedan on narrow body sides. It has a plain hood without a scoop, a single lower grille and silver split-spoke wheels of about 20 in. The asset has opening doors and lights.
- That body rules out the Scat Pack, the Daytona and the SRT (functional hood scoop or widebody). It fits the SXT, GT and R/T. I chose the **R/T**, the only V8 on the plain-hood body.
- Note: the skeleton `D:\sc\carla-content\Static\Car\4Wheeled\DodgeCharger\Charger\Skel_DodgeCharger.uasset` points to a mesh named `SM_DodgeCharger2024`. The render is still the LD body, not the 2024 Daytona EV. To do: check the rear of the mesh for an R/T badge and twin exhaust tips.

## Mass and dimensions
- Curb weight 4,286 lb = 1944 kg (S, [auto-data.net](https://www.auto-data.net/en/dodge-charger-vii-ld-facelift-2019-r-t-5.7-hemi-v8-370hp-automatic-38969); KBB gives 4,286 lb). The C&D test car weighed 4,287 lb (M).
- Weight split 52/48 (E). I found no maker or magazine figure.
- Wheelbase 120.0 in = 3.048 m; length 5100, width 1905, height 1467 mm; track 1611/1620 mm (S, auto-data.net).
- CG height 0.55 m (E: 0.375 x the 1.467 m roof height, the NHTSA sedan ratio).
- Inertia [3100, 3840, 1075] kg m2 (E: camaro87 scaled by mass and body box).

## Engine (5.7 HEMI with MDS and VVT, 5654 cm3, 99.5 x 90.9 mm, CR 10.5:1)
- 370 hp at 5250 rpm, 395 lb-ft (536 N m) at 4200 rpm (P, Dodge rating, as quoted by C&D and auto-data.net).
- Power check: 502 N m x 5250 rpm = 276 kW = 370 hp.
- Curve (rpm: N m): 800:400, 1500:450, 2000:475, 2500:490, 3000:505, 3500:520, 4200:536, 4800:525, 5250:502, 5600:470, 5900:430. The 4200 and 5250 points are P. The others are E, and none is above 370 hp.
- Redline 5800 rpm, fuel cut 5900 rpm, idle 600 rpm (E). Firing order 1-8-4-3-6-5-7-2 (S). MDS (cylinder deactivation) is not modelled.
- Engine braking (E, Heywood ch. 13): FMEP 1.7 bar gives 76 N m at 3000 rpm; PMEP 0.8 bar gives 36 N m.

## Driveline
- ZF 8HP70 eight-speed automatic: 4.71 / 3.14 / 2.10 / 1.67 / 1.29 / 1.00 / 0.84 / 0.67, reverse 3.32 (S, [TorqueNews](https://www.torquenews.com/106/2020-dodge-charger-four-transmissions-and-gear-ratios-detailed)).
- Converter torque ratio 1.9 at stall (E). The converter locks up from 2nd gear.
- Final drive 2.62, standard on 5.7 models (S, TorqueNews).
- Differential: open (S). A Mopar 3.09 limited-slip kit is sold to replace the 2.62 "geared differential" of the 5.7 car ([High Horse Performance listing](https://highhorseperformance.com/mopar-3-09-zf-lsd-upgrade-kit-2015-2018-5-7-hemi-charger-8-speed-automatic-mprsrt15cr)). Read the axle tag on a real car to confirm.
- Gearing check: a 245/45R20 is 729 mm in diameter, with a rolling radius of about 0.356 m. mph per 1000 rpm: 6.8 in 1st, 10.1 in 2nd and 47.5 in 8th. That is about 1470 rpm at 70 mph in 8th. 2nd gear runs out at 59 mph at 5800 rpm.

## Tyres, brakes, controls
- Tyres: 245/45ZR20 all-season on 20 x 8 in wheels (S, auto-data.net). Placard 32/32 psi (S, [tirepressure.com](https://tirepressure.com/2020-dodge-charger-tire-pressure)).
- Brakes: 345 x 28 mm vented front discs, 320 x 22 mm vented rear discs (S, auto-data.net). Single-piston sliding calipers (E).
- Brake torque per wheel 3400 N m front and 1400 N m rear (E). That is a front share of 0.71.
- The ideal front share is 0.52 + 0.180 x decel: 0.61 at 0.5 g, 0.66 at 0.8 g, 0.70 at 1.0 g. So the fronts lock first. EBD in the ABS also limits the rear.
- ABS, ESC (with a Sport mode) and traction control are standard (S).
- Parking brake: electric, with a switch on the centre console (S, 2015+ owner manual). With `parking_brake_type` "electric", the sim brakes through the ABS when the switch is held above 2 m/s. Torque 1500 N m (E).

## Steering and suspension
- Electric power steering, rack and pinion (S, auto-data.net). Turning circle 11.5 m (S, auto-data.net).
- Ratio 15.7:1 and 2.9 turns lock to lock (E). A bicycle model with an 11.5 m circle and a 3.048 m wheelbase gives a mean lock of about 33 deg, and 2 x 33 x 15.7 / 360 = 2.9. Count the turns on a car.
- Front: short-long arm with a high upper arm. Rear: five-link independent. Anti-roll bars at both ends (S).
- I did not find spring rates or bar sizes. The wheel rates of 32.6 kN/m front and 36.8 kN/m rear come from 1.35 Hz and 1.5 Hz ride frequencies (E, a soft sedan ride). Caster 7 deg (E).

## Aero
- Cd 0.335 (S, auto-data.net) x frontal area 2.40 m2 (E: 0.86 x width x height) = CdA 0.80 m2. The top speed is governed, so it gives no check.

## Target: closest instrumented test
No instrumented test of a plain 2015-2020 R/T was found. The closest is:
- Car and Driver, "2017 Dodge Charger Daytona 5.7L V-8", August 2017 (M, [Yahoo reprint](https://ca.finance.yahoo.com/news/2017-dodge-charger-daytona-5-224500506.html)):
  - 0-60 mph 5.1 s; 1/4 mile 13.7 s at 105 mph
  - 70-0 mph 171 ft, "with slight fade"
  - 0.87 g on the 300 ft skidpad
  - 4,287 lb, on Goodyear Eagle RS-A2 all-season tyres
- The Daytona has the R/T engine, gearbox and weight. It adds a cold-air intake, a hood scoop and body parts. The test does not give its axle ratio; other sources say 3.07 (unverified).
- The stop and the skidpad transfer to the R/T. The 0-60 time probably does not: a 2.62 axle gives 15 % less torque at the wheels than a 3.07.
- A 171 ft stop from 70 mph is a mean deceleration of 0.96 g.

## In-car
- Analogue speedometer and tachometer with a 7 in centre display, and a Uconnect screen in the dash (S). The driver eye point is E.

## Open items
- A test of a plain R/T with the 2.62 axle (Motor Trend or Car and Driver).
- Weight split, steering ratio and turns.
- Spring and bar data: read the FCA service manual.
- Next phase: MDS cylinder deactivation (sound and economy only).

```json
{
 "_comment": "2020 Dodge Charger R/T (LD), US, 5.7 HEMI V8, ZF 8HP70 8-speed automatic, 2.62 open axle, 245/45ZR20 all-season. Matches the CARLA vehicle.dodge.charger_2020 body (plain hood). Draft from lab/cars/charger20-research.md; model fields come from the fit.",
 "id": "charger20",
 "name": "Charger R/T",
 "make": "Dodge",
 "body": "sedan",
 "seats": 5,
 "mass_kg": 1944.0,
 "front_weight": 0.52,
 "cg_height_m": 0.55,
 "inertia": [3100, 3840, 1075],
 "wheelbase_m": 3.048,
 "track_front_m": 1.611,
 "track_rear_m": 1.620,
 "tire_width_m": 0.245,
 "tire_aspect_ratio": 0.45,
 "rim_in": 20.0,
 "tyre_pressure_psi": [32.0, 32.0],
 "tyre_model": "tmeasy",
 "tmeasy_peak_mu_y": 0.95,
 "tmeasy_slide_mu_y": 0.80,
 "tmeasy_peak_mu_x": 0.95,
 "tmeasy_slide_mu_x": 0.78,
 "load_sensitivity": 0.1,
 "drag_area_m2": 0.80,
 "unsprung_corner_kg": 52.0,
 "wheel_rate_n_m": [32600, 36800],
 "bar_rate_n_m": [25000.0, 12000.0],
 "damper": ["ratio", "ratio"],
 "damper_ratio_rebound": [0.45, 0.45],
 "damper_ratio_compression": [0.28, 0.28],
 "static_camber_deg": [-0.5, -0.8],
 "caster_deg": 7.0,
 "toe_per_wheel_deg": 0.05,
 "steering_turns_lock_to_lock": 2.9,
 "parking_rack_deg": 36.0,
 "road_reference_deg": 33.0,
 "brake_front_nm": 3400.0,
 "brake_rear_nm": 1400.0,
 "parking_brake_nm": 1500.0,
 "parking_brake_type": "electric",
 "abs_enabled": true,
 "traction_control": true,
 "stability_control": true,
 "esc_understeer_deg_per_g": 2.8,
 "engine": "combustion",
 "torque_curve": [[800, 400], [1500, 450], [2000, 475], [2500, 490], [3000, 505], [3500, 520], [4200, 536], [4800, 525], [5250, 502], [5600, 470], [5900, 430]],
 "torque_past_curve_nm": 350.0,
 "engine_friction_nm": 34.0,
 "engine_friction_per_rpm": 0.014,
 "engine_pumping_nm": 31.0,
 "engine_pumping_per_rpm": 0.0017,
 "idle_rpm": 600.0,
 "fuel_cut_rpm": 5900.0,
 "max_gear_rpm": 5800.0,
 "engine_inertia": 0.30,
 "transmission": "automatic",
 "gears": [4.71, 3.14, 2.10, 1.67, 1.29, 1.00, 0.84, 0.67],
 "reverse_ratio": 3.32,
 "final_drive": 2.62,
 "driveline_efficiency": 0.85,
 "shift_seconds": 0.2,
 "upshift_rpm": 5700,
 "downshift_rpm": 1800,
 "clutch_capacity_nm": 800,
 "clutch_back_nm": 750,
 "launch_rpm": 1000,
 "launch_rpm_per_throttle": 900,
 "converter_stall_ratio": 1.9,
 "converter_coupling_ratio": 0.87,
 "layout": "rwd",
 "rear_diff_bias": 1.0,
 "rear_diff_coast_bias": 1.0,
 "target_0_60_s": 5.1,
 "target_70_0_ft": 171.0,
 "target_skidpad_g": 0.87,
 "sources": {
  "mass": "4,286 lb (auto-data.net, KBB); C&D Daytona 5.7 test car 4,287 lb. Split 52/48 and CG 0.55 m are estimates.",
  "engine": "Dodge: 370 hp at 5250 rpm, 395 lb-ft at 4200 rpm. The curve between the two points is an estimate. MDS not modelled.",
  "gearing": "8HP70 4.71/3.14/2.10/1.67/1.29/1.00/0.84/0.67, reverse 3.32; 2.62 axle standard on 5.7 (TorqueNews, secondary). Converter ratio is an estimate.",
  "brakes": "345 mm vented front, 320 mm vented rear (auto-data, secondary). Torques are estimates; front share 0.71 is above the ideal up to 1.0 g.",
  "diff": "Open 2.62 differential (secondary: the Mopar 3.09 LSD kit replaces it). Confirm on the axle tag.",
  "steering": "EPS, 11.5 m circle (auto-data). Ratio 15.7 and 2.9 turns are estimates.",
  "aero": "Cd 0.335 (auto-data) x 2.40 m2 (estimate).",
  "tyre_pressure": "32/32 psi for 245/45R20 (tirepressure.com, secondary).",
  "calibration": "Car and Driver, 2017 Charger Daytona 5.7, Aug 2017: 5.1 s, 13.7 s at 105 mph, 171 ft, 0.87 g. Stop and skidpad are firm targets; 0-60 is soft (Daytona axle probably 3.07).",
  "estimates": "Split, CG, inertia, springs, bars, dampers, alignment, brake torques, converter, steering ratio and turns, ESC gradient, frontal area, tyre curve.",
  "research": "lab/cars/charger20-research.md"
 },
 "calibration_open": "The 0-60 target comes from a Daytona (axle probably 3.07); a plain 2.62 R/T should be a few tenths slower. Replace it with a plain R/T test when one is found."
}
```
