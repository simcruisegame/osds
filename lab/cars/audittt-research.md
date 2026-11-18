# Audi TT Coupe 3.2 quattro (8J), US 2008-2009, 3.2 VR6, 6-speed manual

Labels: **M** = measured (magazine instrumented test), **P** = published maker spec, **S** = secondary (dealer, catalogue, database, forum), **E** = estimate (method given).

## Body and trim chosen, and why
- The [CARLA catalogue](https://carla.readthedocs.io/en/latest/catalogue_vehicles/) render of `vehicle.audi.tt` shows a second-generation (8J, 2007-2014) coupe: single-frame grille, round fog lamps, 8J roof line. The 8N had a small split grille and the 8S a wide flat one. The asset names (`D:\sc\carla-content\Static\Car\4Wheeled\AudiTT`) give no year.
- I chose the **3.2 quattro with the 6-speed manual**. Of the US 8J coupes (2.0T FWD or quattro with S tronic, 3.2 quattro, TTS, TT RS), it is the only one with a full instrumented test found. It also gives the player a clutch.
- To do: check the rear of the mesh. The 3.2 has one tip on each side, the 2.0T two tips on the left (unverified, from photographs). If it is a 2.0T, use the [Audi 2011 TT 2.0T sheet](https://www.audiworld.com/wp-content/uploads/2014/03/11_TT_20_tech.pdf) (211 hp, 350 N m, 3,153 lb, P).

## Mass and dimensions
- Curb weight 3,241 lb = 1470 kg; split 59/41 (P, [Audi of America 2009 TT 3.2 spec sheet](https://www.audiworld.com/wp-content/uploads/2014/03/09TT_32-specs.pdf)). The C&D test car weighed 3,255 lb = 1476 kg at 59.4/40.6 (M). The file uses the test car.
- Wheelbase 2468 mm; track 1572/1558 mm; length 4178, width 1842, height 1352 mm (P, Audi sheet).
- CG height 0.50 m (E: 0.37 x roof height).
- Inertia [2330, 2435, 660] kg m2 (E: mini15s scaled by mass and body box).

## Engine (BUB 3.2 VR6, 3189 cm3, 84.0 x 95.9 mm, CR 11.3:1, port injection)
- 250 hp (184 kW) at 6300 rpm, 236 lb-ft (320 N m) at 2500-3000 rpm (P, Audi sheet). Firing order 1-5-3-6-2-4 (P).
- Power check: 279 N m x 6300 rpm = 184 kW.
- Curve (rpm: N m): 1000:250, 1500:275, 2000:300, 2500:320, 3000:320, 3500:315, 4000:310, 4500:305, 5000:300, 5500:295, 6000:288, 6300:279, 6800:250. The 2500, 3000 and 6300 points are P. The others are E, and none is above 250 hp.
- Redline and fuel cut 6800 rpm, idle 700 rpm (E).
- Engine braking (E, Heywood ch. 13): FMEP 1.9 bar gives 48 N m at 3000 rpm; PMEP 0.7 bar gives 18 N m.

## Driveline
- 6-speed manual (02Q) with two final drives (P, Audi sheet):
  - 1st to 4th: 3.357 / 2.087 / 1.469 / 1.088 on 4.235.
  - 5th, 6th and reverse: 1.108 / 0.912 / 3.990 on 3.273.
- The JSON uses one final drive (4.235) and scales 5th, 6th and reverse by 3.273 / 4.235 to 0.856, 0.705 and 3.084. The overall ratios do not change.
- Gearing check: a 245/45R17 has a rolling radius of about 0.318 m. mph per 1000 rpm, gears 1 to 6: 5.2 / 8.4 / 12.0 / 16.2 / 20.6 / 25.0, so 75 mph is 3000 rpm in 6th. 2nd runs out at 57 mph at 6800 rpm.
- quattro: a Haldex multi-plate coupling to the rear axle; open front axle with EDL (brake-based); open rear axle (P, Audi sheet).
- The loader has a centre differential, not a Haldex coupling. The JSON uses a front share of 0.6 and a centre bias of 3.0 (E).
- Clutch: single plate with a dual-mass flywheel (S). Capacity 450 N m (E, 1.4 x peak torque). Engine inertia 0.18 kg m2 (E).

## Tyres, brakes, controls
- Tyres: 245/45R17 on 8.5J x 17 alloys (P, Audi sheet); 18 in optional.
- Pressure 34/32 psi (S, [tirepressure.com](https://tirepressure.com/2008-audi-tt-tire-pressure), 245/40R18 on a 2008 TT). No 17 in value found, so the same is used (E).
- Brakes: 340 mm vented front discs, 310 mm vented rear discs, diagonal split circuits (P, Audi sheet).
- Brake torque per wheel 2800 N m front and 700 N m rear (E). That is a front share of 0.80.
- The ideal front share is 0.59 + 0.203 x decel: 0.69 at 0.5 g, 0.75 at 0.8 g, 0.79 at 1.0 g. So the fronts lock first up to 1.0 g. ABS with EBD is fitted.
- ESP (stability control) with ABS, ASR (traction control), EDL and a hydraulic brake assistant is standard (P, Audi sheet).
- Parking brake: a centre-console hand lever, mechanically actuated at the rear wheels (P, Audi sheet). Torque 900 N m (E).

## Steering and suspension
- Electromechanical steering with speed-dependent assistance. Ratio 16.9:1, 2.9 turns lock to lock, turning circle 10.96 m curb to curb (P, Audi sheet).
- Steering check: 2.9 x 180 / 16.9 = 30.9 deg mean lock; a bicycle model with 10.96 m and 2.468 m gives 29-31 deg. The numbers agree.
- Front: MacPherson strut with lower wishbones and tubular bar. Rear: four-link (P). Magnetic ride was optional; the base car has passive dampers.
- I did not find spring rates or bar sizes. The wheel rates of 40.0 kN/m front and 31.7 kN/m rear come from 1.6 Hz and 1.75 Hz ride frequencies (E). Caster 7 deg (E).

## Aero
- Cd 0.30 (P, Audi sheet) x frontal area 2.04 m2 (E: 0.82 x width x height) = CdA 0.61 m2. The top speed is governed at 130 mph (P), so it gives no check.

## Targets
- Car and Driver, "2008 Audi TT 3.2 Quattro", April 2007, by Erik Johnson (M, [German Car Forum repost](https://www.germancarforum.com/threads/car-driver-audi-tt-3-2-test.13651/latest)):
  - 0-60 mph 6.1 s; 1/4 mile 14.5 s at 97 mph
  - 70-0 mph 159 ft
  - 0.93 g on the 300 ft skidpad, with stability control off
  - 3,255 lb, 59.4/40.6
- Audi claims 0-60 in 5.5 s for the manual (P). The C&D time is 0.6 s slower. Calibrate to C&D and do not raise the torque.
- A 159 ft stop from 70 mph is a mean deceleration of 1.03 g.

## Open items
- The mesh rear (3.2 or 2.0T), the tyres on the C&D car, the 17 in placard.
- Spring and bar data (Audi workshop manual, ElsaWin).
- Next phase: a Haldex coupling model and EDL. These are new features.

```json
{
 "_comment": "2008-2009 Audi TT Coupe 3.2 quattro (8J), US, 3.2 VR6, 02Q 6-speed manual, Haldex quattro (approximated by a centre differential), 245/45R17. Matches the CARLA vehicle.audi.tt body (8J). Draft from lab/cars/audittt-research.md; model fields come from the fit.",
 "id": "audittt",
 "name": "TT 3.2 quattro",
 "make": "Audi",
 "body": "coupe",
 "seats": 4,
 "mass_kg": 1476.0,
 "front_weight": 0.594,
 "cg_height_m": 0.50,
 "inertia": [2330, 2435, 660],
 "wheelbase_m": 2.468,
 "track_front_m": 1.572,
 "track_rear_m": 1.558,
 "tire_width_m": 0.245,
 "tire_aspect_ratio": 0.45,
 "rim_in": 17.0,
 "tyre_pressure_psi": [34.0, 32.0],
 "tyre_model": "tmeasy",
 "tmeasy_peak_mu_y": 1.0,
 "tmeasy_slide_mu_y": 0.84,
 "tmeasy_peak_mu_x": 1.0,
 "tmeasy_slide_mu_x": 0.8,
 "load_sensitivity": 0.1,
 "drag_area_m2": 0.61,
 "unsprung_corner_kg": 40.0,
 "wheel_rate_n_m": [40000, 31700],
 "bar_rate_n_m": [24000.0, 14000.0],
 "damper": ["ratio", "ratio"],
 "damper_ratio_rebound": [0.5, 0.5],
 "damper_ratio_compression": [0.3, 0.3],
 "static_camber_deg": [-0.7, -1.4],
 "caster_deg": 7.0,
 "toe_per_wheel_deg": 0.05,
 "steering_turns_lock_to_lock": 2.9,
 "parking_rack_deg": 34.0,
 "road_reference_deg": 31.0,
 "brake_front_nm": 2800.0,
 "brake_rear_nm": 700.0,
 "parking_brake_nm": 900.0,
 "parking_brake_type": "lever",
 "abs_enabled": true,
 "traction_control": true,
 "stability_control": true,
 "esc_understeer_deg_per_g": 2.5,
 "engine": "combustion",
 "torque_curve": [[1000, 250], [1500, 275], [2000, 300], [2500, 320], [3000, 320], [3500, 315], [4000, 310], [4500, 305], [5000, 300], [5500, 295], [6000, 288], [6300, 279], [6800, 250]],
 "torque_past_curve_nm": 220.0,
 "engine_friction_nm": 21.0,
 "engine_friction_per_rpm": 0.0091,
 "engine_pumping_nm": 13.5,
 "engine_pumping_per_rpm": 0.0014,
 "idle_rpm": 700.0,
 "fuel_cut_rpm": 6800.0,
 "max_gear_rpm": 6800.0,
 "engine_inertia": 0.18,
 "transmission": "manual",
 "gears": [3.357, 2.087, 1.469, 1.088, 0.856, 0.705],
 "reverse_ratio": 3.084,
 "final_drive": 4.235,
 "driveline_efficiency": 0.85,
 "clutch_capacity_nm": 450.0,
 "layout": "awd",
 "awd_front_share": 0.6,
 "center_bias": 3.0,
 "front_diff_bias": 1.0,
 "rear_diff_bias": 1.0,
 "target_0_60_s": 6.1,
 "target_70_0_ft": 159.0,
 "target_skidpad_g": 0.93,
 "sources": {
  "mass": "Audi 2009 TT 3.2 sheet: 3,241 lb, 59/41; C&D test car 3,255 lb, 59.4/40.6 (used). CG 0.50 m is an estimate.",
  "engine": "Audi sheet: 250 hp at 6300 rpm, 236 lb-ft at 2500-3000 rpm, CR 11.3, firing order 1-5-3-6-2-4. Curve outside the published points is an estimate.",
  "gearing": "Audi sheet: 3.357/2.087/1.469/1.088 on 4.235, 1.108/0.912 and reverse 3.990 on 3.273. 5th, 6th and reverse scaled by 0.7729 to one 4.235 final drive.",
  "brakes": "340 mm vented front, 310 mm vented rear (Audi sheet). Torques are estimates; front share 0.80 is above the ideal up to 1.0 g.",
  "diff": "Haldex coupling, open front with EDL, open rear (Audi). Approximated by a 0.6 front share and centre bias 3.0 (estimate).",
  "steering": "16.9:1, 2.9 turns, 10.96 m circle (Audi sheet).",
  "aero": "Cd 0.30 (Audi) x 2.04 m2 (estimate).",
  "tyre_pressure": "34/32 psi from the 2008 TT 245/40R18 entry (tirepressure.com, secondary); 17 in value not found.",
  "calibration": "Car and Driver, April 2007, 2008 TT 3.2 quattro manual: 6.1 s, 14.5 s at 97 mph, 159 ft, 0.93 g (ESP off).",
  "estimates": "CG, inertia, springs, bars, dampers, alignment, brake torques, clutch, Haldex split, ESC gradient, frontal area, tyre curve.",
  "research": "lab/cars/audittt-research.md"
 },
 "calibration_open": "C&D tyres unknown (17 or 18 in). Haldex modelled as a fixed-split centre differential. Audi claims 5.5 s to 60; calibrate to the measured 6.1 s without raising torque."
}
```
