#!/bin/bash

INSTALL_DIR=$1
BUILD_DIR=build


if [ ! -d "$BUILD_DIR"]; then
    echo "not exist dir $BUILD_DIR"
else
    rm -rf "$BUILD_DIR"
    mkdir "$BUILD_DIR"
fi

cd "$BUILD_DIR"
cmake -DKVS_PLAT_ANYKE_FREERTOS="ON" -DKVS_PLAT_ESP_FREERTOS="OFF" -DCMAKE_TOOLCHAIN_FILE="../CrossCompile.cmake" -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR/../  ..
make
make install


