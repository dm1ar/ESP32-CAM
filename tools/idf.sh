#!/usr/bin/env bash
set -e
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
export IDF_PATH="${IDF_PATH:-/home/rax/esp/v6.1/esp-idf}"
export IDF_TOOLS_PATH="${IDF_TOOLS_PATH:-/home/rax/.espressif}"
export IDF_PYTHON_ENV_PATH="${IDF_PYTHON_ENV_PATH:-/home/rax/.espressif/python_env/idf6.1_py3.12_env}"
source "$IDF_PATH/export.sh" >&2
cd "$project_dir"
exec "$IDF_PYTHON_ENV_PATH/bin/python" "$IDF_PATH/tools/idf.py" "$@"
