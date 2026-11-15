# Known faults, v2.0.3

Every fault the owner reported, then the test escapes and the blind reviews, each with its state: **fixed and verified** (the after-the-fix frame at the same spot and view, path), **open** (the owner lane), or **accepted** (who accepted it and why). The final test fills the last two columns; until then the state is what the lanes report. Frames: the release test folders on the PC (`D:\sc\lane-rc*\Saved\ReleaseTest\...`).

## The owner's reports

| # | Date (UTC) | The owner's words / what | Where it showed in our frames | Lane | State | After the fix (frame) |
|---|---|---|---|---|---|---|
| 1 | 11-11 | "atlas not ported" | first Unreal playtest | ui | fixed (atlas in the pause menu) | to check in the controller pass |
| 2 | 11-11 | "steering wheel didn't work" | first playtest | cockpit | fixed (rim pivot, lock from the car data) | cockpit review per car |
| 3 | 11-11 | traffic blocking roads; traffic wheels not rolling | first playtest | driving | accepted for v2.0.3: traffic is off (owner, 11-14 20:31Z) | - |
| 4 | 11-11 | "the car's driving physics don't feel real, it's all over the place" | first playtest | driving | open until the drive-real part passes | drive-real part, per car |
| 5 | 11-11 | buildings don't look real | first playtest | world | open: facades with doors and shopfronts (v2.0.3 scope) | Detroit slow look |
| 6 | 11-14 19:30Z | "cars arent following traffic" (freeway cars crossing Woodward at grade) | c1a6b76 Detroit south video 51 s (`owner-1930\south-51s.png`) | driving | accepted for v2.0.3: traffic is off | - |
| 7 | 11-14 19:30Z, 20:23Z | "top of buildings are missing": the top window row cut by the roof line, no parapet | c1a6b76 and e9db211 Detroit start frames (`release7-traffic\building-tops\`) | world | open: release-10 has building tops; to verify | Detroit slow look, same spot |
| 8 | 11-14 20:22Z | the inside mirror black at 0 mph (Detroit north approach) | e9db211 Detroit north video frames 1-18, right after a 5-min freeze (`release7-traffic\owner-2022-mirror\`) | picture | open | first second of every drive |
| 9 | 11-14 20:2xZ | the dash very dark in shade | c1a6b76 Detroit north video frame 444 (`owner-1930\tops\detroit-north\00444.jpg`) | picture | open | same spot |
| 10 | 11-14 20:28Z | "bridge was warped" (Doraville, Northcrest Road over I-285): a lumpy deck, tilted wall pieces, the Corvette stuck at 6600 rpm | e9db211 and c1a6b76 Doraville Corvette 30.3-50 s (`release7-traffic\owner-2028\`) | world | open: plan A clearance (release-12); the judge now catches it (hit, stuck, deck-not-smooth) | Doraville both ways |
| 11 | 11-14 20:15Z | the game stood still for 5 minutes at a Detroit start (the owner at the keyboard) | e9db211 Detroit north log 20:15:16-20:20:30 | release | open: cause not proven; the hang watchdog now logs the stack | every run (`longest_hang_s`) |
| 12 | 11-14 21:06Z | "yall need to optoize your testing" | - | review | in progress: "How we look", the new review method | this test |
| 13 | 11-14 23:32Z | the installer must work on any PC | dep-check: the VC++ runtime is not shipped or installed | release | open: the installer thread | dep-check 0, installer gate |

## The release-10 review (dfed34c, 2026-11-15), blind review and fresh eyes

Frames under `C:\sc\final\release10-review\`. Each gets an after frame at the same spot in the final test (the same place, start and car; the frame at the same drive second or path metre).

| # | Fault | Where it showed (release-10) | Found by | Lane | State | After the fix (frame) |
|---|---|---|---|---|---|---|
| R1 | Miata automatic: 19 mph in D1 at 851 rpm (below idle in gear); first held to 6789 rpm at 31 mph; gear hunting D5-D4-D2-D4 at 35 mph | dragon-oldcrowns\review\006-006.3s-timed.png (sidecar rpm 851, gear 1); dragon\review\030-031.3s-timed.png; dragon\motion\strip-01.png | blind review, checked against the sidecars; the strip | driving | open | Dragon, same seconds |
| R2 | The road visible through the Miata door, under the side mirror | dragon-oldcrowns\review\006-006.3s-timed.png (bottom left) | blind review, checked by eye | cockpit | open | Dragon, same second |
| R3 | Jagged black stair-step shadows over the Miata binnacle and column; the cabin flips between clay-grey and near-black within 0.5 s | dragon\review\026-027.2s-timed.png; dragon\motion\strip-01.png | blind review, fresh eyes, the strip | picture | open | Dragon strip |
| R4 | The Corvette's digital cluster digits garbled | jasper-ar\review\ (frames 4-6 of the blind set) | blind review | cockpit | open | Jasper, same seconds |
| R5 | Palm fronds dithered, comb-like | ocean-drive\review\ (all) | blind review | picture | open | Ocean Drive start |
| R6 | Parked cars standing into the driving lane on Ocean Drive, very close at 28 mph | ocean-drive\review\ (blind frames 3, 6-10) | blind review | world | open | Ocean Drive, same seconds |
| R7 | Blocky, soft rear-view and side mirrors; the rear-view mirror blank or dark in some frames (d50a90e lowers the mirror to 768x320 to save GPU) | dragon\review\, ocean-drive\review\ | blind review | picture | open | every place |
| R9 | The engine speed does not follow gear and road speed in automatic mode: the Mustang at 36 mph in third at 4672 rpm (its 2.78/1.93/1.36/1.00 box and 3.00 axle give about 1,900); the Miata 23 mph in D1 at 1350 rpm in one frame and 22 mph in D1 at 4950 in another; below idle in gear | delta-ut\review\006-006.2s-timed.png (sidecar), ankeny-ia blind frames 2 and 9 | blind review 2, checked against the sidecars and the car data | driving | open (with R1) | the same seconds |
| R10 | The Mustang cockpit: the door mirror is a faceted black disc with no glass; a ghosted double edge on the left A-pillar; the rear-view mirror arm has no mirror | delta-ut\review\ (every frame) | blind review 2 | cockpit | open | Delta start |
| R11 | Downtown towers are untextured grey boxes without windows, the focal point of the Detroit view | detroit-slow\review\ (every frame) | blind review 2 | world | open (far facades in the candidate?) | Detroit slow, same seconds |
| R12 | Traffic signals look unlit or barely lit by day | ankeny-ia, detroit-slow | blind review 2 | world | open | Ankeny, Detroit |
| R13 | Dragon foliage smeared into long streaks at the top left; black rectangular posts on the hillside | dragon\review\ (blind frames 1-2) | blind review 2 | world | open | Dragon start |
| T1 | Test robot (not the game): the reviewer saw the double yellow under the left side and the wheel turned about 44 deg on straights (Mustang; 2 deg at the road wheels, its 22:1 ratio); the robot passes red signals (signals are not in the robot) and the slow look creeps at 5 mph by design | delta-ut, ankeny-ia, detroit-slow | blind review 2 | review (robot) | checked: NOT PROVEN for the centre line. The pose log puts the car 1.8 m right of the centreline at the start of every run (the inner lane), so from the driver's seat the double yellow shows under the left mirror as in a real car; the 2 deg of steer on straights is the robot's correction; to check the lane later in a drive | - |
| R8 | The Corvette minimap over the centre screen (the screen shows through it); the HUD speed digits fade to grey | jasper-ar\review\014-014.5s-timed.png; detroit-slow\review\009-009.3s-timed.png | blind review, fresh eyes | ui | maybe fixed in the candidate (3b49f7e: the map at the top left, no speed fade): before and after frames at the same spot in the final test | Jasper 14.5 s, Detroit slow 9.3 s |

## Test escapes and blind-review findings

See [test-escapes.md](test-escapes.md): building tops, freeway traffic at grade, the pad menus' top row, wheel and gauges, and the 15 findings of the blind review of c1a6b76 (terrain shards beside the motorway, binnacle streaks, the cabin clay-grey on the motorway, a floating street light, road markings on sidewalks, white windows by day, untextured towers, the minimap over the clock, fading HUD digits, blocky mirrors, the wheel badge, first gear held at full throttle). Each gets a row here with its state when the final test has checked it.
