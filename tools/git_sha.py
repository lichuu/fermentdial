import subprocess

Import("env")


def git_sha():
    try:
        return subprocess.check_output(
            ["git", "rev-parse", "--short=12", "HEAD"],
            cwd=env.subst("$PROJECT_DIR"),
            stderr=subprocess.DEVNULL,
            text=True,
        ).strip()
    except Exception:
        return "unknown"


env.Append(BUILD_FLAGS=[f'-D FERM_GIT_SHA=\\"{git_sha()}\\"'])
