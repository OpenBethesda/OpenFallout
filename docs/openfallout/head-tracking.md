# Head tracking

Head-tracked sound keeps a sound in place in the world when the player turns the head. Headphones that report motion
tell the engine how the head turns, and the engine turns the sound listener by the same amount. It is the engine's
first native bridge.

It is a setting, and it is off by default. The owner judges it by ear. If it lags, it is cut.

## The setting

- In the launcher: Settings, Audio, **Head Tracking**.
- In `settings.cfg`, category `[Sound]`: `head tracking = true`. The default is `false`.

With the setting off, the engine does not touch Core Motion at all.

## What the player needs

- A Mac with macOS 26.0 or later, and headphones that report head motion to the Mac.
- **HRTF on.** Head tracking turns the listener, and a turned listener is heard as a turned sound field only when OpenAL
  Soft renders for headphones (HRTF). Set HRTF to **On** in the launcher (Settings, Audio, HRTF), or
  `hrtf enable = 1` in `[Sound]`. The default, Automatic (`-1`), may leave HRTF off. The log says which: `Enabled HRTF
  <name>` or `HRTF disabled`. With head tracking on and HRTF not set to On, the log also gives a warning.
- macOS may ask once for permission to read the headphones' motion. The answer can be changed later in System Settings,
  Privacy & Security, Motion & Fitness.

## What it does

- The listener's direction is the camera's direction turned by the head's turn. Turn the head left by an angle and the
  sound field turns right by the same angle: a sound that was straight ahead is now heard on the right.
- **Straight ahead** is the pose of the head in the first motion sample the engine reads after tracking starts: when
  the game starts with the setting on, when the setting is switched on, when the headphones connect, and when motion
  comes back after more than half a second without any (for example headphones taken off and put on again without
  macOS saying that they went). To set it again, face the screen and take the headphones off and on again, or switch
  the setting off and on.
- A motion sample that still arrives after macOS said the headphones went is ignored; only a new connection starts
  tracking again.
- A motion sample with numbers that cannot be a rotation (not finite, or far from unit length) is dropped before it
  reaches the sound: the engine behaves as if it had not come.
- If Core Motion says headphone motion is not available when tracking starts, the engine asks again every few seconds
  while the setting is on, so headphones connected later still work.
- The position, velocity, Doppler effect, underwater filter and reverb (the EFX environment) are exactly as before.
  Only the listener's direction and up change.
- With the setting off, with no headphones, with headphones that go during play, or when no motion sample has arrived
  for half a second, the listener is exactly the camera's, as before.

## How it works

- `apps/openfallout/mwsound/headtracking.hpp` is the plain C++ interface: a motion source (`HeadMotionSource`: start,
  stop, the latest sample), the hand-over box, the tracker that keeps straight ahead, and the mathematics that turns
  the listener. A stand-in source drives the unit tests (`apps/openfallout_tests/mwsound/testheadtracking.cpp`).
- `apps/openfallout/mwsound/headphonemotion.mm` is the native bridge: the only Objective-C++ file. It uses
  `CMHeadphoneMotionManager` and the attitude Core Motion gives. It uses no personalised spatial audio profile.
- Threads: the engine's main thread starts and stops the bridge and turns the listener (in the sound manager's update).
  Core Motion calls the bridge's motion handler on a serial queue of its own, and the connect and disconnect calls on a
  thread it chooses. Both only write the latest sample into a box under a lock; the main thread reads it.
- Axes: the head frame is the camera frame of the engine: x towards the right ear, y out of the face, z up. Apple's
  figure of the axes of motion-capable headphones shows the same three axes, so the bridge passes Core Motion's
  quaternion through unchanged. The engine's world (x east, y north, z up) and OpenAL are both right-handed, and the
  listener's direction and up go to OpenAL in world axes, as before.
- The sound manager updates the listener and the sounds at most 30 times a second, as it did before. While headphones
  deliver motion, it also sets the listener's direction and up every frame between those updates, so head tracking
  adds no wait for the next update. With the setting off or no headphones delivering, nothing is set between updates.

## Log lines

- `Head tracking on: waiting for headphones that report motion` and `Head tracking off`: the setting.
- `Head tracking: reading headphone motion through Core Motion (permission ...)`: the bridge started, with macOS's
  answer so far.
- `Head tracking: Core Motion says headphone motion is not available now; trying again ...`: once, when the bridge
  starts waiting. `Head tracking: headphone motion is available now`: once, when it starts after waiting.
- `Head tracking: headphones that report motion connected` and `Head tracking: headphones disconnected`.
- `Head tracking: motion arriving, the listener turns with the head` and `Head tracking: no motion from the headphones,
  the listener follows the camera`.
- `Head tracking: straight ahead set to the pose of the headphones now`.
- At debug level (`OPENFALLOUT_DEBUG_LEVEL=DEBUG`), every ten seconds: `Head tracking: <n> samples per second`.

## Not confirmed

- Which headphone models report motion to a Mac. Apple's figure shows AirPods Pro and AirPods Max.
- Whether macOS asks for permission, and when. The app's property lists carry the motion usage description
  (`NSMotionUsageDescription`), without which macOS stops an app that starts headphone motion. The hardened runtime has
  no entitlement for motion, so none is added.
- The direction of the axes and of the attitude on real headphones. The listening check tells which, and each wrong
  outcome has its own fix in the bridge (see "What to report" below).
- The delay from a head turn to the sound through Core Motion, the frame, OpenAL Soft's mixing and the headphones'
  wireless link. The wireless audio delay of the headphones is in what the owner hears, the engine cannot remove it,
  and it is usually larger than every delay inside the engine.
- Whether Core Motion's answer that headphone motion is available depends on headphones being connected.
- Whether macOS sends connect and disconnect when the headphones leave the ears, and whether another app's spatial
  audio moves straight ahead.

The real device path is untested until the owner's listening check.

## The owner's listening check

1. Switch macOS's own Spatial Audio and its head tracking **off** for the game: while the game plays, open Control
   Center, Sound, and turn them off for the headphones. macOS can turn the stereo picture itself; with both on, the
   sound moves twice.
2. Use headphones that report motion, connected to the Mac. In the launcher, Settings, Audio: HRTF **On**, Head
   Tracking **on**. Start the game from the packaged app (not from Terminal: macOS would ask for Terminal's permission
   instead). If macOS asks for motion permission, allow it.
3. Load a save in a quiet place. Face the screen. Stand still in front of a steady sound (a radio, a fire, a
   generator), with the sound straight ahead.
4. Turn the head left by about 45 degrees. The sound must move to the right ear and stay where it is on the screen.
   Turn right: it moves to the left ear. Turn back: it is in front again.
5. Turn the head quickly and stop. Listen for the sound to trail behind the head. A short trail that is hard to notice
   passes; a trail you notice every time is lag. What you hear includes the wireless audio delay of the headphones,
   which the engine cannot remove and which is usually the largest part. So compare with head tracking in another app
   on the same headphones (a film or music with Spatial Audio): only a trail clearly longer than there is the engine's
   lag.
6. Look up and down, and tilt the head to each side. The sound must stay put. If it moves with the head or the wrong
   way, say which movement.
7. Turn the mouse (the camera) without turning the head: the sound turns as it always did.
8. Walk into water, and into a room with an echo: underwater sound and reverb are as before.
9. Take the headphones off or switch them off during play: the sound is as without head tracking. Put them on again:
   facing the screen is straight ahead again (the log says `straight ahead set` again), and a sound in front is heard
   in front, not off to one side.
10. Send `openfallout.log` with the result. The lines above show what the engine saw.

### What to report

For steps 4 and 6, say for each movement whether the sound stayed put or moved the wrong way:

- **Everything right**: nothing to fix.
- **Left and right, up and down, and tilt ALL the wrong way**: the attitude runs the other way. The fix is the
  conjugate of Core Motion's quaternion in the bridge (`headphonemotion.mm`).
- **Only up and down and tilt the wrong way**, left and right right: the headphones' x and y axes point the other way.
  The fix is the axis mapping in the bridge.
- Anything else: say exactly which movement went which way.

Also say whether the trail in step 5 was longer than in the other app.

Pass: the sound stays fixed in the world in steps 4, 6 and 7 without lag beyond that of other apps, and steps 8 and 9
behave as written. Otherwise the feature is cut and milestone two closes without it.
