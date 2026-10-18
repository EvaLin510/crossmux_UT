"""Override NimBLE host configuration without changing installed sources."""

import os

Import("env")  # noqa: F821
import sys

sys.path.insert(0, env.subst("$PROJECT_DIR/scripts"))  # noqa: F821
from pio_middleware import add_build_middleware  # noqa: E402

config = os.path.join(
    env.subst("$PROJECT_DIR"),
    env.GetProjectOption("custom_nimble_config", "src/platform/NimblePsramConfig.h"),
)


def configure_host(build_env, node):
    compiled = build_env.Object(node, CCFLAGS=build_env.get("CCFLAGS", []) + ["-include", config])[0]
    build_env.Depends(compiled, config)
    return compiled


add_build_middleware(env, configure_host, "*/NimBLE-Arduino/src/*")
