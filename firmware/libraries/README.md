# firmware/libraries

Arduino 스케치북의 `libraries` 폴더다. Arduino IDE의 **스케치북 위치를 `firmware/`로** 지정하면 IDE가 이 폴더의 라이브러리를 자동으로 찾는다. `fw` 컨테이너도 이 폴더를 쓴다(`--libraries libraries`).

## Edge Impulse 라이브러리 (팀원 1)

[docs/05-face-model.md §2](../../docs/05-face-model.md) 순서대로 받은 Arduino 라이브러리 zip을 **이 폴더에 압축 해제하고 커밋**한다.

```
firmware/libraries/
└── Face_detection_-_FOMO_inferencing/   ← 예시. 실제 이름은 내보낸 zip 에 따라 다름
    ├── library.properties
    ├── examples/esp32/esp32_camera/
    └── src/Face_detection_-_FOMO_inferencing.h
```

압축 해제 후 `firmware/face_pause/face_sensor.cpp`의 `#include <...inferencing.h>`를 실제 헤더 이름으로 맞춘다.

## PubSubClient (각자 설치, 커밋하지 않음)

Arduino IDE 라이브러리 관리자에서 **PubSubClient 2.8** (Nick O'Leary)을 설치한다. 스케치북이 `firmware/`이면 이 폴더에 설치되지만 `.gitignore`로 제외되어 있다. `fw` 컨테이너에는 이미 설치되어 있다.
