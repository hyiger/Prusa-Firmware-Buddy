#!/bin/bash
# Prepares a Claude Code on the web container for Buddy firmware work:
# system tools the unit tests need, the .venv that CMake and the build
# scripts look for, and whichever pinned tools from utils/bootstrap.py the
# environment's network policy allows. Idempotent; stdout becomes session
# context, so progress goes to stderr and only the summary to stdout.
set -euo pipefail

if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
    exit 0
fi

cd "${CLAUDE_PROJECT_DIR:-$(git -C "$(dirname "$0")" rev-parse --show-toplevel)}"

log() { echo "[session-start] $*" >&2; }

SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    SUDO="sudo"
fi

apt_install() {
    log "apt-get install $*"
    if ! $SUDO apt-get install -y -qq "$@" >&2; then
        $SUDO apt-get update -qq >&2
        $SUDO apt-get install -y -qq "$@" >&2
    fi
}

# msgfmt compiles the .po files for the translator tests and the firmware.
if ! command -v msgfmt >/dev/null; then
    apt_install gettext
fi

# Exactly 3.12: utils/build_tests.py uses PEP 701 f-strings (3.11 is too old),
# and requirements.txt pins numpy==1.26.4, which has no wheels for 3.13+.
is_py312() { "$1" -c 'import sys; sys.exit(sys.version_info[:2] != (3, 12))' 2>/dev/null; }

PYTHON=""
for candidate in python3.12 python3; do
    if command -v "$candidate" >/dev/null && is_py312 "$candidate"; then
        PYTHON="$candidate"
        break
    fi
done
if [ -z "$PYTHON" ]; then
    apt_install python3.12 python3.12-venv
    PYTHON="python3.12"
fi

# cmake/Utilities.cmake and utils/build.py both expect the venv at <repo>/.venv.
if [ -x .venv/bin/python ] && ! is_py312 .venv/bin/python; then
    log "recreating .venv with $PYTHON"
    rm -rf .venv
fi
if [ ! -x .venv/bin/python ]; then
    log "creating .venv with $PYTHON"
    "$PYTHON" -m venv --prompt buddy .venv
fi

requirements_hash="$(sha256sum requirements.txt | cut -d' ' -f1)"
if [ "$(cat .venv/.requirements.sha256 2>/dev/null)" != "$requirements_hash" ]; then
    log "installing requirements.txt into .venv"
    pip_install() { .venv/bin/python -m pip install --disable-pip-version-check --no-input -q "$@" >&2; }
    # Same order as utils/bootstrap.py: the pinned pip first, then the rest.
    pip_install "$(grep -m1 '^pip' requirements.txt)"
    pip_install -r requirements.txt
    echo "$requirements_hash" >.venv/.requirements.sha256
fi

# Reuse bootstrap.py's own download logic so versions and URLs stay in one
# place. Unlike running bootstrap.py itself, a blocked host doesn't abort the
# rest, and no git hooks get installed. Debug-only extras (mini404 simulator,
# SVD files, CrashDebug) are left for bootstrap.py to fetch on demand.
tools_status="$(
    .venv/bin/python - <<'EOF'
import shutil
import sys

sys.path.insert(0, 'utils')
import bootstrap

# bootstrap prints progress to stdout; keep stdout for the result only.
result_out, sys.stdout = sys.stdout, sys.stderr

# bootstrap() creates this before installing; install_dependency() doesn't.
bootstrap.dependencies_dir.mkdir(parents=True, exist_ok=True)

required = ('cmake', 'ninja', 'clang-format')
wanted = [
    dep for dep in bootstrap.dependencies
    if dep in required or dep == 'gcc-arm-none-eabi'
    or dep.startswith(('bootloader-', 'firmware-'))
]

missing = []
for dep in wanted:
    if bootstrap.recommended_version_is_available(dep):
        continue
    try:
        bootstrap.install_dependency(dep)
    except Exception as error:
        print(f'\n[session-start] failed to install {dep}: {error}', file=sys.stderr)
        # A partial directory would pass recommended_version_is_available() next time.
        shutil.rmtree(bootstrap.get_dependency_directory(dep), ignore_errors=True)
        missing.append(dep)

if any(dep in required for dep in missing):
    sys.exit(f'[session-start] required tools missing: {missing}')
print(' '.join(missing), file=result_out)
EOF
)"

if [ -n "${CLAUDE_ENV_FILE:-}" ]; then
    {
        echo "export VIRTUAL_ENV=\"$PWD/.venv\""
        echo "export PATH=\"$PWD/.venv/bin:\$PATH\""
    } >>"$CLAUDE_ENV_FILE"
fi

echo "Buddy dev environment ready: .venv ($(.venv/bin/python --version)) with requirements.txt is on PATH," \
    "gettext installed, pinned cmake/ninja/clang-format 16 in .dependencies/." \
    "Unit tests: python3 utils/build_tests.py --run -- -LE slow."
if [ -n "$tools_status" ]; then
    echo "Not installed (download blocked or failed): $tools_status." \
        "Without gcc-arm-none-eabi, firmware builds are unavailable in this session;" \
        "do not substitute a distro arm-none-eabi-gcc."
fi
