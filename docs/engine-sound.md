# Engine sound

How OSDS makes each car's engine sound, where the numbers come from, and how it is tested. Added in 2.2 (team 5).

## In short

- **Recorded loops when a car has them.** A car with a recording plays loops recorded at steady engine speeds, on throttle and off, inside and outside, blended by engine speed and load. This is how racing games do it.
- **A synthesized voice for every other car.** It now has the right layout for each car: inline four, 60-degree V6 or cross-plane V8. Each car also has its own exhaust colour.
- **A turbo** (spool whine, intake whoosh, the sigh or hiss when you lift) for the Mini, the CR-V and the F-150 Raptor.
- **The Model 3's motor:** its magnetic whine and gear mesh, rising with speed and torque.
- **Volume rows** in Settings: Master, Engine and World.

No car ships with recorded loops yet. No licensed recording held steady engine speeds for one of our exact cars; see [what we found](#recordings). The first bank should be the owner's own Miata, made with [Recording your car](recording-your-car.md).

## How the recorded voice plays

`FOsdsLoopBankVoice` (`Source/OSDS/Private/Audio/OsdsEngineVoice.cpp`):

- A bank is a folder `data/sound/engines/<name>/` with `bank.json` and one mono WAV per loop: its engine speed, its load (`on` or `off`) and its microphone (`cabin` or `exterior`). `scripts/engine-sound/cut_loops.py` writes it.
- Each loop is a whole number of engine cycles (720 crank degrees), so it repeats without a seam.
- At any engine speed, the two loops recorded nearest to it play together. Each is pitched by *engine rpm ÷ its own rpm*, so no loop is shifted further than to its neighbour. They crossfade at equal power along the logarithm of rpm.
- Every loop keeps running, pitched to the engine, even when silent. A loop that fades in is already in step.
- On-throttle and off-throttle sets crossfade by the delivered torque. The cabin and exterior sets crossfade over 50 ms when the camera changes.
- A missing set borrows another. An off-throttle set borrowed from the on set plays quieter and darker. A missing cabin set uses the exterior one through the cabin's low-pass filter.
- Over the rev limiter the fuel cut stutters at 14 Hz, as with the synth. A stall fades to silence.
- The loops play at 44.1 kHz. A bank that fails to load is logged, and the car plays its synth.

## The synthesized voices

`FOsdsInlineFourSynth` is the 2.0 voice, ported from the Godot game. It is still the inline four. In 2.2:

- `ConfigureEvenFiring` gives any evenly firing layout: 6 events per engine cycle for a V6, 8 for a V8. Each event sounds through its bank's pipe. A 60-degree V6 alternates banks, so it has energy at engine order 1.5 (rpm ÷ 40 Hz) below its firing order (rpm ÷ 20 Hz). That is the V6 growl.
- `FTone` gives each car its own muffler body resonance, pipe rasp and level, cam lope at idle, and cabin and exterior brightness. Its cylinder spread makes each cylinder's pulse a little different. Real engines have this variation, and it puts energy at every half order at idle. The 2.0 V8 voice had none. It was measured against the reference recordings and moved the Camaro from 14.4 to 11.7 dB median error, and the C8 from 10.9 to 8.3 dB.

| Car | Layout | Firing order and banks | Tone (all estimates) |
| --- | --- | --- | --- |
| Miata NB (three) | inline four | 1-3-4-2 | the 2.0 voice, matched to the TurboTheSergal recordings |
| Mini Cooper S | inline four, turbo | 1-3-4-2 | body 200 Hz, rasp 2.2 kHz; spool from 1300 rpm |
| CR-V | inline four, turbo | 1-3-4-2 | quiet (rasp 0.4), dark cabin; spool from 1700 rpm |
| F-150 Raptor | 60° V6, twin turbo | 1-4-2-5-3-6, banks alternate | body 160 Hz; spool from 2500 rpm |
| Odyssey | 60° V6 | 1-4-2-5-3-6, banks alternate | quiet (rasp 0.5) |
| 1965 Mustang 289 | cross-plane V8 | 1-5-4-2-6-3-7-8 | deep body 140 Hz, soft rasp, a little lope |
| Corvette C8 LT2 | cross-plane V8 | 1-8-7-2-6-5-4-3 | bright: body 210 Hz, rasp 2.4 kHz at 1.4 |
| Camaro IROC-Z L98 | cross-plane V8 | 1-8-4-3-6-5-7-2 | muted: body 160 Hz, rasp 0.6, darker |
| Grand Cherokee SRT8 | cross-plane V8 (6.4 HEMI) | 1-8-4-3-6-5-7-2 | body 175 Hz, rasp 1.1 |
| Model 3 | electric | 3 pole pairs, 54 slots | see below |

The Grand Cherokee in the game is the SRT8, which has a 6.4 L HEMI V8, not a V6. The V6 voice therefore goes to the Odyssey and the Raptor, whose EcoBoost engine is a V6.

## Turbo

`FOsdsTurboSynth`:

- **Shaft speed** follows the boost that the pedal and engine speed ask for. There is none at idle; full boost comes from the car's `spool_rpm`. It rises over `lag_s` and falls more slowly.
- **Whine:** a tone at `whine_hz` × (0.2 + 0.8 × shaft speed), with a soft second harmonic. Its level rises with the square of shaft speed and with the pedal.
- **Whoosh:** band noise around 1.8 kHz with flow.
- **Lift-off:** closing the throttle on boost vents the charge. A stock recirculating valve gives a soft, dark sigh (900 Hz, 0.22 s). An atmospheric valve gives a bright hiss (2.6 kHz, 0.35 s). With no valve, the compressor surges in an 18 Hz flutter.
- It is quieter in the cabin (× 0.6). A twin turbo is about 2 dB louder.

## The Model 3's motor

`FOsdsElectricMotorSynth` plays these orders of the motor shaft's rotation:

- the torque ripple at 6 × the electrical frequency (order 18 for 3 pole pairs), the classic motor whine,
- the electrical orders 6 and 36,
- the slot-pass order (54),
- the pinion's gear mesh (23 teeth, an estimate) and its second harmonic.

The gear mesh grows most with torque, in drive or in regeneration. Orders above 45 % of the sample rate are left out. The motor's speed comes from the wheels through the 9.04:1 reduction.

## Recordings

We searched Freesound for CC0 and CC BY recordings of each car (18 November 2026). We downloaded only the public preview files, which need no account. What exists:

- **Miata:** idle only, inside and outside (TurboTheSergal, CC BY 4.0). The car's year and exhaust are unknown.
- **Mini Cooper S 2019:** a 54-second contact-microphone drive (TheLittleCrow, CC0). It is clean, but it picks up the engine block, not the exhaust. Tracking its engine speed through gear changes jumped past the 6500 rpm redline, so loops cut from it would be wrong. We did not ship them.
- **Corvette C5 (LS1) and a 1974 Camaro:** idle and blips from the same engine families as the C8 and the L98 (CC0). These are useful as references, but not as loops.
- **Turbo blow-off:** a drive-by (CC0).
- **Nothing usable** for the Raptor's EcoBoost, the Odyssey, the CR-V, the Grand Cherokee SRT8 or the Model 3's motor.

Racing-game loops need steady engine speeds from idle to the redline. None of these recordings has them. So no car ships with loops yet. The recordings serve as references (order levels in `data/sound/reference/`) and for listening. The owner's own Miata recordings are the first real bank; see [Recording your car](recording-your-car.md).

## Tests

Run them with `scripts\check.ps1 -Filter OSDS.Audio`.

| Test | What it checks |
| --- | --- |
| `OSDS.Audio.InlineFour`, `CrossPlaneV8`, `Limiter`, `InlineFourSweep` | the 2.0 voice; these are unchanged |
| `OSDS.Audio.LoopBank` | Between two loops there is one firing line at rpm ÷ 30, not two. The sound gets louder with revs and with load. A 1000 to 6000 rpm sweep has no clicks. Borrowed sets work. A stall goes silent. |
| `OSDS.Audio.Turbo` | There is no boost off idle and the turbo spools at 4000 rpm. The whine is at the shaft's frequency. Lifting off makes a hiss. |
| `OSDS.Audio.ElectricMotor` | The order-18 whine and the gear mesh are present. It is louder pulling than coasting and rises with speed. |
| `OSDS.Audio.V6` | The firing frequency is rpm ÷ 20. Order 1.5 is present with alternating banks and absent with equal ones. |
| `OSDS.Audio.CarVoices` | **The spectrogram check for every car.** At 1500, 3000 and 4500 rpm, the firing line (rpm × events ÷ 120) stands 6 dB over the other half orders. Each step is louder than the one below. Throttle on is louder than off. Where `data/sound/reference/<car>.json` exists, the order levels are compared with the recording's (median error under its tolerance). |
| `OSDS.Audio.CarSweeps` (editor) | It writes every car's pull, lift and limiter, inside and outside, to `Saved/Audio/<car>-<view>.wav`. This is the A/B material. |

Results on 18 November 2026: every car passes. Median order-level errors against the references are 6.7 dB for the NB Miatas (exterior idle), 8.3 dB for the C8 and 11.7 dB for the Camaro. The Miata's cabin idle is 3.4 dB from its recording (`OSDS.Audio.InlineFour`). The family references (C5 for the C8, the 1974 Camaro for the L98) use a 12 dB tolerance. They are the same engine family, not the same car. The tolerance should drop to about 4 dB once each car has its own recording.

## Blind A/B for the owner

The 2.2 target: in a blind A/B, the owner picks the real engine less than 60 % of the time.

1. Run the editor tests once (`scripts\check.ps1 -Filter OSDS.Audio`) so `Saved/Audio/` has the game's renders.
2. Make ten pairs from a real recording and the game's render at the same engine speed. For example, the Miata at idle, outside:

   ```bash
   python scripts/engine-sound/ab_test.py make --real "Miata Start and Stop Exterior.wav:2:4.5" --game Saved/Audio/nb2001-outside.wav:0.3:1.4 --trials 10 --out ab-miata
   ```

3. Listen to each pair on headphones. Write A or B on its own line in `answers.txt` for the clip you think is real. Do not open `key.json`.
4. Score it:

   ```bash
   python scripts/engine-sound/ab_test.py score ab-miata answers.txt
   ```

The clips are levelled to the same loudness, so loudness gives no clue. Use the owner's own recordings as the "real" side when they exist; they are the fairest test. For now, expect the synth to fail this test above idle. Recorded loops are what will pass it.
