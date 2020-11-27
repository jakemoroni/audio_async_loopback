# audio_async_loopback
Real-time S/PDIF PCM/AC3 capture and playback

A simple program to play audio from an S/PDIF input, in real-time.

Requires Pulseaudio, libavcodec, and libsamplerate.

It supports uncompressed PCM as well as IEC 61937-3 (AC3) bitstreams.

When an IEC 61937 AC3 bitstream is detected, it automatically begins
decoding it into 5.1 channel audio. If there are no IEC 61937 data
bursts found within a given time window, it switches back to PCM mode.

Why not just use pacat and pipe it into ffplay/mpv/vlc/whatever? Or
Pulseaudio's module_loopback?

- module_loopback isn't capable of decoding compressed bitstreams.

- Because pacat+ffplay cannot account for the case where the S/PDIF
  input is on a different clock domain than that of the output device.
  In other words, if you're using a USB S/PDIF input device and trying
  to play back audio through your soundcard, the input and output are
  on two different clock domains. Even if they're both set to the
  same sampling rate, there will always be a slight difference due to
  oscillator tolerances. So, what ends up happening is that your
  pipe buffer either ends up growing and growing (input faster than
  output), resulting in larger and larger latency, or you end up
  with occasional buffer underflows (output faster than input).

To address this clock domain issue, the audio samples are passed
through a resampler with a ratio that is dynamically adjusted to
attempt to maintain a constant amount of data in the intermediate
buffer at any given time. This compensates for any slight differences
in sampling rate between the input and output.

One of the goals was to maintain as low of a latency as possible,
so a lot of the parameters are fairly aggressive, resulting in
high CPU usage. These parameters can be tweaked (see config.h),
and will probably need to be adjusted depending on your system.

In my system, I typically see about ~15 milliseconds of latency with
PCM audio, and ~45 with AC3 bitstreams. The reason AC3 bitstreams
have more latency is because it's decoded on a per-frame basis, and
each AC3 frame typically represents 1536 PCM samples (33 milliseconds
at 48 kHz).

In order for this program to work for AC3, it requires an S/PDIF
input that is capable of capturing bit-perfect audio. Many S/PDIF
inputs will do things like internal resampling, mixing, volume control,
etc., which will break any compressed bitstream format. Other
inputs will automatically mute the audio if the non-PCM IEC 60958
channel status bit is set, which is also a problem.

I was able to get this working using the miniDSP USBStreamer B with
the stereo firmware loaded. Just make sure Pulseaudio is configured
for 48 kHz and that the volume is set to 100%.

- Compiling:

  Just do: gcc -o test main.c iec_61937.c pcm_sink.c ac3_sink.c -lpulse-simple -lsamplerate -lpthread -lavutil -lavcodec -Wall -O3 -flto

- Usage:

  Invoke the program with the argument being the name of your input.
  Use "pactl list sources" to get a list of inputs.

NOTE: There are a lot of loose ends in this program. I made it for
      my own personal use. I'm sure there are bugs, but it works
      fine for me.


