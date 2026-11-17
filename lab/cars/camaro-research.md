# 1987 Chevrolet Camaro IROC-Z 5.7 (L98 TPI, 700R4): research for a car file

Status: research only (2026-11-17). Labels: **measured** (instrumented magazine test), **published spec** (maker or magazine spec panel), **secondary** (dealer, forum or database; not checked against a primary document), **estimate** (my calculation; method given).

## Configuration chosen, and why

1987 IROC-Z coupe, 5.7 L L98 Tuned Port Injection V8, THM 700R4 four-speed automatic, 3.27 Borg-Warner axle, optional rear discs, P245/50VR16.

Reason: Road & Track, October 1986, "Ford Mustang GT vs Chevrolet Camaro IROC-Z" (tests 1987 models) gives the most complete instrumented set found for any IROC-Z: 0-60, quarter mile, 60-0 **and** 80-0, skidpad, slalom, top speed, a full spec panel, mph/1000 rpm and the converter ratio. Text and data: [thirdgen.org reprint](https://www.thirdgen.org/rt_october1986_mustang_vs_camaro_irocz/). No 5.0 LB9 five-speed test with braking and skidpad data was found online (the scans of the 1988 R&T IROC-Z vs Mustang 5.0 on thirdgen.org could not be read). The L98 was automatic-only ([chevyhardcore.com](https://www.chevyhardcore.com/?p=8683), secondary).

Caution: R&T tested an early car and printed **220 bhp at 4200 rpm, 320 lb-ft at 3200 rpm**. The production 1987 rating is **225 hp at 4400 rpm, 330 lb-ft** (secondary, [chevyhardcore.com](https://www.chevyhardcore.com/?p=8683); the torque-peak rpm is 2800 or 3200, sources disagree). The file uses the R&T figures, because the targets come from that car.

## Mass and chassis

| Item | Value | Label / source |
|---|---|---|
| Curb weight | 3,490 lb (1,583 kg) | published spec, R&T Oct 1986 (test weight 3,720 lb) |
| Weight split F/R | 58/42 % | published spec, R&T Oct 1986 |
| Wheelbase | 101.0 in (2.565 m) | published spec, R&T |
| Track F/R | 60.7 / 60.6 in (1.542 / 1.539 m) | published spec, R&T |
| Length / width / height | 192.0 / 72.8 / 50.3 in (4.877 / 1.849 / 1.278 m) | published spec, R&T |
| CG height | 0.50 m | estimate: 0.39 x roof height (NB Miata ratio 0.37; the F-body sits low but carries an iron V8 high in the bay) |
| Inertia [pitch, yaw, roll] | 2310, 2870, 765 kg m2 | estimate: mustang65 values scaled by mass and body-box size |

## Engine (L98, 5733 cc, bore x stroke 101.6 x 88.4 mm, CR 9.0:1; published spec, R&T)

Firing order 1-8-4-3-6-5-7-2 (GM small-block, published spec). Cross-plane crank: the uneven bank pulses give the "burble" R&T notes. The loader has no firing-order field; the sound work needs one.

| rpm | N m | Label |
|---|---|---|
| 600 | 300 | estimate (idle in Drive) |
| 1000 | 340 | estimate |
| 1500 | 380 | estimate |
| 2000 | 405 | estimate |
| 2500 | 425 | estimate |
| 3200 | 434 | published spec (320 lb-ft, R&T) |
| 3600 | 420 | estimate |
| 4200 | 373 | published spec (220 bhp at 4200 rpm = 275 lb-ft) |
| 4600 | 330 | estimate |
| 5000 | 285 | estimate |
| 5500 | 240 | estimate |

Power check: no point exceeds 220 hp (3600 rpm: 212 hp; 4600 rpm: 213 hp). The shape follows the long-runner TPI character (strong low end, falls fast past 4500 rpm); no published dyno curve was found. Idle 600 rpm and fuel cut 5500 rpm are estimates: no factory Camaro redline was found (the Corvette L98 tach is red at 5000 per a [Corvette Forum post](https://www.corvetteforum.com/forums/c4-tech-performance/2245797-success-transplanting-a-92-lt1-instrument-cluster-in-a-91-l98-pics-inside-2.html), secondary). R&T saw 4500-4600 rpm at the 149 mph top speed.

Engine braking (estimate; Heywood SI friction correlation, FMEP about 1.5 bar at 2000 rpm and 2.4 bar at 4000 rpm for 5.73 L): friction 40 N m + 0.016 N m/rpm, closed-throttle pumping 35 N m. First check that the sim treats `torque_curve` as net brake torque, so friction is not counted twice.

## Driveline

- 700R4 ratios 3.06 / 1.63 / 1.00 / 0.70 (published spec, R&T). Reverse 2.29 (secondary, common 700R4 spec).
- Converter torque ratio 1.8 at stall (published spec: R&T prints "1.8 x" before the gear ratios). The lock-up clutch in 3rd and 4th is not modelled.
- Final drive 3.27, Borg-Warner axle, the only ratio offered (published spec, R&T).
- Limited slip: G80 Positraction, cone-clutch type in the Borg-Warner 7.75 in axle (secondary: [camarotech.com](https://camarotech.com/ThirdGenDescription.html) and dealer listings). Bias 2.0 drive and 2.0 coast: estimate (a cone LSD locks in both directions).
- Gearing check: a 245/50R16 is 651 mm in diameter (r 0.326 m, rolling about 0.324 m). In 4th, 1000 rpm gives 437 wheel rpm, 33.3 mph. R&T measured 33.1 mph/1000 rpm: **agrees**. Speeds at 5000 rpm: 38, 71 and 116 mph in 1st to 3rd.

## Tyres, brakes, steering, suspension

- Tyres: Goodyear Eagle VR50 ("Gatorback" tread) P245/50VR16 radial on all four corners; 16 x 8 cast alloy wheels (published spec, R&T). Pressures 30/30 psi (secondary, [Firestone 1987 Camaro listing](https://vehicle.firestonecompleteautocare.com/chevrolet/camaro/1987/tires)).
- Brakes: 10.5 in vented discs front and rear (published spec, R&T; rear discs were a USD 186 option, J65). No ABS on any third-gen IROC. A combination valve with rear proportioning is fitted (secondary). R&T: the Camaro had "better proportioning of braking force" and was "more stable under braking" than the Mustang.
- Steering: recirculating ball, power assist (published spec, R&T). Ratio 14:1 quick box (secondary, [camarotech.com steering](https://camarotech.com/steering.html); other pages say 12.7:1, unverified). Box travel 32 deg for the 16 in wheel box (same source). Check: 2 x 32 deg x 14 / 360 deg = **2.49 turns**, which agrees with the commonly quoted 2.5. Turning circle 11.5 m: estimate from wheelbase and lock; no published figure found.
- Suspension: front modified MacPherson strut with separate coil springs and lower A-arms; rear live axle with torque arm, trailing arms, Panhard rod and coils; anti-roll bars at both ends (published spec, R&T). Spring rates and bar diameters were **not found** (forum bar sizes conflict: 24 mm front, 35 mm rear, unverified). The JSON wheel rates come from ride frequencies of 1.45 Hz front and 1.55 Hz rear (estimate; R&T calls the car "stiffly sprung" and rates ride 6.5/10).
- Alignment: caster 3.0 deg and front camber 0.5 deg are estimates.

## Aerodynamics

Cd 0.34, frontal area 21 ft2 (1.95 m2), CdA 0.66 m2 (secondary: [ClassRacer thread](https://classracer.com/classforum/showthread.php?p=456878), [camaroz28.com thread](https://www.camaroz28.com/forums/3rd-gen-l98-engine-tech-20/drag-co-efficiency-282680/)). Check against R&T's 149.2 mph: about 187 hp at the wheels (0.85 efficiency), less 17 kW rolling loss, leaves 122 kW for drag, so CdA must be at most 0.69 m2. **0.66 fits; the also-quoted Cd 0.37 does not.**

## Targets (R&T Oct 1986, measured)

| Test | Value | Note |
|---|---|---|
| 0-60 mph | 6.8 s | 0-30 2.3 s, 0-100 18.4 s; quarter mile 15.3 s at 90.5 mph |
| 60-0 mph | 159 ft (0.76 g) | measured |
| 80-0 mph | 261 ft (0.82 g) | measured |
| 70-0 mph | 208 ft | **derived**: mean of 159 x (70/60)^2 = 216 and 261 x (70/80)^2 = 200 |
| Skidpad | 0.85 g | measured on a **150 ft diameter** pad, not 300 ft: open item |
| Slalom | 63.3 mph | measured |
| Top speed | 149 mph | measured |

Other 0-60: 6.6 s, Motor Trend Nov 1989 (350/230 hp; [musclecarfacts.com](https://www.musclecarfacts.com/?p=61), secondary). The 0.92 g often quoted for the 1985 IROC is a maker or tyre claim, not a test.

## Brake balance (estimate: h 0.50 m, L 2.565 m, 58 % front)

Ideal front share = 0.58 + 0.195 x decel (g): 0.68 at 0.5 g, 0.74 at 0.8 g, 0.78 at 1.0 g. The proposed per-wheel torques (2100 N m front, 850 N m rear, valve knee 0.35, slope 0.40) give a front share of 0.71 at the knee (0.43 g), 0.76 at 0.68 g and 0.78 at 0.88 g: the fronts lock first up to the tyre limit. The ISO 7975 brake-in-turn bench must confirm.

## In-car

- Speedometer: 145 mph face for the 1987 IROC-Z and Z28 (secondary, [camarotech.com](https://camarotech.com/ThirdGenDescription.html)); R&T praised the "140-mph" speedometer (rounding or typo). Tachometer face and redline: not confirmed; compare with photographs.
- Analogue cluster: large speedometer and tachometer with small gauges for oil pressure, coolant, voltage and fuel (secondary, from photographs; verify on the turntable).
- Low seat, long doors, T-tops common. Power seat option (USD 250, R&T). R&T rated the front seats 6.5/10. Driver eye about 0.95 m above ground: estimate (roof 1.278 m, reclined seat).

## Open items

Spring rates, bar diameters, damper data, factory redline, turning circle, CG height, the door placard and a 300 ft skidpad figure. Sources to find: the 1987 Chevrolet Camaro shop manual (spring and bar data) and a Car and Driver IROC-Z test with a 300 ft skidpad.

## JSON fragment (field names as in data/cars/mustang65.json and the automatic files; model and rig fields left out)

```json
{
 "_comment": "1987 Chevrolet Camaro IROC-Z coupe, 5.7 L L98 TPI V8 (R&T test car: 220 bhp), 700R4 four-speed automatic, 3.27 Borg-Warner axle with cone limited slip, P245/50VR16 Goodyear Eagle VR50, four-wheel discs, no ABS.",
 "id": "camaro87",
 "name": "1987 Camaro IROC-Z 5.7",
 "make": "Chevrolet",
 "body": "coupe",
 "seats": 4,
 "mass_kg": 1583,
 "front_weight": 0.58,
 "cg_height_m": 0.50,
 "inertia": [2310, 2870, 765],
 "collision_box": [1.85, 0.52, 4.85],
 "collision_box_y": 0.66,
 "wheelbase_m": 2.565,
 "track_front_m": 1.542,
 "track_rear_m": 1.539,
 "unsprung_corner_kg": 50,
 "wheel_rate_n_m": [34400, 26300],
 "reference_corner_load_n": 3882,
 "tire_width_m": 0.245,
 "tire_aspect_ratio": 0.50,
 "rim_in": 16,
 "tyre_pressure_psi": [30, 30],
 "tyre_model": "tmeasy",
 "drag_area_m2": 0.66,
 "parking_rack_deg": 32,
 "steering_turns_lock_to_lock": 2.5,
 "brake_front_nm": 2100,
 "brake_rear_nm": 850,
 "brake_rear_knee": 0.35,
 "brake_rear_slope": 0.40,
 "abs_enabled": false,
 "parking_brake_nm": 1000,
 "engine": "combustion",
 "torque_curve": [[600, 300], [1000, 340], [1500, 380], [2000, 405], [2500, 425], [3200, 434], [3600, 420], [4200, 373], [4600, 330], [5000, 285], [5500, 240]],
 "torque_past_curve_nm": 200,
 "idle_rpm": 600,
 "idle_omega": 63,
 "governor_rpm": 625,
 "fuel_cut_rpm": 5500,
 "max_gear_rpm": 5600,
 "max_omega": 586,
 "engine_inertia": 0.30,
 "engine_friction_nm": 40,
 "engine_friction_per_rpm": 0.016,
 "engine_pumping_nm": 35,
 "gears": [3.06, 1.63, 1.00, 0.70],
 "reverse_ratio": 2.29,
 "final_drive": 3.27,
 "driveline_efficiency": 0.85,
 "shift_seconds": 0.35,
 "upshift_rpm": 4800,
 "downshift_rpm": 1600,
 "clutch_capacity_nm": 650,
 "clutch_back_nm": 600,
 "launch_rpm": 1200,
 "launch_rpm_per_throttle": 800,
 "converter_stall_ratio": 1.8,
 "converter_coupling_ratio": 0.87,
 "layout": "rwd",
 "rear_diff_bias": 2.0,
 "rear_diff_coast_bias": 2.0,
 "transmission": "automatic",
 "static_camber_deg": [0.5, 0],
 "camber_per_m": [6, 0],
 "caster_deg": 3.0,
 "target_0_60_s": 6.8,
 "target_70_0_ft": 208,
 "target_skidpad_g": 0.85,
 "traction_control": false,
 "card": {
  "turning_circle_m": 11.5,
  "steering_ratio": 14.0,
  "handling": "Road & Track (Oct 1986): 0.85 g on a 150 ft skidpad, 63.3 mph slalom; stiffly sprung, more stable under braking than the Mustang GT; less precise on centre."
 },
 "sources": {
  "calibration": "Road & Track, October 1986, Mustang GT vs Camaro IROC-Z (1987 models), thirdgen.org reprint: 0-60 6.8 s, 1/4 mile 15.3 s at 90.5 mph, 60-0 159 ft, 80-0 261 ft, 0.85 g on a 75 ft radius skidpad, 63.3 mph slalom, 149 mph. 70-0 208 ft derived from the two stops.",
  "engine": "R&T test car: 220 bhp at 4200 rpm, 320 lb-ft at 3200 rpm (production 1987 rating 225 hp at 4400, 330 lb-ft; secondary). Curve shape, idle and fuel cut are estimates. Firing order 1-8-4-3-6-5-7-2.",
  "gearing": "700R4 3.06/1.63/1.00/0.70, converter 1.8:1, 3.27 axle (R&T); reverse 2.29 (secondary). R&T's 33.1 mph/1000 rpm in 4th matches the tyre size.",
  "chassis": "3,490 lb curb, 58/42, 101.0 in wheelbase, 60.7/60.6 in track, 192.0 x 72.8 x 50.3 in (R&T).",
  "aero": "Cd 0.34 x 21 ft2 (forum, secondary); CdA 0.66 m2 is consistent with 149 mph on 220 bhp.",
  "estimates": "CG height, inertia, wheel rates (1.45/1.55 Hz), dampers, brake torques and proportioning, LSD bias, engine friction and pumping, alignment, turning circle, launch and shift points."
 },
 "calibration_open": "Skidpad target is from a 150 ft diameter pad, not 300 ft. 70-0 is derived. The engine curve shape is an estimate between two published points. Spring and bar data not found."
}
```
