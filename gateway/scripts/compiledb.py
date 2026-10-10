import json
import os

Import("env")

env.Replace(COMPILATIONDB_PATH=os.path.join("$BUILD_DIR", "clangd", "compile_commands.json"))


def _absolutize_drivers(target, source, env):
    with open(target[0].get_path()) as fp:
        entries = json.load(fp)
    for entry in entries:
        if "command" in entry:
            token, _, rest = entry["command"].partition(" ")
            resolved = _resolve(token, env)
            if resolved:
                entry["command"] = resolved + (" " + rest if rest else "")
        elif entry.get("arguments"):
            resolved = _resolve(entry["arguments"][0], env)
            if resolved:
                entry["arguments"][0] = resolved
    with open(target[0].get_path(), "w") as fp:
        json.dump(entries, fp, indent=4)
        fp.write("\n")


def _resolve(token, env):
    token = token.strip('"')
    if not token or os.path.isabs(token):
        return None
    return env.WhereIs(token, path=env["ENV"]["PATH"])


if "compiledb" in COMMAND_LINE_TARGETS:
    # clangd resolves bare compiler names through the editor's PATH, which does
    # not contain PlatformIO toolchains, so system headers would be missing.
    env.AddPostAction(env.File("$COMPILATIONDB_PATH"), _absolutize_drivers)
