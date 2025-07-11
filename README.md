# Dynamic Range Destroyer (DRD) LV2 plugin

© 2025 David Flater<br>
[GPLv3](https://www.gnu.org/licenses/gpl-3.0.html)

## Overview

Dynamic Range Destroyer (DRD) is an audio compression filter that aims to
remove big changes in volume from an audio stream.  It supports mono, stereo,
6-channel, and 8-channel streams, and it preserves the balance among
channels.

## Build and install

The Makefile takes two variables:

```
# Prefix under which the LV2 include files are found
LV2PREFIX ?= /usr
# Prefix to install the plugin to
DRDPREFIX ?= $(LV2PREFIX)
```

Example installation to home directory:

```
$ DRDPREFIX=$HOME make install
gcc -I/usr/include -O2 -fPIC -fvisibility=hidden -shared -lm -o DRD.so DRD.c
install -d /home/dave/lib/lv2/DRD.lv2
install DRD.so /home/dave/lib/lv2/DRD.lv2
install -m 644 manifest.ttl DRD.ttl /home/dave/lib/lv2/DRD.lv2
$ export LV2_PATH=$HOME/lib/lv2
$ lv2ls
http://flaterco.com/lv2/DRD#6ch
http://flaterco.com/lv2/DRD#8ch
http://flaterco.com/lv2/DRD#mono
http://flaterco.com/lv2/DRD#stereo
```

## Controls

### Attack

Range (0 to ∞) s<br>
Default 0.01 s

An indication of the rate at which DRD lowers the gain in response to
increases in the volume of the input.  It is an exponential process.  The
attack is the time for the gain to respond to 1 − 1/e or approximately 63% of
an increase of input level.

### Decay

Range (0 to ∞) s<br>
Default 0.5 s

An indication of the rate at which DRD increases the gain in response to
decreases in the volume of the input.  It is an exponential process.  The
decay is the time for the gain to respond to 1 − 1/e or approximately 63% of
a decrease of input level.

## Example uses

The environment variable LV2_PATH must be set correctly for the plugin to be
found.

```
$ ls ~/lib/lv2
DRD.lv2
$ export LV2_PATH=$HOME/lib/lv2
```

DRD can be installed as a global system audio filter for Linux with PipeWire.
See
[https://flaterco.com/kb/audio/pipewire/volume.html](https://flaterco.com/kb/audio/pipewire/volume.html)
for details and an installation kit.

If [FFmpeg](https://ffmpeg.org/) was configured with --enable-lv2, you can
use DRD through a command line option similar to `-af
'lv2=p=http\\://flaterco.com/lv2/DRD#stereo:c=attack=0.01|decay=0.5'`.  Be
sure to match the plugin URI to the number of channels of the input file
(mono, stereo, 6ch, or 8ch) to avoid surprising behavior.

If [mpv](https://mpv.io/) is able to find the run-time dependency
libavfilter, you can use DRD through a command line option similar to
`--af='lavfi="lv2=p=http\\://flaterco.com/lv2/DRD#stereo:c=attack=0.01|decay=0.5"'`.
Again, please match the plugin URI to the number of channels of the input
file to avoid surprising behavior.

## Technical details

DRD is functionally similar to the compand filter of [FFmpeg version
7.1.1](https://github.com/FFmpeg/FFmpeg) (libavfilter.so.10.4.100) with fixed
parameters points=-100/-100|-50/-15|0/-15 soft-knee=1 gain=0 volume=-15
delay=0.  The major differences are (1) balance is preserved (the same gain
is applied to every channel) and (2) the transfer function is different (see
plots below).

![The transfer functions of DRD and compand -100/-100|-50/-15|0/-15:1:0 are plotted with input volume in dB on the x axis and target volume in dB on the y axis.  DRD:  Below -100 dB input the volume is unchanged.  Above -50 dB input the target volume is flat at -15 dB.  Between -100 and -50 dB input is a smooth curve.  Compand differs from DRD by making hard turns at the inflection points, taking a straight line between them, and having a small hook up to -14 dB output as the input level reaches 0 dB.](TransferFunctions.svg)

![DRD gain function.  The x axis is input volume in dB.  The y axis is gain in dB.  The gain rises in a curve from 0 dB at input volume -100 dB to a peak around 38 dB at input volume -57 dB, then curves downward, becoming a straight downward-sloping line that reaches -15 dB at input volume 0 dB.](Gain.svg)

The process involves a state variable herein called the floating volume
level.  It follows the volume of the input in a general sense, effectively
finding the average over a short time interval.

The following steps are done for each sample:

1. Estimate the overall volume level of the input as the maximum level of any channel (absolute value).
2. Update the floating volume level using a fraction of its difference from the volume level of the input.  The fraction is derived from the attack or decay parameter (as applicable) as 1 − e^(−1/(parameter × sample rate)).
3. Evaluate the gain function using the floating volume level as input.
4. Apply that gain to every channel.

Clipping is not prevented.  Loud pops will occur and it is intended that they
be clipped.  The plot below shows the worst-case scenario where the input
level instantaneously transitions from 0 (minimum) to 1 (maximum) at time 0
and stays there.  Since the floating volume level sweeps the range starting
from 0, it passes through the peak of maximum gain on its way to the top,
resulting in an extreme pop 38 dB beyond the limit within the first few
samples.  Negative gain is applied after ⅕ of the attack time has elapsed.

![Worst case clipping (instantaneous 0-to-1 input volume transition).  The x axis is time divided by attack ranging from 0 to 0.5.  The y axis is decibels ranging from -60 to 40, against which scale both the floating volume and the gain applied are plotted.  A thick red horizontal line at 0 dB indicates the maximum applicable gain.  The floating volume enters nearly vertically from the bottom left corner and then curves sharply to the right, nearing -8 dB at the end of the x axis.  The gain applied almost instantly shoots up to the maximum of 38 dB then decays rapidly till it crosses the 0 dB line just before x = 0.2.  At the right side of the plot it is close to -7 dB.](WorstCase.svg)

## Acknowledgments

The starting point for DRD was eg-amp.lv2 in [lv2 version
1.18.10](https://github.com/lv2/lv2).  Comments from eg-amp remain in the
source.

> A simple example of a basic LV2 plugin with no additional features.<br>
> Copyright 2006–2016 David Robillard<br>
> Copyright 2006 Steve Harris<br>
> [ISC license](https://www.isc.org/licenses/)

The attack/decay and update_volume code was copied from af_compand.c of
[FFmpeg version 7.1.1](https://github.com/FFmpeg/FFmpeg)
(libavfilter.so.10.4.100).

> Copyright (c) 1999 Chris Bagwell<br>
> Copyright (c) 1999 Nick Bailey<br>
> Copyright (c) 2007 Rob Sykes<br>
> Copyright (c) 2013 Paul B Mahol<br>
> Copyright (c) 2014 Andrew Kelley<br>
> [LGPL v2.1](https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html)

Additional LV2 infrastructure followed the example of [dpl.lv2 version
0.7.0](https://github.com/x42/dpl.lv2).

> Digital Peak Limiter<br>
> Copyright (C) 2018 Robin Gareus<br>
> [GPL v2](https://www.gnu.org/licenses/old-licenses/gpl-2.0.html) and [GPLv3](https://www.gnu.org/licenses/gpl-3.0.html)
