# Dynamic Range Destroyer LV2 plugin (DRD.lv2)

© 2025 David Flater<br>
[GPLv3](https://www.gnu.org/licenses/gpl-3.0.html)

[Note on versioning](#note)

## Overview

Dynamic Range Destroyer (DRD.lv2 or simply DRD) is an audio compressor plugin
that aims to remove changes in volume from an audio stream.  This effect is
also called auto volume levelling, volume normalization, or "stable volume."
It supports mono, stereo, 6-channel, and 8-channel streams, and it preserves
the balance among channels.

## Build and install

The Makefile takes two variables:

```
# Prefix under which the LV2 include files are found (not necessarily where
# plugin bundles are installed)
LV2PREFIX ?= /usr
# Directory into which the plugin bundle (DRD.lv2) should be installed
# For a user install say DESTDIR=$HOME/.lv2
DESTDIR ?= /usr/local/lib/lv2
```

Example installation to home directory:

```
$ DESTDIR=$HOME/.lv2 make install
gcc -I/usr/include -O2 -fPIC -fvisibility=hidden -shared -lm -o DRD.so DRD.c
install -d /home/dave/.lv2/DRD.lv2
install DRD.so /home/dave/.lv2/DRD.lv2
install -m 644 manifest.ttl DRD.ttl /home/dave/.lv2/DRD.lv2
$ lv2ls
http://flaterco.com/lv2/DRD#6ch
http://flaterco.com/lv2/DRD#8ch
http://flaterco.com/lv2/DRD#mono
http://flaterco.com/lv2/DRD#stereo
```

If the plugin is not found by lv2ls, you need to set or change the value of
the environment variable LV2_PATH.  The default search path is supposed to
be $HOME/.lv2:/usr/local/lib/lv2:/usr/lib/lv2.

## Controls

### Attack

Range (0 to ∞) s<br>
Default 0.01 s

An indication of the rate at which DRD responds to increases in the volume of
the input.  It is an exponential process.  The attack is the time to respond
to 1 − 1/e or approximately 63% of an increase of input level.

### Decay

Range (0 to ∞) s<br>
Default 0.5 s

An indication of the rate at which DRD responds to decreases in the volume of
the input.  It is an exponential process.  The decay is the time to respond
to 1 − 1/e or approximately 63% of a decrease of input level.

## Example uses

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

[Ardour](https://ardour.org/) works with DRD.  It provides sliders to change
attack and decay on the fly, albeit with a defaulted maximum of 1 s each.

[Tenacity](https://tenacityaudio.org/) does not work with DRD at the moment.
Tenacity supports LV2 plugins and is seemingly able to apply DRD, but the
result is silent audio.  The pertinent
[code](https://codeberg.org/tenacityteam/tenacity) in src/effects/lv2 is
complex.

## Technical details

DRD is functionally similar to the compand filter of
[FFmpeg](https://github.com/FFmpeg/FFmpeg) with fixed parameters
points=-100/-100|-50/-15|0/-15 soft-knee=1 gain=0 volume=-15 delay=0.  The
major differences are (1) balance is preserved (the same gain is applied to
every channel) and (2) the transfer function is different (see plots below).
Although these plots stop at 0 dB as the nominal maximum input volume, DRD
effectively extends the straight lines above −50 dB as far as necessary to
handle out-of-range input.

![The transfer functions of DRD and compand -100/-100|-50/-15|0/-15:1:0 are plotted with input volume in dB on the x axis and target volume in dB on the y axis.  DRD:  Below -100 dB input the volume is unchanged.  Above -50 dB input the target volume is flat at -15 dB.  Between -100 and -50 dB input is a smooth curve.  Compand differs from DRD by making hard turns at the inflection points, taking a straight line between them, and having a small hook up to -14 dB output as the input level reaches 0 dB.](TransferFunctions.svg)

![DRD gain function.  The x axis is input volume in dB.  The y axis is gain in dB.  The gain rises in a curve from 0 dB at input volume -100 dB to a peak at approximately 38.31 dB at input volume -56.94̅ dB, then curves downward, becoming a straight downward-sloping line that reaches -15 dB at input volume 0 dB.](Gain.svg)

The process involves a state variable herein called the floating volume
level.  It follows the volume of the input in a general sense, effectively
finding the average over a short time interval.

The following steps are done for each sample:

1. Estimate the overall volume level of the input as the maximum level of any channel (absolute value).
2. Update the floating volume level using a fraction of its difference from the volume level of the input.  The fraction is derived from the attack or decay parameter (as applicable) as 1 − e^(−1/(parameter × sample rate)).
3. Evaluate the gain function using the floating volume level as input.
4. Apply that gain to every channel.

DRD does not avoid or prevent clipping, nor does it clip the output values
itself.  They are delivered as floats, which can go outside the intended
range of −1 to 1.  It is assumed and intended that clipping will occur
downstream.

The plot below shows the nominal worst-case scenario where the input level
instantaneously transitions from 0 (minimum) to 1 (maximum) at time 0 and
stays there.  Since the floating volume level sweeps the range starting from
0, it passes through the peak of maximum gain on its way to the top,
resulting in an extreme pop 38.3 dB beyond the limit within the first few
samples.  Negative gain is applied after ⅕ of the attack time has elapsed.

![Worst case clipping (instantaneous 0-to-1 input volume transition).  The x axis is time divided by attack ranging from 0 to 0.5.  The y axis is decibels ranging from -60 to 40, against which scale both the floating volume and the gain applied are plotted.  A thick red horizontal line at 0 dB indicates the maximum applicable gain.  The floating volume enters nearly vertically from the bottom left corner and then curves sharply to the right, nearing -8 dB at the end of the x axis.  The gain applied almost instantly shoots up to the maximum of 38 dB then decays rapidly till it crosses the 0 dB line just before x = 0.2.  At the right side of the plot it is close to -7 dB.](WorstCase.svg)

Of course, if the input to DRD is already out of range, the excursion can be
worse yet, but DRD will continue to reduce the gain until the −15 dB target
is reached.

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

## <a name="note">Note on versioning</a>

The release numbers X.Y used for github releases indicate the LV2
[minorVersion](https://lv2plug.in/ns/lv2core#minorVersion) and
[microVersion](https://lv2plug.in/ns/lv2core#microVersion) of the plugin.
The LV2 versioning scheme is described as follows (summarizing from
[minorVersion](https://lv2plug.in/ns/lv2core#minorVersion)):

> The minor version MUST be incremented when backwards (but not forwards) compatible additions are made, for example the addition of a port to a plugin.
>
> The micro version is incremented for changes which do not affect compatibility at all, for example bug fixes or documentation updates.
>
> There is deliberately no major version: all versions with the same URI are compatible by definition. Replacing a resource with a newer version of that resource MUST NOT break anything. If a change violates this rule, then the URI of the resource (which serves as the major version) MUST be changed.
>
> An odd minor *or* micro version, or minor version zero, indicates that the resource is a development version.

The minor and micro version are specified in the Turtle file DRD.ttl that is
installed as part of the DRD.lv2 bundle.

The shared library is always installed as simply DRD.so.  The version of
DRD.so can be retrieved from the binary with
`strings DRD.so | grep -F 'DRD version'`.
