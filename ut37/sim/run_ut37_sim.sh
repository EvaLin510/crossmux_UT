#!/usr/bin/env bash
# Build (and optionally run) the UT37 desktop simulator on Linux/WSL.
#
#   ut37/sim/run_ut37_sim.sh                     # build only
#   ut37/sim/run_ut37_sim.sh -t run_simulator    # build + open the SDL window
#
# The repo is mirrored into $UT37_SIM_WORK (default ~/ut37sim) and built there,
# so the Windows checkout's .pio is never touched and builds stay off /mnt/c.
# The simulator is cloned at the commit pinned in platformio.ini, then
# patch_simulator_ut37.py adds the SIMULATOR_DEVICE_UT37 profile.
# Screenshots/scripted input use the simulator's env vars, e.g.
#   CROSSPOINT_SIM_SCREENSHOTS="3000:/tmp/home.png"
#   CROSSPOINT_SIM_INPUT_SCRIPT="..."
set -euo pipefail

REPO=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${UT37_SIM_WORK:-$HOME/ut37sim}
PIO=${PIO:-$HOME/.pio-venv/bin/pio}
SIM_URL=https://github.com/0x1abin/crosspoint-simulator.git
SIM_COMMIT=$(grep -o 'crosspoint-simulator.git#[0-9a-f]*' "$REPO/platformio.ini" | head -1 | cut -d'#' -f2)

mkdir -p "$WORK"
[ -d "$WORK/simulator/.git" ] || git clone -q "$SIM_URL" "$WORK/simulator"
git -C "$WORK/simulator" fetch -q origin "$SIM_COMMIT" 2>/dev/null || true
git -C "$WORK/simulator" checkout -q --force "$SIM_COMMIT"
python3 "$REPO/ut37/sim/patch_simulator_ut37.py" "$WORK/simulator"

# fs_/ is the simulated SD card root (books, fonts); keep it across syncs.
rsync -a --delete --exclude '/.pio' --exclude '/fs_' --exclude '.git' --exclude '/硬體資料*' "$REPO/" "$WORK/crossmux/"

# platformio.ini already loads platformio.local.ini via extra_configs; add the
# UT37 simulator env to the mirror's copy only.
{
  [ -f "$REPO/platformio.local.ini" ] && cat "$REPO/platformio.local.ini"
  cat <<EOF

[env:simulator_ut37]
extends = env:simulator
; The AHT30 driver needs Wire; the weak env_sensor::read() stub reports no sensor.
build_src_filter =
  \${env:simulator.build_src_filter}
  -<util/EnvSensorAht30.cpp>
build_flags =
  \${env:simulator.build_flags}
  -DSIMULATOR_DEVICE_UT37
  ; Global, like the firmware env: headers such as SdCardFontSystem.h change
  ; class layout on it, so every TU must agree, not only BoardConfig includers.
  -DFREEINK_DEVICE_UT37=1
extra_scripts =
  \${env:simulator.extra_scripts}
  pre:ut37/sim/c_std_gnu17.py
lib_deps =
  simulator=symlink://$WORK/simulator
  BleKeyboardHost=symlink://freeink-sdk/libs/network/BleKeyboardHost
  FreeInkUI=symlink://freeink-sdk/libs/ui/FreeInkUI
  Icons=symlink://freeink-sdk/libs/assets/Icons
  bblanchon/ArduinoJson @ 7.4.2
  ricmoo/QRCode @ 0.0.1
  knolleary/PubSubClient @ 2.8
EOF
} > "$WORK/crossmux/platformio.local.ini"

cd "$WORK/crossmux"
exec "$PIO" run -e simulator_ut37 "$@"
