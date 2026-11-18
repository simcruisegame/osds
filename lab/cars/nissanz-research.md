# Nissan Z Performance (RZ34), US 2023, 6-speed manual

Scope: 2023 Z Performance with the 6-speed manual, RAYS 19 in forged wheels, Bridgestone Potenza S007, mechanical LSD and the larger brakes. The main source is the Nissan USA "2023 Nissan Z Specifications" release (May 2022), here read from a [ZCCA copy](https://zcca.org/wp-content/uploads/2025/10/Specifications-2023-Nissan-Z.pdf). It gives axle weights, ratios and brake sizes, so most values are P.

Labels: **M** = measured (magazine instrumented test), **P** = published maker spec, **S** = secondary (dealer, catalogue, database, forum), **E** = estimate.

## Mass and dimensions
- Curb mass 3,536 lb = 1604 kg: 1,995 lb front, 1,541 lb rear, so 56.4/43.6 (P, Nissan spec, Performance MT).
- Wheelbase 100.4 in = 2550 mm; track 61.2 in front, 61.6 in rear = 1554/1565 mm (Performance; Sport is wider at 61.6/62.8 in); length 4379, width 1844, height 1316 mm (P).
- CG height 0.50 m (E): 0.38 x roof height, front-mid V6 on a 370Z-derived platform.
- Inertia [2780, 2870, 780] kg m2 (E): the mini15s mass-normalised radii of gyration scaled to 1604 kg, 4.379 m and 1.844 m.

## Engine VR30DDTT (3.0 L twin-turbo V6)
- 2997 cm3, bore and stroke 86.0 x 86.0 mm, CR 10.3:1, direct injection, maximum boost 15 psi (P).
- 400 hp at 6400 rpm, 350 lb-ft at 1600-5200 rpm, maximum engine speed 6800 rpm, premium fuel (P). The rating needs 93-octane fuel (Car and Driver).
- Power check: 475 N m x 5200 rpm = 259 kW; 445 N m x 6400 rpm = 298 kW = 400 hp. Both match.
- Curve (rpm: N m): 1000:280, 1300:380, 1600:475, 5200:475, 5800:465, 6400:445, 6800:405. Plateau and the 6400 rpm point are P; spool and roll-off are E. No point exceeds 298 kW.
- Fuel cut 6800 rpm (P, maximum engine speed). Idle 650 rpm (E).
- Engine braking (E, mini15s method scaled to 3.0 L): 45 N m friction and 17 N m pumping at 3000 rpm.

## Driveline
- Ratios 3.795 / 2.325 / 1.625 / 1.272 / 1.000 / 0.795, reverse 3.446, final drive 3.538 (P).
- mph per 1000 rpm on the rear 275/35R19 (rolling radius 0.327 m, E): 5.7 / 9.3 / 13.3 / 17.0 / 21.7 / 27.3. 2nd runs out at 63 mph at 6800 rpm, so 0-60 needs one shift.
- Clutch: EXEDY high-performance clutch (P). Capacity 665 N m (E, 1.4 x peak torque). Engine and flywheel inertia 0.17 kg m2 (E). Carbon-fibre driveshaft on the manual (P).
- SynchroRev Match (automatic throttle blip on downshifts) is standard on the Performance manual (P). The sim has no rev-match model; note it for the driver aids list.
- Differential: mechanical clutch-type limited slip, Performance only (P). Ramp angles and preload are not published. Bias 2.5 drive, 1.8 coast (E).

## Tyres, brakes, controls
- Tyres: Bridgestone Potenza S007 255/40R19 on 19x9.5 front, 275/35R19 on 19x10 rear (P). The loader takes one size; use the rear size and axle_grip [0.97, 1.0] (E).
- Placard 32 psi front, 29 psi rear (S, [tirepressure.com](https://tirepressure.com/2023-nissan-z-tire-pressure), BFGoodrich fitment table). Low but consistent across sources; read the B-pillar label.
- Brakes: 14.0 x 1.26 in (356 x 32 mm) vented front with floating four-piston aluminium calipers; 13.8 x 0.79 in (351 x 20 mm) vented rear with two-piston calipers; vacuum assist (P).
- Brake torque at full pedal: 2700 N m front, 750 N m rear per wheel (E). Front share 0.78 (front rolling radius 0.333 m, rear 0.327 m).
- Ideal front share (h 0.50 m, L 2.55 m, 56.4 % front): 0.66 at 0.5 g, 0.72 at 0.8 g, 0.76 at 1.0 g. The fronts lock first up to the tyre limit.
- 4-channel ABS, Brake Assist, EBD, VDC (stability control) and TCS standard (P).
- Parking brake: "mechanical, pull-type" lever (P). Torque 1100 N m (E).

## Steering and suspension
- Electric rack, fixed ratio 15.0:1, speed-variable assist (P). Turning circle 11.0 m kerb to kerb (P, [Nissan Australia Z spec sheet](https://www.nissan.com.au/content/dam/Nissan/AU/Files/Brochures/Models/Specsheet/Nissan%20Z%20Spec%20Sheet%20Final.pdf)). Lock-to-lock is not published.
- Lock check: an 11.0 m circle with a 2.55 m wheelbase gives 27.6 deg outer and 37.5 deg inner, mean 32.6 deg. At 15.0:1 that is 2 x 32.6 x 15.0 / 360 = 2.7 turns (E).
- Front double wishbone (aluminium), rear multi-link, monotube dampers, anti-roll bars both ends, sport-tuned on the Performance (P). Rates not published. Wheel rates 41.5 kN/m front and 37.2 kN/m rear (E, for 1.6 and 1.75 Hz).
- Caster 7 deg, camber -0.5 deg front and -1.3 deg rear (E).

## Aero
- Cd 0.31 (P, Nissan Australia). Frontal area 2.05 m2 (E). CdA 0.64 m2 (E).

## Targets
- Car and Driver, "Tested (Twice): 2023 Nissan Z Performance Doesn't Reach Far Enough" ([Yahoo reprint](https://autos.yahoo.com/2023-nissan-z-first-drive-040100385.html), S):
  - 0-60 mph 4.5 s, quarter mile 13.0 s at 111 mph, manual, 1 ft rollout, 91-octane fuel, pre-production car (M; the manual is confirmed by a [KSL summary](https://www.ksl.com/article/50566072/2023-nissan-z-performance-test-drive-review), S).
  - A second car on 93 octane: 4.1 s and 12.6 s at 115 mph (M; gearbox not confirmed).
  - 70-0 mph 166 ft and 0.93 g on the 300 ft skidpad (M). Car and Driver blames the S007 tyres for both.
- Calibrate to 4.5 s, but expect the sim on rated torque to run 4.1-4.5 s. Do not cut torque to hit 4.5 s.
- Other: MotorTrend 4.3 s (S, transmission not confirmed).

## Open items
- Confirm the gearbox of the braking and skidpad car on the Car and Driver sheet.
- LSD type detail (1-way, 1.5-way or 2-way) and preload from the Nissan service manual.
- Spring, bar and alignment data from the service manual.
- Next phase: SynchroRev Match as a driver aid (new feature).

```json
{
 "_comment": "2023 Nissan Z Performance (RZ34, US), 3.0 L VR30DDTT twin-turbo V6, 6-speed manual, rear-wheel drive, mechanical clutch LSD. Draft from lab/cars/nissanz-research.md; model fields (model_scene, rig, collision, mount) come from the fit.",
 "id": "nissanz",
 "name": "Z Performance",
 "make": "Nissan",
 "body": "coupe",
 "seats": 2,
 "mass_kg": 1604.0,
 "front_weight": 0.564,
 "cg_height_m": 0.50,
 "inertia": [2780, 2870, 780],
 "wheelbase_m": 2.55,
 "track_front_m": 1.554,
 "track_rear_m": 1.565,
 "tire_width_m": 0.275,
 "tire_aspect_ratio": 0.35,
 "rim_in": 19.0,
 "axle_grip": [0.97, 1.0],
 "tyre_pressure_psi": [32.0, 29.0],
 "tyre_model": "tmeasy",
 "tmeasy_peak_mu_y": 0.98,
 "tmeasy_slide_mu_y": 0.82,
 "tmeasy_peak_mu_x": 1.0,
 "tmeasy_slide_mu_x": 0.82,
 "load_sensitivity": 0.1,
 "drag_area_m2": 0.64,
 "unsprung_corner_kg": 44.0,
 "wheel_rate_n_m": [41500, 37200],
 "bar_rate_n_m": [24000.0, 14000.0],
 "damper": ["ratio", "ratio"],
 "damper_ratio_rebound": [0.5, 0.5],
 "damper_ratio_compression": [0.3, 0.3],
 "static_camber_deg": [-0.5, -1.3],
 "caster_deg": 7.0,
 "toe_per_wheel_deg": 0.05,
 "steering_turns_lock_to_lock": 2.7,
 "parking_rack_deg": 37.0,
 "road_reference_deg": 32.5,
 "brake_front_nm": 2700.0,
 "brake_rear_nm": 750.0,
 "parking_brake_nm": 1100.0,
 "parking_brake_type": "lever",
 "abs_enabled": true,
 "traction_control": true,
 "stability_control": true,
 "esc_understeer_deg_per_g": 2.0,
 "engine": "combustion",
 "aspiration": "turbo",
 "torque_curve": [[1000, 280], [1300, 380], [1600, 475], [5200, 475], [5800, 465], [6400, 445], [6800, 405]],
 "torque_past_curve_nm": 330.0,
 "engine_friction_nm": 19.5,
 "engine_friction_per_rpm": 0.0086,
 "engine_pumping_nm": 12.8,
 "engine_pumping_per_rpm": 0.0014,
 "idle_rpm": 650.0,
 "fuel_cut_rpm": 6800.0,
 "max_gear_rpm": 6700.0,
 "engine_inertia": 0.17,
 "transmission": "manual",
 "gears": [3.795, 2.325, 1.625, 1.272, 1.0, 0.795],
 "reverse_ratio": 3.446,
 "final_drive": 3.538,
 "driveline_efficiency": 0.87,
 "clutch_capacity_nm": 665.0,
 "layout": "rwd",
 "rear_diff_bias": 2.5,
 "rear_diff_coast_bias": 1.8,
 "target_0_60_s": 4.5,
 "target_0_60_rollout": true,
 "target_70_0_ft": 166.0,
 "target_skidpad_g": 0.93,
 "sources": {
  "mass": "3,536 lb, 1,995 lb front / 1,541 lb rear, Performance MT (Nissan 2023 Z specifications). CG 0.50 m is an estimate.",
  "engine": "Nissan: 400 hp at 6400 rpm, 350 lb-ft at 1600-5200 rpm, 6800 rpm maximum, 15 psi boost. Curve outside the plateau is an estimate.",
  "gearing": "3.795/2.325/1.625/1.272/1.000/0.795, reverse 3.446, final drive 3.538 (Nissan).",
  "brakes": "356 x 32 mm 4-piston front, 351 x 20 mm 2-piston rear, ABS with EBD (Nissan). Torques are estimates; front share 0.78 stays above the ideal up to 1.0 g.",
  "diff": "Mechanical clutch-type LSD, Performance only (Nissan). Bias 2.5/1.8 are estimates.",
  "steering": "Fixed 15.0:1 (Nissan US); 11.0 m kerb circle (Nissan Australia). 2.7 turns is an estimate from the lock check.",
  "aero": "Cd 0.31 (Nissan Australia) x 2.05 m2 (estimate).",
  "tyre_pressure": "32/29 psi for 255/40R19 and 275/35R19 (tirepressure.com, BFGoodrich; secondary). Staggered tyres: rear size in the file, axle_grip for the front.",
  "calibration": "Car and Driver Tested (Twice): manual 4.5 s on 91 octane (93 octane car 4.1 s), 166 ft 70-0, 0.93 g (Yahoo reprint).",
  "estimates": "CG, inertia, springs, bars, dampers, alignment, brake torques, clutch, diff bias, frontal area, ESC gradient, tyre curve, axle_grip.",
  "research": "lab/cars/nissanz-research.md"
 }
}
```
