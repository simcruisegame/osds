# Ford Mustang GT fastback (S550), US 2016, 5.0 V8, 6-speed manual, GT Performance Package

Labels: **M** = measured (magazine instrumented test), **P** = published maker spec, **S** = secondary (dealer, catalogue, database, forum), **E** = estimate (method given).

## Stop: the CARLA body is not this car
- The [CARLA catalogue](https://carla.readthedocs.io/en/latest/catalogue_vehicles/) render of `vehicle.ford.mustang` shows a 1965-66 notchback hardtop, not an S550. Its "Generation: 1" is the CARLA asset generation.
- `D:\sc\carla-content\Static\Car\4Wheeled\Mustang\SK_Mustang.uasset` keeps the FBX names `FordMustangGt1966_details` and `FordMustangGt1966_windows1`.
- Do not fit this data to that mesh. Find a 2016 model, or use the CARLA body as a hardtop form of `mustang65` with 1966 GT data (A-code 289, 225 hp).

## Trim chosen, and why
2016 GT Premium fastback, 6-speed manual, GT Performance Package (PP). The only instrumented test found (Car and Driver) is of this car. The PP is manual-only. It adds Brembo 6-piston front calipers, a 3.73 TORSEN axle, 255/40R19 front and 275/40R19 rear summer tyres, heavy-duty front springs, a larger rear bar, and its own EPAS, ABS and ESC tuning (P, [Ford 2016 Mustang brochure, dealer copy](https://pictures.dealer.com/greenwayford/635f6a0e0a0e0acc2af6861838ce10c2.pdf)).

## Mass and dimensions
- Test weight 3,782 lb = 1715 kg (M, C&D test car with PP, Recaro seats and Shaker audio). Catalogue curb weight 3,705 lb = 1681 kg (S, [auto-data.net](https://auto-data.net/en/ford-mustang-vi-gt-5.0-v8-426hp-31362)).
- Weight split 53/47 (E). No Ford or C&D figure found.
- Wheelbase 107.1 in = 2.720 m; length 188.3 in; height 54.4 in (P, Ford brochure). Width 1915 mm, track 1582/1648 mm (S, auto-data.net, 18 in wheels).
- CG height 0.50 m (E: 0.36 x roof height 1.382 m).
- Inertia [2410, 3030, 915] kg m2 (E: camaro87 scaled by mass and body box).

## Engine (Coyote Gen 2, 4951 cm3, 92.2 x 92.7 mm, CR 11.0:1)
- 435 hp at 6500 rpm and 400 lb-ft (542 N m) at 4250 rpm, on 93-octane fuel (P, Ford brochure). Bore, stroke and CR: S (auto-data.net).
- Power check: 477 N m x 6500 rpm = 324 kW = 435 hp.
- Curve (rpm: N m): 1000:380, 1500:420, 2000:450, 2500:475, 3000:500, 3500:525, 4250:542, 5000:530, 5500:515, 6000:497, 6500:477, 7000:440. The 4250 and 6500 points are P. The other points are E, and none is above 435 hp.
- Redline 7000 rpm, fuel cut 7000 rpm and idle 650 rpm (E, owner reports). Firing order 1-5-4-8-6-3-7-2 (Ford modular V8, S).
- Engine braking (E, Heywood ch. 13):
  - Friction: FMEP 1.7 bar gives 67 N m at 3000 rpm (JSON: 31 N m + 0.012 N m per rpm).
  - Pumping: PMEP 0.8 bar gives 32 N m.

## Driveline
- MT82 6-speed: 3.66 / 2.43 / 1.69 / 1.32 / 1.00 / 0.65 (S, [Stangnet table](https://stangnet.com/mustang-forums/threads/rear-gear-ratios.891632/), [CJ Pony Parts](https://www.cjponyparts.com/resources/mt82-transmission-specs)). Reverse 3.53 (E).
- Axle: 3.73 with a TORSEN helical-gear differential, PP only (P, Ford brochure). The GT without the PP has a 3.55 axle with a clutch-type limited slip. Bias 2.5 drive and 2.5 coast (E, a typical Torsen T-2 torque bias).
- Gearing check: the 275/40R19 rear is 703 mm in diameter, with a rolling radius of about 0.343 m. mph per 1000 rpm, gears 1 to 6: 5.9 / 8.9 / 12.8 / 16.3 / 21.6 / 33.2. 2nd gear runs out at 62 mph at 7000 rpm, so the 0-60 run needs one shift.
- Clutch: single plate with a dual-mass flywheel (S). Capacity 720 N m (E, 1.33 x peak torque). Engine inertia 0.25 kg m2 (E).

## Tyres, brakes, controls
- Tyres: 255/40R19 on 9 in front rims, 275/40R19 on 9.5 in rear rims, summer only (P).
  - The loader takes one size, so the JSON uses 265/40R19 (radius 1 % under the driven rear).
- Placard 32/32 psi (S, [tirepressure.com](https://tirepressure.com/2016-ford-mustang-tire-pressure)).
- Brakes (PP): 380 mm vented front discs with Brembo 6-piston fixed calipers, 330 mm vented rear discs with single-piston calipers (S, [autoevolution](https://www.autoevolution.com/news/2015-ford-mustang-performance-packages-detailed-84082.html); the caliper type is P).
- Brake torque per wheel 3400 N m front and 1300 N m rear (E). That is a front share of 0.72.
- The ideal front share is 0.53 + 0.184 x decel: 0.62 at 0.5 g, 0.68 at 0.8 g, 0.71 at 1.0 g. So the fronts lock first up to 1.0 g. ABS with EBD is fitted.
- ABS, AdvanceTrac ESC and traction control are standard (P).
- Parking brake: a hand lever on the centre console, acting on the rear calipers (P, the brochure lists a "parking brake handle"). Torque 1200 N m (E).

## Steering and suspension
- EPAS with Normal, Sport and Comfort effort. Ratio 16.0:1 (S, [Mustang6G forum](https://www.mustang6g.com/forums/threads/6g-sponsors-please-build-quicker-steering-ratio-response-for-s550.97969/)). Turning circle 11.52 m (S, auto-data.net).
- Lock to lock 2.7 turns (E). A bicycle model with an 11.52 m circle and a 2.72 m wheelbase gives a mean lock of about 30 deg, and 2 x 30 x 16 / 360 = 2.7.
- Front: double-ball-joint MacPherson strut. Rear: integral-link independent (P).
- I did not find spring rates or bar sizes. The wheel rates of 36.4 kN/m front and 38.5 kN/m rear come from 1.5 Hz and 1.65 Hz ride frequencies (E). Caster 7 deg (E).

## Aero
- Ford published no Cd. Catalogues give 0.32 (auto-data.net) and 0.371 (MotorMatchup), both S without a source. The file uses 0.35 x 2.25 m2 (E: 0.85 x width x height) = CdA 0.79 m2 (E). The governed top speed gives no check.

## Targets
- Car and Driver, 2016 Ford Mustang GT manual with PP (M, [Yahoo reprint of the C&D 2017 Mustang review](https://www.yahoo.com/news/2017-ford-mustang-190000351.html)):
  - 0-60 mph 4.3 s (C&D uses a 1 ft rollout)
  - 1/4 mile 12.9 s at 112 mph
  - 70-0 mph 156 ft
  - 0.94 g on the 300 ft skidpad
- A 156 ft stop from 70 mph is a mean deceleration of 1.05 g.

## In-car
- Analogue speedometer and tachometer with a centre LCD; the PP adds oil-pressure and vacuum gauges (P).

## Open items
- The CARLA body mismatch. Weight split. Tyre size per axle in the loader.
- Spring and bar data (Ford workshop manual), lock to lock (count on a car), Torsen bias.

```json
{
 "_comment": "2016 Ford Mustang GT fastback (S550), US, 5.0 Coyote V8, MT82 6-speed manual, GT Performance Package (3.73 Torsen, Brembo 6-piston, 255/275-40R19 summer). Draft from lab/cars/mustang16-research.md. Not for the CARLA vehicle.ford.mustang mesh, which is a 1966 Mustang GT.",
 "id": "mustang16",
 "name": "Mustang GT",
 "make": "Ford",
 "body": "coupe",
 "seats": 4,
 "mass_kg": 1715.0,
 "front_weight": 0.53,
 "cg_height_m": 0.50,
 "inertia": [2410, 3030, 915],
 "wheelbase_m": 2.720,
 "track_front_m": 1.582,
 "track_rear_m": 1.648,
 "tire_width_m": 0.265,
 "tire_aspect_ratio": 0.40,
 "rim_in": 19.0,
 "tyre_pressure_psi": [32.0, 32.0],
 "tyre_model": "tmeasy",
 "tmeasy_peak_mu_y": 1.05,
 "tmeasy_slide_mu_y": 0.88,
 "tmeasy_peak_mu_x": 1.05,
 "tmeasy_slide_mu_x": 0.85,
 "load_sensitivity": 0.1,
 "drag_area_m2": 0.79,
 "unsprung_corner_kg": 45.0,
 "wheel_rate_n_m": [36400, 38500],
 "bar_rate_n_m": [30000.0, 18000.0],
 "damper": ["ratio", "ratio"],
 "damper_ratio_rebound": [0.5, 0.5],
 "damper_ratio_compression": [0.3, 0.3],
 "static_camber_deg": [-0.8, -1.0],
 "caster_deg": 7.0,
 "toe_per_wheel_deg": 0.05,
 "steering_turns_lock_to_lock": 2.7,
 "parking_rack_deg": 33.0,
 "road_reference_deg": 30.0,
 "brake_front_nm": 3400.0,
 "brake_rear_nm": 1300.0,
 "parking_brake_nm": 1200.0,
 "parking_brake_type": "lever",
 "abs_enabled": true,
 "traction_control": true,
 "stability_control": true,
 "esc_understeer_deg_per_g": 1.6,
 "engine": "combustion",
 "torque_curve": [[1000, 380], [1500, 420], [2000, 450], [2500, 475], [3000, 500], [3500, 525], [4250, 542], [5000, 530], [5500, 515], [6000, 497], [6500, 477], [7000, 440]],
 "torque_past_curve_nm": 380.0,
 "engine_friction_nm": 31.0,
 "engine_friction_per_rpm": 0.012,
 "engine_pumping_nm": 28.0,
 "engine_pumping_per_rpm": 0.0015,
 "idle_rpm": 650.0,
 "fuel_cut_rpm": 7000.0,
 "max_gear_rpm": 7000.0,
 "engine_inertia": 0.25,
 "transmission": "manual",
 "gears": [3.66, 2.43, 1.69, 1.32, 1.00, 0.65],
 "reverse_ratio": 3.53,
 "final_drive": 3.73,
 "driveline_efficiency": 0.88,
 "clutch_capacity_nm": 720.0,
 "layout": "rwd",
 "rear_diff_bias": 2.5,
 "rear_diff_coast_bias": 2.5,
 "target_0_60_s": 4.3,
 "target_70_0_ft": 156.0,
 "target_skidpad_g": 0.94,
 "sources": {
  "mass": "3,782 lb C&D test car (PP, Recaro, Shaker); catalogue curb 3,705 lb. Split 53/47 and CG 0.50 m are estimates.",
  "engine": "Ford 2016 brochure: 435 hp at 6500 rpm, 400 lb-ft at 4250 rpm (93 octane). The curve between the two points is an estimate.",
  "gearing": "MT82 3.66/2.43/1.69/1.32/1.00/0.65 (forum, secondary), reverse 3.53 estimate; 3.73 Torsen axle with the PP (Ford brochure).",
  "brakes": "PP: 380 mm front with Brembo 6-piston, 330 mm rear (secondary). Torques are estimates; the front share 0.72 is above the ideal up to 1.0 g.",
  "diff": "TORSEN helical-gear limited slip, PP only (Ford). Bias 2.5 is an estimate.",
  "steering": "EPAS 16.0:1 (forum); 11.52 m circle (auto-data); 2.7 turns derived.",
  "aero": "Cd not published; 0.35 x 2.25 m2 is an estimate.",
  "tyre_pressure": "32/32 psi (tirepressure.com, secondary). Staggered 255/275 tyres run as one 265/40R19.",
  "calibration": "Car and Driver 2016 Mustang GT manual with PP: 4.3 s, 12.9 s at 112 mph, 156 ft, 0.94 g (Yahoo reprint).",
  "estimates": "Split, CG, inertia, springs, bars, dampers, alignment, brake torques, clutch, Torsen bias, ESC gradient, Cd, tyre curve.",
  "research": "lab/cars/mustang16-research.md"
 }
}
```
