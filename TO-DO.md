# To do

## Hardware tests

Run these on a Move with build 0.4.0. Tick each one that works.

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

**0.4: hits, redo, retrigger, Loop**
- [ ] Tap the selected track's pad: one hit sounds, also while stopped and when muted. A CC track sends its lane values.
- [ ] Shift + Undo redoes. A new edit clears the redo steps.
- [ ] Random + step marks a retrigger step (pink). Random, groove, phrase and style restart there.
- [ ] Delete on the Repeats pads stops running repeats. Length pink pad (or a tap on Delete) jumps to a random start, Shift + pink pad goes back in line.
- [ ] Random + Plus/Minus sets Loop. With a loop the same chances and random values come round. Random + jog click shows REROLL and gives new values.

**0.4: dice, density, auto roll**
- [ ] Random held, no knob touched: dice pads 1 to 5, 7 (All) and 8 (Back) roll and light as described. Delete + dice pad clears a line.
- [ ] Shift + knob 3 (DiceTyp) changes the rolled Notes line step by step.
- [ ] Shift + knob 4 (Density) thins hits; thinned hits stay dim on the step row.
- [ ] Auto roll (Random + lower pads, or Shift + Random + Plus/Minus) rolls on the bar count.

**0.4: song key, chord track, Follow, Split**
- [ ] Settings has Set and Track pages, switched with the jog wheel.
- [ ] Song root and scale move every Song track. Shift + scale pad and Shift + root key set the song from any track.
- [ ] Scale pads page with Plus/Minus. Yellow pads show the song's choice.
- [ ] ChordTrk: the chord track lights blue. Follow Bass, Chord and Arp (Shift + Sustain) follow its chord; followers light green and the footer names the role.
- [ ] Split sends every second hit to the partner track's channel.
- [ ] The web page shows and changes song root, scale and chord track.

**0.4: shapes and time**
- [ ] Random + Left/Right steps Source; footer shows SRC. Each shape is audible on Velocity with a random amount.
- [ ] Jitter (Random + knob 3) skips, repeats and steps back; with Loop it repeats.
- [ ] Warp (Random + knob 4) rushes or drags inside the cycle, cycles keep their length.
- [ ] Fit (Random + Shift + knob 4, or Div pad 16): 5 steps against 4 stay in time with Move.
- [ ] Dir Forward, Backward, Pendulum and Random play as named. Old sets with Up/Down load.

**0.4: steps in one cycle, CC steps, chains**
- [ ] In cycle edit, Shift + step and a knob change that step in that cycle only. The footer shows STEP n Cm, the step lights yellow in that cycle's view.
- [ ] A step edited for every cycle and again in one cycle plays the cycle value there and the every-cycle value elsewhere.
- [ ] Delete + step in cycle edit clears its values for that cycle and for every cycle.
- [ ] CC track: picked steps take their own lane values in Pulse, Change and Glide.
- [ ] Hold Pattern, hold a slot, tap two more: they play in order, each for its Length (else one bar) on the Quant lines, and loop. Chained pads bright blue, playing white, next blinking. Footer shows CHAIN while Pattern is held.
- [ ] A single tap on a slot ends the chain. The chain comes back after saving and loading the set, and after a restart.
- [ ] Stopping and starting the transport during a chain keeps it in step.

**Outputs and clock**
- [ ] Output Move plays Move's tracks on channels 1 to 4.
- [ ] Output Computer reaches a DAW over USB-C.
- [ ] Clock Move, Internal and Computer each stay in time. ClkOut drives a DAW.

## Features

- [ ] Free division (Shift + Div in fine steps).
- [ ] Track presets (removed from the menu in 0.3.3).
- [ ] Incoming MIDI notes: record into the pool or transpose.
- [ ] Web page: edit every knob value for a track, and load or save sets.
- [ ] Map an external controller's CCs to E-1 parameters (to be decided).

## Known limits

- A track holds 32 edited step rows (a step for every cycle is one row, each cycle it is edited in adds one). Further edits are ignored.
- A track's saved text must fit the 16 KB the host reads at once. Heavy step and cycle edits on one track can come near it.

## Other

- [ ] Turn on GitHub Pages: Settings, Pages, Deploy from branch, `main`, folder `/docs`.
- [ ] Tag the first release (`v0.3.7`) so the Releases link works.
- [ ] Submit E-1 to the Schwung catalog once the hardware tests pass.
