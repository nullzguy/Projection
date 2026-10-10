#!/usr/bin/env bash
set -e

cd "$(dirname "$0")"

if [ "$1" = "clean" ]; then
    rm -rf build
fi

cmake -S . -B build -G Ninja
cmake --build build -j

./build/Projection