# Toyota GR86 (ZN8), US 2022, 6-speed manual

Scope: 2022 GR86 Premium manual (18 in wheels, Michelin Pilot Sport 4), the trim most magazines tested. Base and Premium share engine, gearbox, Torsen diff and brakes. The Car and Driver test sheet was not readable online; its numbers come through forum and reprint quotes.

Labels: **M** = measured (magazine instrumented test), **P** = published maker spec, **S** = secondary (dealer, catalogue, database, forum), **E** = estimate.

## Mass and dimensions
- Curb mass: base manual 2,811 lb, split 53/47 front (P, [Toyota pressroom, "All-New 2022 Toyota GR 86"](https://pressroom.toyota.com/all-new-2022-toyota-gr-86-thrills-around-every-corner/)). Premium manual 2,838 lb = 1287 kg (S, Edmunds/KBB listings).
- Wheelbase 2575 mm; track 1520 front, 1550 rear (18 in wheels); length 4265, width 1775, height 1310 mm (P, [Toyota UK GR86 technical specifications](https://media.toyota.co.uk/wp-content/uploads/sites/5/pdf/220523M-GR86-Tech-Spec.pdf)).
- CG height 0.46 m (S/E): Toyota quoted 460 mm for the first-generation 86; the ZN8 claims a slightly lower CG with no number.
- Inertia [2110, 2180, 580] kg m2 (E): the mini15s mass-normalised radii of gyration scaled to 1287 kg, 4.265 m and 1.775 m.

## Engine FA24 (2.4 L flat four, naturally aspirated)
- 2387 cm3, bore 94 mm, stroke 86 mm, CR 12.5:1, D-4S direct and port injection (P, Toyota UK).
- US rating 228 hp at 7000 rpm, 184 lb-ft at 3700 rpm (P, Toyota US; also [KBB](https://www.kbb.com/toyota/gr86/2022/specs), S). UK: 172 kW, 250 N m (P).
- Power check: 232 N m x 7000 rpm = 170 kW = 228 hp. The curve must hold 232 N m at 7000 rpm.
- Curve (rpm: N m): 1000:180, 2000:205, 3000:235, 3700:250, 4500:243, 5500:240, 6500:238, 7000:232, 7500:205. The 3700 and 7000 rpm points are P; the shape between them is E. No point exceeds 170 kW.
- Tach redline 7400 rpm (S, owner reports). Fuel cut 7500 rpm (E). Idle 700 rpm (E).
- Engine braking (E, mini15s method scaled to 2.39 L): FMEP 1.9 bar gives 36 N m friction at 3000 rpm; PMEP 0.7 bar gives 13 N m pumping.

## Driveline
- Ratios 3.626 / 2.189 / 1.541 / 1.213 / 1.000 / 0.767, reverse 3.438, final drive 4.100 (P, Toyota UK). US listings give the same 4.10 (S).
- mph per 1000 rpm on 215/40R18 (rolling radius 0.305 m, E): 4.8 / 8.0 / 11.3 / 14.4 / 17.4 / 22.7. 2nd runs out at 59 mph at 7400 rpm, so the 0-60 run includes the 2-3 shift.
- Differential: Torsen limited slip (P, Toyota pressroom). Bias 2.5 drive, 2.0 coast (E, typical Torsen torque bias ratio). A Torsen acts like an open diff when the inside rear lifts; the bias model cannot show that.
- Clutch: single dry plate. Capacity 375 N m (E, 1.5 x peak torque). Engine and flywheel inertia 0.12 kg m2 (E).

## Tyres, brakes, controls
- Tyres: Premium 215/40R18 Michelin Pilot Sport 4 (P); base 215/45R17 Michelin Primacy HP (P).
- Placard 35/35 psi for 215/40R18 (S, [Firestone fitment page](https://vehicle.firestonecompleteautocare.com/toyota/gr86/2022/tires/) and Michelin). One site gives 32/32; read the B-pillar label.
- Brakes: 11.6 in (294 mm) vented front, 11.4 in (290 mm) vented rear (P, Toyota pressroom).
- Brake torque at full pedal: 2000 N m front, 750 N m rear per wheel (E). Front share 0.73.
- Ideal front share (h 0.46 m, L 2.575 m, 53 % front): 0.62 at 0.5 g, 0.67 at 0.8 g, 0.71 at 1.0 g. The fronts lock first up to the tyre limit.
- ABS, VSC and TRAC standard; Track mode and VSC off by button on the manual (P).
- Parking brake: centre hand lever with cables to the rear (S, dealer equipment list "hand-lever parking brake"; Toyota parts catalogue lists a parking brake cable). Torque 900 N m (E).

## Steering and suspension
- EPS rack, overall ratio 13.5:1, about 2.5 turns lock to lock (P, Toyota pressroom; Toyota UK also gives 2.5). Minimum turning radius 5.4 m at the tyre (P, Toyota UK).
- Steering check: 2.5 x 180 / 13.5 = 33.3 deg mean road-wheel lock. A 5.4 m outer-tyre radius with a 2.575 m wheelbase gives 28.5 deg outer and 38.6 deg inner, mean 33.6 deg. The numbers agree.
- Front MacPherson strut, rear double wishbone, anti-roll bars both ends (P). Spring rates and bar sizes are not published. Wheel rates 34.6 kN/m front and 37.7 kN/m rear (E, for 1.7 and 1.9 Hz).
- Caster 6.5 deg, camber -0.8 deg front and -1.3 deg rear (E).

## Aero
- Toyota US publishes no Cd. CdA 0.57 m2 (E: Cd 0.29 x 1.95 m2, first-generation 86 class values).
- The UK top speed of 140 mph cannot check CdA: at 140 mph the car needs only about 6200 rpm in 6th, and the power there would allow a CdA near 0.85 m2. The 140 mph figure is probably a limiter or a conservative claim (E).

## Targets
- Car and Driver, 2022 GR86 and Subaru BRZ first test (M, quoted on [FT86Club](https://www.ft86club.com/forums/showthread.php?t=147704), S): 0-60 mph 5.4 s with 1 ft rollout, quarter mile 13.9 s at 101 mph, 0.98 g on the 300 ft skidpad (the BRZ 0.99 g). Trim and gearbox are not shown in the quote; Car and Driver tested manual cars first.
- 70-0 mph 156 ft (M, Car and Driver comparison summary, S; BRZ 152 ft). Not verified against a test sheet.
- Toyota claims 6.1 s 0-60 for the manual (P). The 0.7 s gap to Car and Driver is the rollout plus a hard launch; calibrate to 5.4 s with rollout.

## Open items
- Read the Car and Driver sheet for trim, gearbox, weight split and 70-0.
- Spring rates, bar sizes and alignment from the Toyota/Subaru repair manual.
- Door placard on a real car (35 or 32 psi).

```json
{
 "_comment": "2022 Toyota GR86 Premium (ZN8, US), 2.4 L FA24 flat four, 6-speed manual, rear-wheel drive, Torsen LSD. Draft from lab/cars/gr86-research.md; model fields (model_scene, rig, collision, mount) come from the fit.",
 "id": "gr86",
 "name": "GR86",
 "make": "Toyota",
 "body": "coupe",
 "seats": 4,
 "mass_kg": 1287.0,
 "front_weight": 0.53,
 "cg_height_m": 0.46,
 "inertia": [2110, 2180, 580],
 "wheelbase_m": 2.575,
 "track_front_m": 1.52,
 "track_rear_m": 1.55,
 "tire_width_m": 0.215,
 "tire_aspect_ratio": 0.40,
 "rim_in": 18.0,
 "tyre_pressure_psi": [35.0, 35.0],
 "tyre_model": "tmeasy",
 "tmeasy_peak_mu_y": 1.05,
 "tmeasy_slide_mu_y": 0.85,
 "tmeasy_peak_mu_x": 1.05,
 "tmeasy_slide_mu_x": 0.85,
 "load_sensitivity": 0.1,
 "drag_area_m2": 0.57,
 "unsprung_corner_kg": 36.0,
 "wheel_rate_n_m": [34600, 37700],
 "bar_rate_n_m": [20000.0, 12000.0],
 "damper": ["ratio", "ratio"],
 "damper_ratio_rebound": [0.5, 0.5],
 "damper_ratio_compression": [0.3, 0.3],
 "static_camber_deg": [-0.8, -1.3],
 "caster_deg": 6.5,
 "toe_per_wheel_deg": 0.05,
 "steering_turns_lock_to_lock": 2.5,
 "parking_rack_deg": 37.0,
 "road_reference_deg": 33.5,
 "brake_front_nm": 2000.0,
 "brake_rear_nm": 750.0,
 "parking_brake_nm": 900.0,
 "parking_brake_type": "lever",
 "abs_enabled": true,
 "traction_control": true,
 "stability_control": true,
 "esc_understeer_deg_per_g": 1.8,
 "engine": "combustion",
 "torque_curve": [[1000, 180], [2000, 205], [3000, 235], [3700, 250], [4500, 243], [5500, 240], [6500, 238], [7000, 232], [7500, 205]],
 "torque_past_curve_nm": 170.0,
 "engine_friction_nm": 15.5,
 "engine_friction_per_rpm": 0.0068,
 "engine_pumping_nm": 10.1,
 "engine_pumping_per_rpm": 0.0011,
 "idle_rpm": 700.0,
 "fuel_cut_rpm": 7500.0,
 "max_gear_rpm": 7400.0,
 "engine_inertia": 0.12,
 "transmission": "manual",
 "gears": [3.626, 2.189, 1.541, 1.213, 1.0, 0.767],
 "reverse_ratio": 3.438,
 "final_drive": 4.1,
 "driveline_efficiency": 0.88,
 "clutch_capacity_nm": 375.0,
 "layout": "rwd",
 "rear_diff_bias": 2.5,
 "rear_diff_coast_bias": 2.0,
 "target_0_60_s": 5.4,
 "target_0_60_rollout": true,
 "target_70_0_ft": 156.0,
 "target_skidpad_g": 0.98,
 "sources": {
  "mass": "2,811 lb base manual and 53/47 split (Toyota pressroom); Premium manual 2,838 lb (Edmunds/KBB, secondary). CG 0.46 m from the first-generation 86 claim.",
  "engine": "Toyota US: 228 hp at 7000 rpm, 184 lb-ft at 3700 rpm (UK 172 kW, 250 N m). Curve shape between the two points is an estimate.",
  "gearing": "3.626/2.189/1.541/1.213/1.000/0.767, reverse 3.438, final drive 4.100 (Toyota UK tech spec).",
  "brakes": "294 mm vented front, 290 mm vented rear (Toyota pressroom). Torques are estimates; front share 0.73 stays above the ideal up to 1.0 g.",
  "diff": "Torsen limited slip (Toyota pressroom). Bias 2.5 drive, 2.0 coast are estimates.",
  "steering": "13.5:1, 2.5 turns, 5.4 m tyre radius (Toyota). Lock check agrees.",
  "aero": "No published Cd; 0.57 m2 is an estimate.",
  "tyre_pressure": "35/35 psi for 215/40R18 (Firestone, Michelin; secondary).",
  "calibration": "Car and Driver first test: 5.4 s, 0.98 g (FT86Club quote); 156 ft 70-0 (comparison summary). Not checked against the test sheet.",
  "estimates": "CG, inertia, springs, bars, dampers, alignment, brake torques, clutch, diff bias, aero, ESC gradient, tyre curve.",
  "research": "lab/cars/gr86-research.md"
 }
}
```
