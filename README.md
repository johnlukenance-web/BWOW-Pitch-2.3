# Bwow Pitch 2
Audio-triggered one-shot pitch bend (JUCE 8 VST3). Each pick attack detected in the incoming audio raises the whole
signal by Amount (default 65 cents) and glides linearly (in cents) back to unity over Decay (default 143 ms).
No MIDI, no gain envelope. Latency 10 ms (reported to host).

* `Source/BwowCore.h` - all DSP (JUCE-free): two-band attack detector + variable-delay (time-warp) pitch shifter
  with waveform-aligned head recentering at each attack.
* `tests/` - `core_test.cpp` offline driver + `run_tests.py` (77 checks measured on output audio).
* `.github/workflows/build.yml` - Windows x64 build, tests, pluginval, ZIP artifact.

Local test: `g++ -O2 -std=c++17 -o tests/core_test tests/core_test.cpp && python3 tests/run_tests.py`
