import os
import shutil

env = globals().get("env", None)

if env:
    def copy_firmware(source, target, env):
        target_dir = os.path.join(env.get("PROJECT_DIR"), "meus_firmwares")
        os.makedirs(target_dir, exist_ok=True)

        shutil.copy(
            str(source[0]),
            os.path.join(target_dir, "firmware.bin")
        )

        print(f"\n[SUCESSO] Firmware copiado para: {target_dir}/firmware.bin\n")

    env.AddPostAction(
        "$BUILD_DIR/${PROGNAME}.bin",
        copy_firmware
    )