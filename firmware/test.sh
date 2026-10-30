#!/bin/sh
# 디바운스 단위 테스트 (Arduino 없이 PC 의 g++ 로):  docker compose run --rm fw sh test.sh
set -e
cd /firmware

g++ -std=c++17 -Wall -Wextra -I face_pause \
    test/debounce_test.cpp face_pause/debounce.cpp \
    -o test/debounce_test.out

./test/debounce_test.out
