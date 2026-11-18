# Honda Civic Type R (FL5), US 2023, 6-speed manual

Scope: 2023 Civic Type R, the only US configuration (6-speed manual, front drive, helical LSD, 19 in Michelin Pilot Sport 4S, adaptive dampers). The main sources are the Honda US specification release on [hondanews.com](https://hondanews.com/en-US/honda-automobiles/releases/2023-honda-civic-type-r-specifications-features) and the Honda Canada 2023 spec sheet ([dealer copy](https://di-uploads-pod2.s3.us-east-1.amazonaws.com/harmonyhonda/uploads/2023/09/2023_SPECS_Civic_TypeR.pdf)). The Car and Driver test sheet was not readable online.

Labels: **M** = measured (magazine instrumented test), **P** = published maker spec, **S** = secondary (dealer, catalogue, database, forum), **E** = estimate.

## Mass and dimensions
- Curb mass 3,188 lb = 1446 kg; weight split 62/38 front (P, Honda US).
- Wheelbase 2735 mm; track 1626 front, 1614 rear (P, Honda Canada; Honda US rounds to 64.0/63.5 in); length 4596, height 1407 mm (P). Width 1890 mm (S, databases).
- CG height 0.52 m (E): 0.37 x roof height, as for a low hatchback.
- Inertia [2760, 2830, 740] kg m2 (E): the mini15s mass-normalised radii of gyration scaled to 1446 kg, 4.596 m and 1.890 m.

## Engine K20C1 (2.0 L turbo four)
- 1996 cm3, bore 86 mm, stroke 85.9 mm, CR 9.8:1, direct injection, VTEC on the exhaust cam, maximum boost 25.2 psi (P).
- 315 hp at 6500 rpm, 310 lb-ft at 2600-4000 rpm, SAE J1349 net; redline 7000 rpm (P).
- Power check: 420 N m x 4000 rpm = 176 kW; 345 N m x 6500 rpm = 235 kW = 315 hp. Both match.
- Curve (rpm: N m): 1000:200, 1500:280, 2000:360, 2600:420, 4000:420, 5000:400, 5500:385, 6000:365, 6500:345, 7000:310. Plateau and the 6500 rpm point are P; the rest E. No point exceeds 235 kW.
- Fuel cut 7000 rpm (P, redline). Idle 750 rpm (E).
- Engine braking (E, mini15s method, same 2.0 L): 30 N m friction and 11 N m pumping at 3000 rpm.

## Driveline
- Ratios 3.625 / 2.115 / 1.529 / 1.125 / 0.911 / 0.734, reverse 3.757, final drive 3.842 (P, Honda US).
- mph per 1000 rpm on 265/30R19 (rolling radius 0.311 m, E): 5.2 / 9.0 / 12.4 / 16.9 / 20.8 / 25.8. 2nd runs out at 63 mph at 7000 rpm, so 0-60 needs one shift.
- Lightweight single-mass flywheel and automatic rev-match, which the driver can switch off (P). Engine and flywheel inertia 0.12 kg m2 (E). Clutch capacity 590 N m (E, 1.4 x peak torque).
- Differential: helical limited slip (P). front_diff_bias 2.5 (E, typical helical torque bias ratio). Like a Torsen, it cannot lock an unloaded inside front wheel; traction control covers that on the real car.

## Tyres, brakes, controls
- Tyres: Michelin Pilot Sport 4S 265/30 R19 on 19 x 9.5J, all four corners (P).
- Placard 35 psi front, 33 psi rear (S, owner report on [CivicXI](https://www.civicxi.com/forum/goto/post?id=863313) and one tyre chart). Other sites give 36/33 or 36/36; read the B-pillar label.
- Brakes: 13.8 in (350 mm) vented front with Brembo four-piston aluminium calipers, 12.0 in (305 mm) solid rear (P).
- Brake torque at full pedal: 2600 N m front, 500 N m rear per wheel (E). Front share 0.84.
- Ideal front share (h 0.52 m, L 2.735 m, 62 % front): 0.72 at 0.5 g, 0.77 at 0.8 g, 0.81 at 1.0 g, 0.83 at 1.1 g. The fronts lock first up to the tyre limit. EBD adds rear at low g.
- ABS, EBD, Brake Assist and VSA with traction control standard; Agile Handling Assist brakes the inside wheels in corners (P).
- Parking brake: electric (EPB) with automatic brake hold (P). Torque 1000 N m (E).

## Steering and suspension
- Variable-ratio EPS, dual-pinion rack. Ratio 11.6:1, 2.10 turns lock to lock, turning diameter 39.9 ft = 12.16 m curb to curb (P, Honda US).
- Lock check: a 12.16 m circle with a 2.735 m wheelbase gives 26.7 deg outer and 35.7 deg inner, mean 31.2 deg. 2.10 x 180 / 31.2 = 12.1:1 mean ratio, close to the published 11.6:1. The numbers agree.
- Front dual-axis strut, rear multi-link, adaptive dampers (P). Anti-roll bars 29 mm front, 20.5 mm rear (P, Honda Canada). Spring rates not published. Wheel rates 52.2 kN/m front and 37.9 kN/m rear (E, for 1.8 and 2.0 Hz).
- Caster 7 deg, camber -1.0 deg front and -1.5 deg rear (E). Read the Honda alignment sheet.

## Aero
- Honda publishes no Cd. CdA 0.70 m2 (E: Cd 0.32 x 2.2 m2). The wing and diffuser make real downforce; the loader has no lift field.

## Targets
- Car and Driver, 2023 Civic Type R test (December 2022 issue, later updated with test results; S quotes):
  - 0-60 mph 4.9 s with 1 ft rollout, quarter mile 13.5 s at 106 mph (M, quoted by [KSL](https://cars.ksl.com/resources/car-review/2023-honda-civic-type-r-test-drive-review) and [AdoreCars](https://www.adorecars.com/reviews/2025-honda-civic-type-r), S).
  - 70-0 mph 153 ft and 1.02 g on the 300 ft skidpad (M, quoted by AdoreCars, S). That page reviews a 2025 car, so the numbers may come from a later Car and Driver test of the same FL5.
- Mean deceleration over 153 ft from 70 mph: 1.07 g.

## Open items
- Read the Car and Driver sheet for the test year, weight split and 70-0.
- Spring rates and alignment from the Honda service manual.
- Next phase: aerodynamic downforce field; Agile Handling Assist as brake vectoring (new features).

```json
{
 "_comment": "2023 Honda Civic Type R (FL5, US), 2.0 L K20C1 turbo four, 6-speed manual, front-wheel drive, helical LSD. Draft from lab/cars/civictyper-research.md; model fields (model_scene, rig, collision, mount) come from the fit.",
 "id": "civictyper",
 "name": "Civic Type R",
 "make": "Honda",
 "body": "hatchback",
 "seats": 4,
 "mass_kg": 1446.0,
 "front_weight": 0.62,
 "cg_height_m": 0.52,
 "inertia": [2760, 2830, 740],
 "wheelbase_m": 2.735,
 "track_front_m": 1.626,
 "track_rear_m": 1.614,
 "tire_width_m": 0.265,
 "tire_aspect_ratio": 0.30,
 "rim_in": 19.0,
 "tyre_pressure_psi": [35.0, 33.0],
 "tyre_model": "tmeasy",
 "tmeasy_peak_mu_y": 1.1,
 "tmeasy_slide_mu_y": 0.9,
 "tmeasy_peak_mu_x": 1.12,
 "tmeasy_slide_mu_x": 0.9,
 "load_sensitivity": 0.1,
 "drag_area_m2": 0.70,
 "unsprung_corner_kg": 40.0,
 "wheel_rate_n_m": [52200, 37900],
 "bar_rate_n_m": [24000.0, 16000.0],
 "damper": ["ratio", "ratio"],
 "damper_ratio_rebound": [0.55, 0.55],
 "damper_ratio_compression": [0.35, 0.35],
 "static_camber_deg": [-1.0, -1.5],
 "caster_deg": 7.0,
 "toe_per_wheel_deg": 0.05,
 "steering_turns_lock_to_lock": 2.1,
 "parking_rack_deg": 35.0,
 "road_reference_deg": 31.0,
 "brake_front_nm": 2600.0,
 "brake_rear_nm": 500.0,
 "parking_brake_nm": 1000.0,
 "parking_brake_type": "electric",
 "abs_enabled": true,
 "traction_control": true,
 "stability_control": true,
 "esc_understeer_deg_per_g": 2.2,
 "engine": "combustion",
 "aspiration": "turbo",
 "torque_curve": [[1000, 200], [1500, 280], [2000, 360], [2600, 420], [4000, 420], [5000, 400], [5500, 385], [6000, 365], [6500, 345], [7000, 310]],
 "torque_past_curve_nm": 260.0,
 "engine_friction_nm": 13.0,
 "engine_friction_per_rpm": 0.0057,
 "engine_pumping_nm": 8.5,
 "engine_pumping_per_rpm": 0.0009,
 "idle_rpm": 750.0,
 "fuel_cut_rpm": 7000.0,
 "max_gear_rpm": 6900.0,
 "engine_inertia": 0.12,
 "transmission": "manual",
 "gears": [3.625, 2.115, 1.529, 1.125, 0.911, 0.734],
 "reverse_ratio": 3.757,
 "final_drive": 3.842,
 "driveline_efficiency": 0.9,
 "clutch_capacity_nm": 590.0,
 "layout": "fwd",
 "front_diff_bias": 2.5,
 "target_0_60_s": 4.9,
 "target_0_60_rollout": true,
 "target_70_0_ft": 153.0,
 "target_skidpad_g": 1.02,
 "sources": {
  "mass": "3,188 lb, 62/38 (Honda US). CG 0.52 m is an estimate.",
  "engine": "Honda: 315 hp at 6500 rpm, 310 lb-ft at 2600-4000 rpm (SAE J1349), redline 7000 rpm, 25.2 psi boost. Curve outside the plateau is an estimate.",
  "gearing": "3.625/2.115/1.529/1.125/0.911/0.734, reverse 3.757, final drive 3.842 (Honda US).",
  "brakes": "350 mm vented front with Brembo 4-piston, 305 mm solid rear (Honda US/Canada). Torques are estimates; front share 0.84 stays above the ideal up to 1.1 g.",
  "diff": "Helical LSD (Honda). Bias 2.5 is an estimate.",
  "steering": "11.6:1 variable, 2.10 turns, 39.9 ft circle (Honda US). Lock check agrees.",
  "aero": "No published Cd; 0.70 m2 is an estimate. Downforce not modelled.",
  "tyre_pressure": "35/33 psi for 265/30R19 (owner report and one tyre chart; secondary).",
  "calibration": "Car and Driver: 4.9 s, 13.5 s at 106 mph (KSL, AdoreCars quotes); 153 ft 70-0, 1.02 g (AdoreCars quote, test year not confirmed).",
  "estimates": "CG, inertia, springs, bar rates, dampers, alignment, brake torques, clutch, diff bias, aero, ESC gradient, tyre curve.",
  "research": "lab/cars/civictyper-research.md"
 }
}
```
