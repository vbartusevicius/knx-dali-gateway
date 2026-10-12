#!/bin/sh
# C++ style gate (cpp-lint container): clang-format must leave every file
# unchanged and clang-tidy (gateway/.clang-tidy) must report nothing.
#   docker compose --profile lint run --rm cpp-lint gateway/scripts/check_cpp.sh [--fix]
# Covers the portable core, tests and the Linux lab (native and lab compile
# databases); src/firmware and src/espidf only build for ESP-IDF and get the
# clang-format check plus GCC's -Wall -Werror in the C6 build.
set -eu
cd "$(dirname "$0")/.."
export PLATFORMIO_BUILD_DIR=/tmp/pio/build PLATFORMIO_LIBDEPS_DIR=/tmp/pio/libdeps
fix=""
[ "${1:-}" = "--fix" ] && fix="-fix"

sources=$(find src tests -name '*.cpp' -o -name '*.hpp' | grep -v '^src/knx/generated/' | sort)
if [ -n "$fix" ]; then
    clang-format-19 -i $sources
else
    clang-format-19 --dry-run --Werror $sources
fi

pio run -e native -e lab -t compiledb >/dev/null
# thelsing/knx headers lack `override`; that is the library's, not ours.
tidy() {
    run-clang-tidy-19 -quiet $fix -extra-arg=-Wno-inconsistent-missing-override -p "$1" "$2"
}
tidy /tmp/pio/build/native/clangd "$(pwd)/(src|tests)/(?!lab/|knxstack/|firmware/).*\.cpp$"
tidy /tmp/pio/build/lab/clangd "$(pwd)/src/(lab|knxstack)/.*\.cpp$"
echo "C++ style: clean"
