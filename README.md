# Duck

One-knob sidechain ducking, in the spirit of Kickstart. AU + VST3 + Standalone, built with JUCE 9.

- **Amount:** how far the track ducks
- **Shape:** Classic, Punchy, Smooth, Long, Breathe, Hard Cut
- **Rate:** 1/8, 1/4, 1/2 or 1 bar (bar lines follow the host's time signature)
- **Trigger:**
  - *Tempo* ducks on every beat, following the host transport (freezes when it stops)
  - *Sidechain* ducks on each hit of whatever is routed to the sidechain input (e.g. the kick),
    plays the shape once and stays at full volume until the next hit

Shares its curve engine and look with [Contour](https://github.com/matiasconcha11/contour)
(`Source/Curve.*` and `Source/LookAndFeel.*` are copies; fixes there should be copied here).

## Build and test

```
make            # Release build; AU + VST3 are copied to ~/Library/Audio/Plug-Ins/
make test       # unit tests
make validate   # pluginval at strictness 10 on the VST3 and AU
make check      # test + validate

./build/DuckTests "[.snapshot]"   # render the editor to /tmp/duck.png
```

## In Logic

Put Duck on the bass (or pad, or bus), set Trigger to *Sidechain*, and choose the kick track in the
**Side Chain** menu at the top of the plugin window.
