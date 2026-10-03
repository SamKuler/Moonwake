# libebur128 1.2.6

Unmodified `ebur128.c`, `ebur128.h` and `COPYING` from the upstream
[`v1.2.6` tag](https://github.com/jiixyj/libebur128/tree/v1.2.6).
The MIT license is retained in `COPYING`. CMake builds the vendored C source
as a static library; configuring Moonwake does not download this dependency.

Windows builds use the upstream tag's unmodified `queue/sys/queue.h` fallback
for the BSD queue macros and enable `_USE_MATH_DEFINES` with MSVC. The queue
header's BSD license is retained in the header and `queue/COPYING`; Windows
builds include both library licenses beside the application.

Moonwake uses momentary (400 ms), short-term (3 s), histogram-based gated
integrated loudness and per-channel sample/true peaks. The library's true-peak
interpolator uses 4x oversampling below 96 kHz, 2x below 192 kHz, and sample
peaks at 192 kHz and above. All library operations run on the measurement
worker. Its initialization writes shared coefficient tables, so access from
multiple meter workers is serialized outside the audio callback.

The audio callback writes post-effects, post-gain PCM into a bounded,
preallocated SPSC queue. Reset discards queued audio from the previous
measurement. Overflow restarts measurement and exposes an interruption flag;
missing samples are never incorporated into a continuous integrated result.
RMS uses a separate 300 ms unweighted sliding window, including LFE.

This integration does not constitute certification against the complete EBU
conformance test set.
