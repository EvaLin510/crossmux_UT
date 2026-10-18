"""Register a PlatformIO build middleware that survives pioarduino's Windows wrapper.

On Windows, pioarduino's Arduino framework script wraps every middleware registered
before it runs: it calls each one for every source file (ignoring its pattern), swaps
env.Object for a stand-in that returns None, and applies the captured kwargs by
mutating env only until the call returns - SCons expands the command later, so the
flags are lost. Registering after BuildFrameworks() avoids the wrapper; a router then
sends matching files to our middleware and everything else through the existing chain
(pioarduino's include-path shortening).
"""

import fnmatch
import sys


def add_build_middleware(env, callback, pattern):
    if sys.platform != "win32" or not hasattr(env, "AddMethod"):
        env.AddBuildMiddleware(callback, pattern)
        return

    original_build_frameworks = env.BuildFrameworks

    def build_frameworks(build_env, *args, **kwargs):
        result = original_build_frameworks(*args, **kwargs)
        downstream = list(build_env.get("__PIO_BUILD_MIDDLEWARES", []))

        def routed(middleware_env, node):
            path = node.srcnode().get_path()
            if fnmatch.fnmatch(path, pattern):
                return callback(middleware_env, node)
            new_node = node
            for next_callback, next_pattern in downstream:
                if next_pattern and not fnmatch.fnmatch(path, next_pattern):
                    continue
                if next_callback.__code__.co_argcount == 2:
                    new_node = next_callback(middleware_env, new_node)
                else:
                    new_node = next_callback(new_node)
                if not new_node:
                    break
            return new_node

        build_env["__PIO_BUILD_MIDDLEWARES"] = [(routed, None)]
        return result

    env.AddMethod(build_frameworks, "BuildFrameworks")
