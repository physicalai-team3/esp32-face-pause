#!/bin/sh
# 컨테이너 안에서 스케치를 컴파일한다:  docker compose run --rm fw
# 추가 옵션은 그대로 전달된다 (예: docker compose run --rm fw sh build.sh --verbose)
set -e
cd /firmware

if [ ! -f face_pause/config.h ]; then
    echo "[build] face_pause/config.h 가 없어 config.example.h 로 만듭니다 (Wi-Fi 값은 직접 채우세요)"
    cp face_pause/config.example.h face_pause/config.h
fi

# 보드 XIAO_ESP32S3 + OPI PSRAM (Arduino IDE 의 도구 메뉴와 같은 설정)
FQBN="esp32:esp32:XIAO_ESP32S3:PSRAM=opi"

arduino-cli compile \
    --fqbn "$FQBN" \
    --libraries libraries \
    --output-dir build \
    face_pause "$@"

echo "[build] OK -> firmware/build/"
