"""PlatformIO post-script: after firmware.bin, merge bootloader, partition table and
app into one image (<build>/firmware-full-usb.bin) that is flashed at 0x0.
Gaps are 0xFF, so nvs and otadata start blank: a first install, not an update."""

from pathlib import Path

Import("env")

MERGED_NAME = "firmware-full-usb.bin"


def merge_image(source, target, env):
    images = [(offset, path) for offset, path in env["FLASH_EXTRA_IMAGES"]]
    images.append((env.subst("$ESP32_APP_OFFSET"), str(target[0])))
    merged = Path(env.subst("$BUILD_DIR")) / MERGED_NAME
    parts = " ".join(f'{offset} "{env.subst(path)}"' for offset, path in images)
    env.Execute(
        f'"$PYTHONEXE" "$OBJCOPY" --chip {env.BoardConfig().get("build.mcu")} merge_bin -o "{merged}" {parts}'
    )


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_image)
