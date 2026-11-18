# Recording your car for OSDS

This guide shows how to record your own car on a phone so that OSDS plays its real sound. It was written for a 2001 Miata, but it works for any car with a tachometer.

The game plays a recorded engine the way racing games do. It uses short loops, each recorded at one steady engine speed, with the throttle open and with it closed, inside the car and outside it. When you drive, the game blends the two loops nearest to the engine's speed. Each recording needs only a small pitch change, so it keeps its real character. To build the loops, you hold the engine at a steady speed for a few seconds at each step.

You need about 45 minutes, a helper for the outside recordings, and a quiet place. A second person is also safer on the road.

## What you need

- A phone with a voice recorder app that can **turn off automatic gain** (also called AGC or auto level). This matters most. If the app changes the level on its own, the engine will sound equally loud at 2000 and 6000 rpm.
  - iPhone: Voice Memos has no level control. Use an app with a manual input gain, for example ShurePlus MOTIV or Rode Reporter, and set the format to WAV or "lossless".
  - Android: Use an app with "AGC off" or "manual gain", for example RecForge II or ASR, set to WAV, 44.1 or 48 kHz.
- A soft foam windscreen for the phone's microphone, or a sock. Wind noise ruins outside recordings.
- Gaffer tape or a phone mount to hold the phone still.
- A notebook, or a second phone, to note what each file is.

## Set the level once

1. Put the phone where it will record (see below).
2. Rev the engine once to 6500 rpm in neutral and watch the app's meter.
3. Set the gain so the loudest moment peaks around **-6 dB** (about three quarters of the meter). It must never reach the red.
4. **Do not touch the gain again** for that position. Write the setting down. Use the same setting for every step from the same position, so the game hears the engine get louder with revs.

Set the level once for the cabin and once for the outside. The two positions can use different gain settings.

## Where to put the phone

- **Inside (cabin):** Tape the phone to the centre of the dashboard or the top of the passenger headrest, with the microphone facing up. Close the windows and the roof. Turn off the fan, the radio and the indicator.
- **Outside (exterior):** Hold or tape the phone about **1 m behind the car and 50 cm to one side of the exhaust tip**, at the height of the tip. Do not point it into the exhaust stream. The car must not move for the outside recordings. Record the neutral steps parked. For the outside "on" sounds, see the note under Part 2.

## Part 1: steady speeds in neutral (parked)

Recording in neutral gives the clean, light-load sound. It is also what the game plays when you lift off the throttle.

For each step below:

1. Start recording.
2. Bring the engine to the speed and **hold it steady for 6 seconds**. Small wobbles are fine; the script measures the real speed.
3. Let it return to idle.
4. Stop recording. Name the file right away (see Naming).

Steps: **idle**, 1500, 2000, 2500, 3000, 3500, 4000, 4500, 5000, 5500, 6000, 6500 rpm.

Do the whole list twice, once from inside and once from outside. Let the engine rest at idle for 20 seconds between the high steps. Do not hold it at the rev limiter.

A free-revving engine in neutral is under very light load. The script files these recordings as **"off" (throttle closed / light load)**.

## Part 2: under load, on a quiet road (cabin)

Under load the engine sounds deeper and rougher. This is the "on" sound.

You need a straight, empty road where you can drive legally and safely. A helper should hold the phone and the notebook. **The driver only drives.**

1. Pick a gear where the engine can hold each speed at a legal road speed. In a Miata NB, use 3rd gear for 1500 to 3500 rpm and 2nd gear for higher speeds, only where it is safe and legal.
2. Press the throttle firmly, about half to three quarters, so the car pulls. Use the brakes lightly with your left foot, or drive up a slight hill, to keep the speed steady. If you cannot hold the speed steady safely, use a **slow, steady pull instead**: full throttle from 1500 to 6500 rpm in 3rd gear, taking 8 to 10 seconds. The script can cut loops from a pull (see Sweep below).
3. Hold each speed for 4 seconds while the helper records.

Steps: 1500, 2000, 2500, 3000, 3500, 4000, 4500, 5000, 5500, 6000 rpm, or one long pull.

**Lift-offs:** After a step at 3000, 4500 and 6000 rpm, close the throttle fully in gear and let the car slow for 4 seconds. Record each one as its own file. These give the over-run sound, including any pops.

**Outside, under load:** This is optional. The helper stands at the roadside about 3 m away while you drive past at a steady speed in 3rd gear. Record a slow pass at about 2500 rpm and one at about 4500 rpm. The game uses these to check the outside sound; the neutral recordings stay the main outside loops.

## Naming

Name each file `<where>_<load>_<rpm>` with the phone's own file ending:

- `<where>` is `cabin` or `exterior`.
- `<load>` is `on` (the engine pulling the car) or `off` (neutral or throttle closed).
- `<rpm>` is the target speed, for example `3000`. Use `850` for idle in a Miata.

Examples: `cabin_on_3000.wav`, `exterior_off_850.wav`, `cabin_off_4500.m4a`.

Name a pull `pull_cabin_on.wav` and a lift-off `liftoff_cabin_4500.wav`. The script does not read these automatically; they are cut with the sweep command below.

Put all the files in one folder.

## Make the game's loops

You need Python 3.9 or newer with numpy, and ffmpeg. On Windows, run:

```bash
winget install Gyan.FFmpeg
```

```bash
pip install numpy
```

Then cut the steady recordings:

```bash
python scripts/engine-sound/cut_loops.py steady "C:\Users\you\Miata recordings" --cylinders 4 --out data/sound/engines/nb2001 --name "2001 Miata NB (owner)" --licence "CC BY 4.0" --credit "Recorded by the OSDS project owner"
```

The script:

- measures each file's real engine speed from its firing frequency,
- picks the steadiest stretch,
- cuts a loop of whole engine cycles (about 1.2 seconds) whose end flows into its start, with a 20 ms crossfade so it never clicks,
- applies one gain to the whole bank, so the loops stay as loud relative to each other as they were recorded,
- writes `bank.json` and one WAV per loop.

To cut loops from a pull instead, use the sweep command. It follows the engine speed through the recording and evens each loop out to an exact speed:

```bash
python scripts/engine-sound/cut_loops.py sweep pull_cabin_on.wav --cylinders 4 --start-rpm 1500 --steps 1500:6500:500 --mic cabin --load on --out data/sound/engines/nb2001 --append
```

## Use it in the game

Add one line to the car's `engine_sound` block in `Content/Vehicles/CarPresentation.json`:

```json
"engine_sound": { "layout": "i4", "recording": "data/sound/engines/nb2001" }
```

Then start the game. The log says `Engine sound playing (recorded loops, 44100 Hz, cabin pickup)`. If a file is missing or broken, the game logs why and plays the synthesized engine instead.

## Check it

```bash
python scripts/engine-sound/cut_loops.py spectrogram pull_cabin_on.wav --cylinders 4 --start-rpm 1500 --png pull.png
```

This draws a spectrogram with the firing frequency marked. The marked line should sit on the brightest stripe. The game's test `OSDS.Audio.CarVoices` checks the same thing for every car.

## Problems

- **Every loop is equally loud.** Automatic gain was on. Record again with it off.
- **"measured 1500 rpm, far from 3000".** The cylinder count is wrong, or the tachometer was misread. Check `--cylinders`.
- **A loop sounds rough or has a thump.** Something else was audible in the steadiest stretch, such as a door or a car passing. Record that step again.
- **Wind roar outside.** Use a foam cover and stand the phone behind the car, not beside the exhaust stream.
