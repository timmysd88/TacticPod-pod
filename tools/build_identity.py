import subprocess

Import("env")

try:
    commit = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=env["PROJECT_DIR"], text=True
    ).strip()
    dirty = bool(subprocess.check_output(
        ["git", "status", "--porcelain"], cwd=env["PROJECT_DIR"], text=True
    ).strip())
except (OSError, subprocess.CalledProcessError):
    commit = "unknown"
    dirty = True

env.Append(CPPDEFINES=[
    ("TACTICPOD_BUILD_COMMIT", '\\"' + commit + '\\"'),
    ("TACTICPOD_BUILD_ENV", '\\"' + env["PIOENV"] + '\\"'),
    ("TACTICPOD_BUILD_DIRTY", int(dirty)),
])
