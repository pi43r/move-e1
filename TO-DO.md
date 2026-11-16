# To do

## Hardware tests

Run these on a Move with build 0.3.9. Tick each one that works.

**Loading and switching**
- [ ] E-1 opens from Tools after installing over a running Schwung.
- [ ] Hold Back switches to Move and E-1 keeps playing.
- [ ] Coming back (Tools menu or the Tools shortcut) restores the screen and lights.
- [ ] Shift + Back quits E-1 and the notes stop.

**Lights**
- [ ] Muted and playing tracks are easy to tell apart at a glance.
- [ ] Selected track is clearly brighter than the others.
- [ ] Hits flash white without looking busy during a full groove.
- [ ] Knob LEDs: 1 to 4 and 5 to 8 differ, the last one touched is brighter.

**Left buttons, Mute, Temp, Freeze**
- [ ] Each left button: hold works, double press keeps it up, one more press lets go.
- [ ] Mute: hold + pads applies on release. Double press keeps Mute on.
- [ ] Sample (Temp): changes undo on release. Double press keeps it on. Capture keeps the changes.
- [ ] Loop (Freeze): all tracks freeze. Step row sets the length. Track pads take tracks in and out.
- [ ] Freeze with held track pads freezes only those. Double press keeps it on.

**Cycles**
- [ ] Footer boxes match what plays.
- [ ] Hold Cycles + pad enters edit, the cycle loops, one press of Cycles leaves.
- [ ] Edits in one cycle stay in that cycle.

**Single steps**
- [ ] Shift + step picks a step, Repeats on it ratchets only that hit.
- [ ] Several steps picked with Shift held change together.
- [ ] Red steps survive saving and loading a set. Delete + red step clears.

**Pitch**
- [ ] Footer shows the octave and pool notes. Octave pads light toward pool notes.

**Patterns and menu**
- [ ] Capture then a slot saves. Back cancels.
- [ ] Pattern + pad queues on Quant, a second tap switches at once.
- [ ] Menu: Save, Save as, Load, Rename, Settings all work. Delete + click in Load deletes a set.

**Web page**
- [ ] With E-1 open, http://move.local:7700/remote-ui#tool shows the E-1 page.
- [ ] Track name, type, channel, output and mute changes reach the Move and stay after a restart.
- [ ] CC lane numbers change what a CC track sends.
- [ ] Set and pattern names change on the Move and are kept when the set is saved.
- [ ] Edits on the Move show up on the page.

**Outputs and clock**
- [ ] Output Move plays Move's tracks on channels 1 to 4.
- [ ] Output Computer reaches a DAW over USB-C.
- [ ] Clock Move, Internal and Computer each stay in time. ClkOut drives a DAW.

## Features

- [x] Show steps with their own values while Shift is held, also when the step has no hit.
- [ ] Per-step values for CC lanes.
- [ ] Free division (Shift + Div in fine steps).
- [ ] Track presets (removed from the menu in 0.3.3).
- [ ] Incoming MIDI notes: record into the pool or transpose.
- [ ] Web page: edit every knob value for a track, and load or save sets.
- [ ] Map an external controller's CCs to E-1 parameters (to be decided).

## Other

- [ ] Turn on GitHub Pages: Settings, Pages, Deploy from branch, `main`, folder `/docs`.
- [ ] Tag the first release (`v0.3.7`) so the Releases link works.
- [ ] Submit E-1 to the Schwung catalog once the hardware tests pass.
