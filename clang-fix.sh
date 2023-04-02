#!/usr/bin/env bash

# http://redsymbol.net/articles/unofficial-bash-strict-mode/

set -euo pipefail
IFS=$'\n\t'

cd "$(dirname "$0")"

ENETCPP_FILES=(
    include/enetcpp/callbacks.h
    include/enetcpp/enetcpp.h
    include/enetcpp/list.h
    include/enetcpp/protocol.h
    include/enetcpp/time.h
    include/enetcpp/unix.h
    include/enetcpp/utility.h
    src/callbacks.cpp
    src/compress.cpp
    src/host.cpp
    src/packet.cpp
    src/peer.cpp
    src/platform.cpp
    src/protocol.cpp
    src/unix.cpp
    tests/tests.cpp
)

clang-tidy --fix-errors -p "build" "${ENETCPP_FILES[@]}"
clang-format -i "${ENETCPP_FILES[@]}" "include/enetcpp/win32.h" "src/win32.cpp"
