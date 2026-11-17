# MINI Cooper S Hardtop 2-door (F56), US 2015, 6-speed manual

Scope: model year 2015. Engine, gearing and chassis are the same as 2014; the 2019 facelift changed only the automatic. This file covers the 6-speed manual. The only instrumented numbers found are from Car and Driver, through a Yahoo Autos reprint, and the reprint does not give the transmission. Car and Driver and MotorTrend block automated fetches, so read the original test sheets by hand before use.

Labels: **M** = measured (magazine instrumented test), **P** = published maker spec, **S** = secondary (dealer, catalogue, database, forum), **E** = estimate.

## Mass and dimensions
- Curb mass 2,785 lb = 1263 kg, 2015 Cooper S manual (S, [KBB 2015 specs](https://www.kbb.com/mini/hardtop-2-door/2015/specs)). EU DIN mass 1160 kg (P, [auto-data.net](https://www.auto-data.net/en/mini-hatch-f56-3-door-cooper-s-2.0-192hp-21555)). Autoblog lists 2,795 lb (S).
- Weight split 62/38 front (E). I found no instrumented figure. Forum and buyer-guide claims go from 63/37 to 68/32 and give no source ([MotoringFile guide](https://www.motoringfile.com/2026/06/16/the-ultimate-f56-mini-cooper-buyers-guide-2014-2024/)). An owner weighed an earlier MINI S at 62.7/37.3 ([NAM](https://www.northamericanmotoring.com/forums/drivetrain-cooper-s/27326-mini-weights.html)). To do: copy the "% front/rear" line from the Car and Driver test sheet.
- Wheelbase 2495 mm; length 3850, width 1727, height 1414 mm; track 1485/1485 mm on 16 in wheels; ground clearance 124 mm (P, MINI media information 03/2015, as quoted by auto-data.net and [Ultimatespecs](https://www.ultimatespecs.com/car-specs/Mini/67787/Mini-Mini-F56-Cooper-S.html)).
- CG height 0.53 m (E): 0.375 x the body height of 1.414 m, the ratio for small hatchbacks in the NHTSA inertial measurements (Heydinger et al., SAE 1999-01-1336).
- Inertia [1690, 1780, 540] kg m2 (E): the mass-normalised radii of gyration of nb2001 and crv25, scaled to 1263 kg, 3.85 m length and 1.727 m width.

## Engine B48A20A
- 1998 cm3, bore 82.0 mm, stroke 94.6 mm, CR 11.0:1, direct injection, twin-scroll turbo (P, auto-data.net).
- US rating: 189 hp (141 kW) at 4700-6000 rpm; 207 lb-ft (280 N m) at 1250-4750 rpm; overboost gives 221 lb-ft (300 N m) for a short time (P, BMW Group 2013 LA show release, as quoted by [Autoblog](https://www.autoblog.com/2013/09/29/2014-mini-cooper-engines-chassis/) and [MotoringFile](https://www.motoringfile.com/2013/09/29/mini-unveils-f56-new-engine-transmissions-we-go-hands-on/)). J.D. Power gives 4700-6200 rpm (S).
- Power check: 280 N m x 4750 rpm = 139 kW and 224 N m x 6000 rpm = 141 kW. Both match the rating.
- Curve (rpm: N m): 800:150, 1000:205, 1250:280, 2500:280, 4750:280, 5200:259, 5600:240, 6000:224, 6500:195. The flat part and the ends of the power plateau are P. The values below 1250 rpm (turbo spool) and above 6000 rpm (roll-off) are E.
- The curve leaves out overboost because the rated numbers fit a plateau-only curve. Add 300 N m only if the sim models a boost timer.
- Boost: about 1.0-1.2 bar gauge at peak (E; MINI does not publish it).
- Redline 6500 rpm on the tach, fuel cut about 6600 rpm (E, owner reports). Idle 750 rpm (E). Firing order 1-3-4-2 (BMW I4 standard, S).
- Engine braking (E, Heywood ch. 13, nb2001 method scaled to 2.0 L):
  - Friction: FMEP 1.9 bar gives 30 N m at 3000 rpm.
  - Pumping: PMEP 0.7 bar gives 11 N m at 3000 rpm.

## Driveline
- Gearbox: 6-speed manual GS6-59SG (TBFJ). Ratios 3.923 / 2.136 / 1.393 / 1.088 / 0.892 / 0.756, final drive 3.588, 6th gear overall 2.713 (S, [NAM forum table](https://www.northamericanmotoring.com/forums/jcw-garage/292260-f56-jcw-manual-trans-owners-wrong-ratios.html)). Reverse 3.55 (E).
- Cross-check: a rolling radius of 0.302 m gives 26.1 mph per 1000 rpm in 6th, so 75 mph is 2870 rpm. An owner reports about 2800 rpm at 75 mph ([NAM](https://www.northamericanmotoring.com/forums/f55-f56-hatch-talk-2014/275840-cruising-rpm.html)), so the ratios pass.
- mph per 1000 rpm, gears 1 to 6: 5.0 / 9.2 / 14.2 / 18.1 / 22.1 / 26.1. 2nd gear runs out at 60.0 mph at 6500 rpm, so the 0-60 time depends on the 2-3 shift.
- Differential: open. EDLC (Electronic Differential Lock Control) uses the DSC brakes to slow a spinning front wheel (P, BMW F56 release). The loader has no EDLC field, so use front_diff_bias 1.0 with traction control.
- Clutch: single dry plate with a dual-mass flywheel (S). Capacity 420 N m (E, 1.4 x overboost torque). Engine and flywheel inertia 0.15 kg m2 (E).

## Tyres, brakes, controls
- Tyres: US standard 195/55R16 87V run-flat on 6.5Jx16 (P/S). Option: 205/45R17 run-flat on 7Jx17.
- Placard 35/35 psi for 195/55R16 (S, [tirepressure.com](https://tirepressure.com/2015-mini-cooper-tire-pressure)). Other secondary sources give 32-34 psi. Read the B-pillar label on a real car.
- Brake sizes: front 294 x 22 mm vented, rear 259 x 10 mm solid, single-piston sliding calipers (S, [Brembo catalogue](https://www.bremboparts.com/america/en/catalogue/mini-mini-f56-cooper-s/000121964-1)).
- Brake torque at full pedal: 2200 N m front and 800 N m rear per wheel (E). That is a front share of 0.73.
- Ideal front share with the CG at 0.53 m: 0.74 at 0.5 g, 0.81 at 0.8 g, 0.85 at 1.0 g. So the hydraulic split alone puts too much on the rear above about 0.5 g.
  - The real car limits the rear with EBD inside ABS/DSC, so the sim must keep abs_enabled true.
  - For a test with ABS off, add brake_rear_knee.
- ABS, DSC (stability control) and DTC (traction mode) are standard, with EDLC and CBC (P, BMW press).
- Parking brake: a centre-console lever with cables to the rear calipers (P, owner manual). Torque 900 N m (E).

## Steering and suspension
- Steering: EPS with Servotronic, overall ratio 14.2:1 (P, MINI media information 03/2014, via Ultimatespecs). Lock to lock 2.4 turns (S, forum, unverified). Turning circle 10.8 m = 35.4 ft (P).
- Steering check: 2.4 x 180 / 14.2 = 30.4 deg mean road-wheel lock. A bicycle model with a 10.8 m circle and a 2.495 m wheelbase gives about 31 deg, so the numbers agree.
- Front: MacPherson strut with anti-roll bar. Rear: multilink with anti-roll bar (P).
- Bar diameters and spring rates are not published. The JSON uses wheel rates of 35.9 kN/m front and 26.1 kN/m rear (E, for 1.6 and 1.8 Hz ride frequency).
- Dynamic Damper Control was an option. The base car has passive dampers.
- Caster 7 deg, camber -0.5 deg front and -1.5 deg rear (E). Read the alignment sheet in the workshop manual.

## Aero
- Cd 0.31, frontal area 2.09 m2, CdA 0.65 m2 (P, MINI media information 03/2015).
- Top-speed check at 146 mph: drag takes 109 kW and rolling resistance 9 kW, 118 kW in total. The wheels get about 127 kW, so the published top speed is plausible.

## Targets
- Car and Driver, 2014 Cooper S Hardtop (M, Yahoo Autos reprint; transmission and tyres not shown):
  - 0-60 mph 6.6 s
  - 70-0 mph 172 ft
  - 0.91 g on the 300 ft skidpad
  - The same car ran 0-60 in 6.4 s at 40,000 miles.
- Maker: 0-60 6.5 s manual, 6.4 s automatic ([BMWBLOG](https://www.bmwblog.com/2014/02/03/2014-mini-cooper-bmwblog-first-drive-review/)). Top speed 146 mph (P). 0-100 km/h 6.8 s, EU (P).
- A 172 ft stop from 70 mph is a mean deceleration of 0.95 g with ABS.
- Not used: Edmunds measured 114 ft 60-0 and 0.86 g, but on the base Cooper ([Edmunds](https://www.edmunds.com/mini/cooper/2014/long-term-road-test/2014-mini-cooper-performance-testing.html)).

## Gauges and seat
- The F56 moved the speedometer from the centre ring to a cluster on the steering column (S, [Club Lexus review](https://www.clublexus.com/forums/car-chat/747580-review-2014-mini-cooper-s.html); 2014 owner manual, "Instrument cluster").
  - The cluster has an analogue speedometer in the centre and the tachometer as an arc on its left. The fuel gauge is an LED bar on the right.
  - The centre ring holds the infotainment screen and an LED ring.
- Seat: low hip point with a high, upright view (Edmunds). driver_eye is E until a model is fitted.

## Open items
- Weight split, the tyres on the Car and Driver test car, and whether that car was a manual: read the Car and Driver sheet by hand.
- Alignment specs and anti-roll bar diameters: read the workshop manual (BMW ISTA/TIS).
- ESC gradient 2.2 deg/g (E): replace it with the value from the 80 km/h step-steer bench, as done for crv25.
- Next phase: EDLC as a brake-based diff lock. This is a new feature, not a data fix.

```json
{
 "_comment": "2015 MINI Cooper S Hardtop 2-door (F56), US, 2.0 L B48 turbo four, 6-speed manual, front-wheel drive. Draft from lab/cars/mini-research.md; model fields (model_scene, rig, collision, mount) come from the fit.",
 "id": "mini15s",
 "name": "Cooper S",
 "make": "MINI",
 "body": "hatchback",
 "seats": 4,
 "mass_kg": 1263.0,
 "front_weight": 0.62,
 "cg_height_m": 0.53,
 "inertia": [1690, 1780, 540],
 "wheelbase_m": 2.495,
 "track_front_m": 1.485,
 "track_rear_m": 1.485,
 "tire_width_m": 0.195,
 "tire_aspect_ratio": 0.55,
 "rim_in": 16.0,
 "tyre_pressure_psi": [35.0, 35.0],
 "tyre_model": "tmeasy",
 "tmeasy_peak_mu_y": 1.0,
 "tmeasy_slide_mu_y": 0.83,
 "tmeasy_peak_mu_x": 1.0,
 "tmeasy_slide_mu_x": 0.8,
 "load_sensitivity": 0.1,
 "drag_area_m2": 0.65,
 "unsprung_corner_kg": 36.0,
 "wheel_rate_n_m": [35900, 26100],
 "bar_rate_n_m": [22000.0, 12000.0],
 "damper": ["ratio", "ratio"],
 "damper_ratio_rebound": [0.5, 0.5],
 "damper_ratio_compression": [0.3, 0.3],
 "static_camber_deg": [-0.5, -1.5],
 "caster_deg": 7.0,
 "toe_per_wheel_deg": 0.05,
 "steering_turns_lock_to_lock": 2.4,
 "parking_rack_deg": 34.0,
 "road_reference_deg": 31.0,
 "brake_front_nm": 2200.0,
 "brake_rear_nm": 800.0,
 "parking_brake_nm": 900.0,
 "parking_brake_type": "lever",
 "abs_enabled": true,
 "traction_control": true,
 "stability_control": true,
 "esc_understeer_deg_per_g": 2.2,
 "engine": "combustion",
 "aspiration": "turbo",
 "torque_curve": [[800, 150], [1000, 205], [1250, 280], [2500, 280], [4750, 280], [5200, 259], [5600, 240], [6000, 224], [6500, 195]],
 "torque_past_curve_nm": 170.0,
 "engine_friction_nm": 13.0,
 "engine_friction_per_rpm": 0.0057,
 "engine_pumping_nm": 8.5,
 "engine_pumping_per_rpm": 0.0009,
 "idle_rpm": 750.0,
 "fuel_cut_rpm": 6600.0,
 "max_gear_rpm": 6500.0,
 "engine_inertia": 0.15,
 "transmission": "manual",
 "gears": [3.923, 2.136, 1.393, 1.088, 0.892, 0.756],
 "reverse_ratio": 3.55,
 "final_drive": 3.588,
 "driveline_efficiency": 0.9,
 "clutch_capacity_nm": 420.0,
 "layout": "fwd",
 "front_diff_bias": 1.0,
 "target_0_60_s": 6.6,
 "target_70_0_ft": 172.0,
 "target_skidpad_g": 0.91,
 "sources": {
  "mass": "2,785 lb, 2015 Cooper S manual (KBB). Split 62/38 and CG 0.53 m are estimates.",
  "engine": "MINI US: 189 hp at 4700-6000 rpm, 207 lb-ft at 1250-4750 rpm; overboost 221 lb-ft not modelled. Curve outside the plateau is an estimate.",
  "gearing": "GS6-59SG ratios and 3.588 final drive from the NAM forum (secondary); 6th checked against an owner report of 2800 rpm at 75 mph. Reverse is an estimate.",
  "brakes": "294 mm vented front, 259 mm solid rear (Brembo catalogue). Torques are estimates. Rear share 0.27 exceeds the ideal above 0.5 g; EBD in ABS/DSC holds the rear, as on the real car.",
  "diff": "Open differential with brake-based EDLC (MINI). EDLC is not modelled; traction control stands in.",
  "steering": "14.2:1 Servotronic EPS, 10.8 m circle (MINI media info); 2.4 turns from the forum (unverified).",
  "aero": "Cd 0.31 x 2.09 m2 = 0.65 m2 (MINI media information 03/2015).",
  "tyre_pressure": "35/35 psi for 195/55R16 run-flats (tirepressure.com, secondary).",
  "calibration": "Car and Driver 2014 Cooper S Hardtop: 6.6 s, 172 ft, 0.91 g (Yahoo reprint; transmission not confirmed). Grip values are starting points for calibration.",
  "estimates": "Weight split, CG, inertia, springs, bars, dampers, alignment, brake torques, clutch, ESC gradient, tyre curve."
 }
}
```
