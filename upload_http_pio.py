import os
from SCons.Script import DefaultEnvironment

env = DefaultEnvironment()

# Construct the actual firmware path PIO will generate
firmware_file = os.path.join(env.subst("$BUILD_DIR"), env.subst("${PROGNAME}.bin"))

def http_upload(source, target, env):
    firmware_path = firmware_file

    ip = env.GetProjectOption("upload_port")
    user = env.GetProjectOption("custom_upload_user")
    password = env.GetProjectOption("custom_upload_password")

    cmd = f"python3 upload_http.py {ip} {user} {password} {firmware_path}"
    print("Running:", cmd)

    return os.system(cmd)

# Hook AFTER the binary is built
env.AddPostAction(firmware_file, http_upload)
