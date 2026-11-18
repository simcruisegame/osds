# Porsche 911 Carrera S coupe (992.1), US 2020, 8-speed PDK

Scope: 2020, rear drive, PDK, steel brakes, standard PASM, no rear-axle steering. Main sources: the Porsche 2020 US spec sheet and the EU technical data sheet; they agree on ratios and dimensions. No Car and Driver PDK test sheet was readable online, so the targets mix a PDK 0-60 with braking and skidpad from the manual car on the same tyres.

Labels: **M** = measured (magazine instrumented test), **P** = published maker spec, **S** = secondary (dealer, catalogue, database, forum), **E** = estimate.

## Mass and dimensions
- Curb mass 3,382 lb = 1534 kg, PDK (P, [Porsche 2020 911 Carrera S/4S US specifications](https://newsroom.porsche.com/dam/jcr:d5712d5c-b376-41d9-8201-09731162e143/2020_911_Carrera_S_and_911_Carrera_4S_US_specifications.pdf.PDF)). EU DIN unladen 1515 kg (P, [Porsche EU technical data](https://media.autoprove.net/wp-content/uploads/2019/05/99e50eae23b78195f3e8a93327326a36.pdf)).
- Weight split 38/62 front (E). Porsche publishes none; a forum reading gives 38/62 ([911UK](https://911uk.com/porsche/weight-distribution-front-rear-on-different-911-gt-gens.119203/latest), S).
- Wheelbase 2450 mm; track 1589 front, 1557 rear; length 4519, width 1852, height 1300 mm (P, both sheets).
- CG height 0.46 m (E): 0.355 x 1.300 m roof height.
- Inertia [2830, 2900, 755] kg m2 (E): the mini15s mass-normalised radii of gyration scaled to 1534 kg, 4.519 m and 1.852 m. The rear engine makes real yaw and pitch higher (lower bound).

## Engine 9A2 Evo (3.0 L twin-turbo flat six)
- US rating 443 hp at 6500 rpm, 390 lb-ft at 2300-5000 rpm, maximum engine speed 7500 rpm (P, US sheet). EU: 331 kW (450 PS), 530 N m (P).
- Power check: 530 N m x 5000 rpm = 277 kW; 486 N m x 6500 rpm = 331 kW. Both match.
- Curve (rpm: N m): 1000:260, 1500:380, 2000:490, 2300:530, 5000:530, 5500:525, 6000:515, 6500:486, 7000:445, 7500:400. Plateau and 6500 rpm point P; the rest E, no point above 331 kW.
- Idle 700 rpm (E). Fuel cut 7500 rpm (P, maximum engine speed).
- Engine braking (E, Heywood ch. 13, mini15s method scaled to 2.98 L): FMEP 1.9 bar gives 45 N m friction at 3000 rpm; PMEP 0.7 bar gives 17 N m pumping.

## Driveline
- PDK ratios 4.89 / 3.17 / 2.15 / 1.56 / 1.18 / 0.94 / 0.76 / 0.61; reverse 3.99 (EU) or 3.96 (US sheet) (P).
- Final drive 3.12 (P, EU sheet "rear axle ratio"). The US sheet prints 3.39 with a 0.92 constant ratio; 3.39 x 0.92 = 3.12, so they agree.
- Clutches: 8.7 in and 6.6 in diameter (P, US sheet). Capacity 800 N m (E, 1.5 x peak torque). Shift time 0.08 s (E).
- mph per 1000 rpm on the rear 305/30 R21 (rolling radius 0.347 m, E): 5.3 / 8.2 / 12.1 / 16.7 / 22.1 / 27.8 / 34.3 / 42.8. 2nd runs out at 62 mph at 7500 rpm, so 0-60 needs only the 1-2 shift.
- Differential: PTV Plus: electronic rear differential lock with variable lock torque plus brake torque vectoring (P, EU sheet). It is standard on the 2020 Carrera S PDK in North America (S, [Porsche Canada finder listing](https://finder.porsche.com/ca/en-CA/details/RE7M77)). The loader has no active lock; bias 3.0 drive and 1.8 coast stand in (E).

## Tyres, brakes, controls
- Tyres: 245/35 ZR20 on 8.5J x 20 ET53 front, 305/30 ZR21 on 11.5J x 21 ET67 rear (P). The loader takes one size: use the rear (driven) size and axle_grip [0.95, 1.0] for the front (E).
- Placard 36 psi front, 45 psi rear (S, [tirepressure.com](https://tirepressure.com/2020-porsche-911-tire-pressure)). Read a real label: Porsche also prints "comfort" pressures.
- Brakes: 350 x 34 mm front with six-piston monobloc calipers, 350 x 28 mm rear with four-piston monobloc calipers, vented and drilled (P).
- Brake torque at full pedal: 2800 N m front, 2000 N m rear per wheel (E). Front force share 0.60 (front rolling radius 0.330 m, rear 0.347 m).
- Ideal front share (h 0.46 m, L 2.45 m, 38 % front): 0.47 at 0.5 g, 0.53 at 0.8 g, 0.57 at 1.0 g, 0.58 at 1.06 g. The fronts lock first at every level; EBD adds rear at low g.
- PSM (stability control) with ABS is standard (P).
- Parking brake: electric (P, EU sheet). Torque 1500 N m (E).

## Steering and suspension
- Electromechanical steering, variable ratio 15.0:1 on centre, about 12.25:1 near lock (P; end value from the 992.2 sheet). Turning circle 11.2 m = 36.8 ft (P).
- Lock check: an 11.2 m circle with a 2.45 m wheelbase gives 26 deg outer and 35 deg inner, mean 31 deg. With a mean ratio near 13.3:1 that is 2 x 31 x 13.3 / 360 = 2.3 turns (E).
- Front MacPherson strut, rear five-link, anti-roll bars both ends, PASM adaptive dampers (P). Springs and bars not published; wheel rates 29.1 kN/m front and 60.9 kN/m rear (E, for 1.7 and 1.9 Hz).
- Caster 8 deg, camber -0.8 deg front and -1.5 deg rear (E).

## Aero
- EU: Cd 0.29, A 2.07 m2, CdA 0.600 m2 (P). US sheet: Cd 0.31 (P), CdA 0.64 m2. Use 0.62 m2 (E, midpoint)
- Top-speed check at 308 km/h: drag takes 236 kW and rolling resistance 16 kW. The wheels get about 298 kW, so the claim is consistent.

## Targets
- 0-60 mph 3.0 s, PDK with launch control (M, Car and Driver, quoted by [Gear Patrol](https://www.gearpatrol.com/cars/a33313936/2020-porsche-911-carrera-s-manual-review/), S). 1 ft rollout. Porsche claims 3.5 s (P).
- 70-0 mph 136 ft and 1.06 g on the 300 ft skidpad (M, Car and Driver, "Tested: 2020 Porsche 911 Carrera S Reminds Us of the Importance of a Clutch Pedal and Shifter", [Yahoo reprint](https://autos.yahoo.com/tested-2020-porsche-911-carrera-140700061.html), S). That car was the 7-speed manual on the same tyre sizes; Car and Driver says the gearbox does not change braking or cornering.

## Open items
- Weight split, door placard, lock-to-lock turns and the US/EU Cd difference.
- A PDK-only Car and Driver sheet with braking and skidpad, to remove the mixed target.
- Next phase: PTV Plus as an active lock with brake vectoring (new feature).

```json
{
 "_comment": "2020 Porsche 911 Carrera S coupe (992.1, US), 3.0 L twin-turbo flat six, 8-speed PDK, rear-wheel drive, PTV Plus. Draft from lab/cars/porsche992-research.md; model fields (model_scene, rig, collision, mount) come from the fit.",
 "id": "porsche992",
 "name": "911 Carrera S",
 "make": "Porsche",
 "body": "coupe",
 "seats": 4,
 "mass_kg": 1534.0,
 "front_weight": 0.38,
 "cg_height_m": 0.46,
 "inertia": [2830, 2900, 755],
 "wheelbase_m": 2.45,
 "track_front_m": 1.589,
 "track_rear_m": 1.557,
 "tire_width_m": 0.305,
 "tire_aspect_ratio": 0.30,
 "rim_in": 21.0,
 "axle_grip": [0.95, 1.0],
 "tyre_pressure_psi": [36.0, 45.0],
 "tyre_model": "tmeasy",
 "tmeasy_peak_mu_y": 1.12,
 "tmeasy_slide_mu_y": 0.9,
 "tmeasy_peak_mu_x": 1.15,
 "tmeasy_slide_mu_x": 0.9,
 "load_sensitivity": 0.1,
 "drag_area_m2": 0.62,
 "unsprung_corner_kg": 44.0,
 "wheel_rate_n_m": [29100, 60900],
 "bar_rate_n_m": [26000.0, 18000.0],
 "damper": ["ratio", "ratio"],
 "damper_ratio_rebound": [0.55, 0.55],
 "damper_ratio_compression": [0.35, 0.35],
 "static_camber_deg": [-0.8, -1.5],
 "caster_deg": 8.0,
 "toe_per_wheel_deg": 0.05,
 "steering_turns_lock_to_lock": 2.3,
 "parking_rack_deg": 35.0,
 "road_reference_deg": 31.0,
 "brake_front_nm": 2800.0,
 "brake_rear_nm": 2000.0,
 "parking_brake_nm": 1500.0,
 "parking_brake_type": "electric",
 "abs_enabled": true,
 "traction_control": true,
 "stability_control": true,
 "esc_understeer_deg_per_g": 1.5,
 "engine": "combustion",
 "aspiration": "turbo",
 "torque_curve": [[1000, 260], [1500, 380], [2000, 490], [2300, 530], [5000, 530], [5500, 525], [6000, 515], [6500, 486], [7000, 445], [7500, 400]],
 "torque_past_curve_nm": 330.0,
 "engine_friction_nm": 19.4,
 "engine_friction_per_rpm": 0.0085,
 "engine_pumping_nm": 12.7,
 "engine_pumping_per_rpm": 0.0013,
 "idle_rpm": 700.0,
 "fuel_cut_rpm": 7500.0,
 "max_gear_rpm": 7400.0,
 "engine_inertia": 0.16,
 "transmission": "dct",
 "gears": [4.89, 3.17, 2.15, 1.56, 1.18, 0.94, 0.76, 0.61],
 "reverse_ratio": 3.99,
 "final_drive": 3.12,
 "driveline_efficiency": 0.9,
 "shift_seconds": 0.08,
 "upshift_rpm": 7300.0,
 "downshift_rpm": 2500.0,
 "launch_control_rpm": 4500.0,
 "clutch_capacity_nm": 800.0,
 "clutch_back_nm": 750.0,
 "layout": "rwd",
 "rear_diff_bias": 3.0,
 "rear_diff_coast_bias": 1.8,
 "target_0_60_s": 3.0,
 "target_0_60_rollout": true,
 "target_70_0_ft": 136.0,
 "target_skidpad_g": 1.06,
 "sources": {
  "mass": "3,382 lb PDK (Porsche 2020 US spec sheet); DIN 1515 kg (EU sheet). Split 38/62 and CG 0.46 m are estimates.",
  "engine": "Porsche: 443 hp at 6500 rpm, 390 lb-ft at 2300-5000 rpm, 7500 rpm maximum. Curve outside the plateau is an estimate.",
  "gearing": "PDK ratios and 3.12 rear axle ratio (EU sheet; US sheet 3.39 x 0.92 constant = 3.12). Reverse 3.99 EU, 3.96 US.",
  "brakes": "350 x 34 mm 6-piston front, 350 x 28 mm 4-piston rear (Porsche). Torques are estimates; front share 0.60 stays above the ideal up to 1.06 g.",
  "diff": "PTV Plus electronic rear lock with brake vectoring (Porsche EU sheet; standard on the North American Carrera S PDK, secondary). Bias 3.0/1.8 stands in.",
  "steering": "15.0:1 on centre to about 12.25:1, 11.2 m circle (Porsche). 2.3 turns is an estimate.",
  "aero": "EU Cd 0.29 x 2.07 m2 = 0.600; US Cd 0.31. 0.62 m2 is the midpoint.",
  "tyre_pressure": "36/45 psi for 245/35ZR20 and 305/30ZR21 (tirepressure.com, secondary). Staggered tyres: rear size in the file, axle_grip for the front.",
  "calibration": "Car and Driver: 3.0 s 0-60 PDK (via Gear Patrol); 136 ft 70-0 and 1.06 g from the 2020 manual Carrera S on the same tyres (Yahoo reprint).",
  "estimates": "Weight split, CG, inertia, springs, bars, dampers, alignment, brake torques, clutch, shift time, diff bias, ESC gradient, tyre curve, axle_grip.",
  "research": "lab/cars/porsche992-research.md"
 }
}
```
