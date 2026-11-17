"""Writes data/cars/<id>.json for the cars added in 2.1 (MINI Cooper S F56 and Camaro IROC-Z).

Same method as make_traffic_cars.py (published data first, targets from one instrumented test,
labelled estimates, derive() for the body-box numbers); kept apart so re-running it never touches
the earlier cars' hand-calibrated files. Research and sources: lab/cars/mini-research.md and
lab/cars/camaro-research.md. Run from the project root:  python scripts/simcruise-cars/make_new_cars.py
"""
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from make_traffic_cars import derive  # noqa: E402  (its module body only builds a list and writes the old cars when run as a script)

RPM = 2 * math.pi / 60
CARS = []

# --- 2015 MINI Cooper S Hardtop 2-door (F56), 6-speed manual. Model: CARLA's Mini (CC BY 4.0).
mini = {
    "_comment": "2015 MINI Cooper S Hardtop 2-door (F56, US): 2.0 L B48 turbo four, 6-speed manual, front-wheel drive. Model: CARLA Team's Mini Cooper (carla-content, CC BY 4.0), exported from Unreal to glTF.",
    "id": "mini15s", "name": "Cooper S", "make": "MINI", "body": "hatchback", "seats": 4,
    "mass_kg": 1263.0, "front_weight": 0.62, "cg_height_m": 0.53, "inertia": [1690, 1780, 540],
    "wheelbase_m": 2.495, "track_front_m": 1.485, "track_rear_m": 1.485,
    "tire_width_m": 0.195, "tire_aspect_ratio": 0.55, "rim_in": 16.0, "tyre_pressure_psi": [35.0, 35.0],
    "static_mu": 0.95, "lateral_grip_scale": 1.0, "load_sensitivity": 0.1, "drag_area_m2": 0.65,
    "tyre_model": "tmeasy", "tmeasy_peak_mu_y": 1.0, "tmeasy_slide_mu_y": 0.83, "tmeasy_peak_mu_x": 1.0, "tmeasy_slide_mu_x": 0.8,
    "wheel_rate_n_m": [35900, 26100], "bar_rate_n_m": [22000.0, 12000.0],
    "damper_ratio_rebound": [0.5, 0.5], "damper_ratio_compression": [0.3, 0.3], "damper_knee_mps": [0.12, 0.12], "damper_knee_slope": [0.45, 0.45],
    "static_camber_deg": [-0.5, -1.5], "camber_per_m": [16.0, 18.0], "caster_deg": 7.0, "toe_per_wheel_deg": 0.05,
    "steering_turns_lock_to_lock": 2.4, "parking_rack_deg": 34.0, "road_reference_deg": 31.0,
    "brake_front_nm": 2200.0, "brake_rear_nm": 800.0, "parking_brake_nm": 900.0, "parking_brake_type": "lever",
    "abs_enabled": True, "traction_control": True, "stability_control": True, "esc_understeer_deg_per_g": 2.2,
    "engine": "combustion", "aspiration": "turbo",
    "torque_curve": [[800, 150], [1000, 205], [1250, 280], [2500, 280], [4750, 280], [5200, 259], [5600, 240], [6000, 224], [6500, 195]],
    "torque_past_curve_nm": 170.0, "engine_friction_nm": 13.0, "engine_friction_per_rpm": 0.0057, "engine_pumping_nm": 8.5, "engine_pumping_per_rpm": 0.0009,
    "idle_rpm": 750.0, "idle_omega": round(750 * RPM, 1), "governor_rpm": 775.0, "min_omega_auto": 78.0, "max_omega": round(6600 * RPM, 1),
    "fuel_cut_rpm": 6600.0, "max_gear_rpm": 6500.0, "engine_inertia": 0.15, "exhaust_pops": 0.0,
    "transmission": "manual", "gears": [3.923, 2.136, 1.393, 1.088, 0.892, 0.756], "reverse_ratio": 3.55, "final_drive": 3.588,
    "driveline_efficiency": 0.9, "shift_seconds": 0.25, "upshift_rpm": 6300.0, "downshift_rpm": 2000.0,
    "clutch_stiffness": 12.0, "clutch_capacity_nm": 420.0, "clutch_back_nm": 400.0, "launch_rpm": 1500.0, "launch_rpm_per_throttle": 2000.0,
    "layout": "fwd", "front_diff_bias": 1.0, "slip_ratio_falloff": 0.1,
    "target_0_60_s": 6.6, "target_70_0_ft": 172.0, "target_skidpad_g": 0.91,
    "model_scene": "data/art/traffic/minif56-carla/scene.gltf", "model_rig": {"fit": True},
    "driver_eye": [-0.33, 1.18, 0.06],
    "sources": {
        "mass": "2,785 lb, 2015 Cooper S manual (KBB). Split 62/38 and CG 0.53 m are estimates.",
        "engine": "MINI US: 189 hp at 4700-6000 rpm, 207 lb-ft at 1250-4750 rpm; overboost 221 lb-ft not modelled. Curve outside the plateau is an estimate.",
        "gearing": "GS6-59SG ratios and 3.588 final drive from the NAM forum (secondary); 6th checked against an owner report of 2800 rpm at 75 mph. Reverse is an estimate.",
        "brakes": "294 mm vented front, 259 mm solid rear (Brembo catalogue). Torques are estimates. Rear share 0.27 exceeds the ideal above 0.5 g; EBD in ABS/DSC holds the rear, as on the real car.",
        "diff": "Open differential with brake-based EDLC (MINI). EDLC is not modelled; traction control stands in.",
        "steering": "14.2:1 Servotronic EPS, 10.8 m circle (MINI media info); 2.4 turns from the forum (unverified).",
        "aero": "Cd 0.31 x 2.09 m2 = 0.65 m2 (MINI media information 03/2015).",
        "tyre_pressure": "35/35 psi for 195/55R16 run-flats (tirepressure.com, secondary).",
        "calibration": "Car and Driver 2014 Cooper S Hardtop: 6.6 s, 172 ft, 0.91 g (Yahoo reprint; transmission not confirmed).",
        "estimates": "Weight split, CG, inertia, springs, bars, dampers, alignment, brake torques, clutch, ESC gradient, tyre curve, driver eye.",
        "research": "lab/cars/mini-research.md"},
}
CARS.append(derive(mini, 1.727, 1.414, 3.85, (1.5, 1.7), 36.0, 0.17))

# --- 1987 Chevrolet Camaro IROC-Z 5.7 (L98, 700R4). The data is the research file's JSON block
# (Road & Track, October 1986, with each figure's source); the model is Tony's 1985 IROC-Z
# (Sketchfab, CC BY 4.0, made in Blender): the 1985-87 body is the same apart from the 1986-on
# third brake light, which the model lacks.
here = os.path.dirname(__file__)
with open(os.path.join(here, "..", "..", "lab", "cars", "camaro-research.md"), encoding="utf-8") as f:
    text = f.read()
camaro = json.loads(text.split("```json", 1)[1].split("```", 1)[0])
camaro.update({
    "_comment": camaro["_comment"] + " Model: Sketchfab '1985 Chevrolet Camaro IROC-Z' by Tony (tonyferello), CC BY 4.0.",
    "name": "Camaro IROC-Z 5.7", "make": "Chevrolet", "exhaust_pops": 0.0, "parking_brake_type": "foot",
    "static_mu": 0.95, "lateral_grip_scale": 1.0, "load_sensitivity": 0.1, "slip_ratio_falloff": 0.1,
    "model_scene": "data/art/traffic/camaro85-c6d20cc3/scene.gltf", "model_rig": {"fit": True},
    "driver_eye": [-0.38, 1.05, 0.1],
})
for key, value in {"bar_rate_n_m": [26000.0, 9000.0], "damper_ratio_rebound": [0.45, 0.45], "damper_ratio_compression": [0.28, 0.28],
                   "damper_knee_mps": [0.12, 0.12], "damper_knee_slope": [0.45, 0.45], "toe_per_wheel_deg": 0.05,
                   "road_reference_deg": 30.0, "upshift_min_omega": round(0.95 * 4800 * RPM, 1), "clutch_stiffness": 12.0}.items():
    camaro.setdefault(key, value)  # estimates where the research gives none (class values, as the other cars)
camaro["sources"]["research"] = "lab/cars/camaro-research.md"
camaro["calibration_open"] = (camaro.get("calibration_open", "") + " 0-60: the bench floors the throttle from rest, and 434 N m through the 1.8:1 converter "
    "and 3.06 first spins the 245/50R16 rears (slip over 1 to 33 mph), so the bench reads about 8.6 s; Road & Track's 6.8 s was a "
    "driver feeding the throttle in. The IROC-Z had no traction control, so the car keeps none; a player who eases on gets the real launch.").strip()
CARS.append(derive(camaro, 1.85, 1.27, 4.877, (1.6, 1.7), camaro.get("unsprung_corner_kg", 50.0), 0.17))
for key in ("collision_box", "collision_box_y", "wheel_rate_n_m", "reference_corner_load_n", "inertia"):
    if key in camaro:
        CARS[-1][key] = camaro[key]

# Calibration on the CarTrack bench (SimCruise.Vehicle.CarTrack.<id>): only estimated values move.
CALIBRATION = {
    "mini15s": {"static_mu": 0.97, "lateral_grip_scale": 1.0, "launch_rpm_per_throttle": 2000.0, "shift_seconds": 0.2,
                "tmeasy_peak_mu_x": 0.92, "tmeasy_peak_mu_y": 1.07, "upshift_rpm": 6450.0,
                "target_0_60_rollout": True},
    "camaro87": {"static_mu": 0.95, "lateral_grip_scale": 1.0, "launch_rpm": 1200.0, "launch_rpm_per_throttle": 600.0,
                 "tmeasy_peak_mu_x": 0.9, "tmeasy_slide_mu_x": 0.74, "tmeasy_peak_mu_y": 1.0, "tmeasy_slide_mu_y": 0.85},
}
for car in CARS:
    car.update(CALIBRATION.get(car["id"], {}))
CALIBRATED = {key for values in CALIBRATION.values() for key in values}

root = os.path.join(os.path.dirname(__file__), "..", "..", "data", "cars")
for car in CARS:
    path = os.path.join(root, car["id"] + ".json")
    if os.path.exists(path):  # the tyre fit owns tyre_model and tmeasy_*
        with open(path, encoding="utf-8") as f:
            old = json.load(f)
        for key, value in old.items():
            if (key == "tyre_model" or key.startswith("tmeasy_")) and key not in CALIBRATED:
                car[key] = value
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(car, f, indent=1, ensure_ascii=False)
        f.write("\n")
    print("wrote", car["id"])
