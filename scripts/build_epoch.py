"""Expose the build's UTC epoch to firmware as <BuildEpoch.h>.

Written into the build directory (never the source tree) behind a fixed include
path, so it changes no compiler flags: only the file that includes it rebuilds.
UT37 seeds an RTC that has never held a valid time from this value.
"""

import os
import time

Import("env")  # noqa: F821

out_dir = os.path.join(env.subst("$BUILD_DIR"), "generated")  # noqa: F821
os.makedirs(out_dir, exist_ok=True)
with open(os.path.join(out_dir, "BuildEpoch.h"), "w", encoding="utf-8") as header:
    header.write("#pragma once\n#define CROSSMUX_BUILD_EPOCH %dLL\n" % int(time.time()))
env.Append(CPPPATH=[out_dir])  # noqa: F821
