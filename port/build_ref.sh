#!/bin/sh
# Builds the reference (original Shruthi firmware sources, AVR hardware stubbed) for the host.
# Usage: ./build_ref.sh /path/to/shruthi-1-master
set -e
S=${1:-../src/shruthi-1-master}
cd "$(dirname "$0")"
ln -sfn "$(cd "$S" && pwd)/shruthi" ref/shruthi
# real avrlib ("avril" repo), copied and patched for the host (see ref/README.md for the three edits)
A=${AVRIL:-../src/avril-master}
rm -rf ref/avrlib && mkdir -p ref/avrlib && cp -r "$A"/* ref/avrlib/
python3 host_patch_avrlib.py ref/avrlib
CXX="g++ -std=gnu++11 -O1 -w -fpack-struct=1 -Iref/stub -Iref -I$(cd "$S" && pwd)"   # AVR has no struct padding
for f in part clock note_stack parameter patch system_settings voice_allocator sequencer_settings oscillator resources; do
  $CXX -c ref/shruthi/$f.cc -o ref/$f.o
done
$CXX -c ref/avrlib/random.cc -o ref/random.o
$CXX -c ref/avrlib/string.cc -o ref/avstring.o
$CXX -c ref/ref_harness.cc -o ref/ref_harness.o
$CXX -c ref/stub/ref_voice_harness.cc -o ref/ref_voice_harness.o
echo "reference built"
