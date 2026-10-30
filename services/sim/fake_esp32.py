"""가짜 ESP32 (팀원 3).

진짜 보드와 똑같은 토픽·형식(docs/03-messages.md)으로 발행해서, 보드 없이 웹을 개발/테스트한다.

시나리오 (환경 변수 SCENARIO):
  cycle       : present 5초 ↔ absent 5초 반복
  flicker     : 짧은 간격으로 present/absent 가 빠르게 바뀜 (웹이 꼬이지 않는지 확인)
  offline     : 몇 초 뒤 접속을 끊어 브로커가 LWT(offline)를 보내게 함 → 다시 접속
  interactive : 키보드 입력  p = present, a = absent, e = error, q = 종료
                (docker attach fp-sim 으로 붙어서 입력)
"""

import json
import os
import random
import time

import paho.mqtt.client as mqtt

BROKER = os.getenv("MQTT_BROKER", "localhost")
PORT = int(os.getenv("MQTT_PORT", "1883"))
DEVICE_ID = os.getenv("DEVICE_ID", "sim-01")
SCENARIO = os.getenv("SCENARIO", "cycle")
KEEPALIVE_S = 15
TELEMETRY_S = 1.0

TOPIC_PRESENCE = f"facepause/{DEVICE_ID}/presence"
TOPIC_STATUS = f"facepause/{DEVICE_ID}/status"
TOPIC_TELEMETRY = f"facepause/{DEVICE_ID}/telemetry"

BOOT = time.monotonic()


def uptime_ms() -> int:
    return int((time.monotonic() - BOOT) * 1000)


class FakeDevice:
    def __init__(self) -> None:
        self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=DEVICE_ID)
        # 진짜 보드와 같이 LWT 등록: 비정상 종료 시 브로커가 offline 을 retained 로 발행
        self.client.will_set(
            TOPIC_STATUS,
            json.dumps({"v": 1, "device": DEVICE_ID, "state": "offline"}),
            qos=1,
            retain=True,
        )
        self.seq = 0
        self.state = "absent"

    def connect(self) -> None:
        self.client.connect(BROKER, PORT, keepalive=KEEPALIVE_S)
        self.client.loop_start()
        self.publish_status("online")
        self.publish_presence("absent")
        print(f"[sim] connected {BROKER}:{PORT} as {DEVICE_ID}, scenario={SCENARIO}", flush=True)

    def publish_status(self, state: str, reason: str | None = None) -> None:
        # TODO(팀원3): shared/schema/status.schema.json 형식으로 retained 발행
        #   online 이면 fw, ip 포함 / error 이면 reason 포함
        pass

    def publish_presence(self, state: str) -> None:
        # TODO(팀원3): shared/schema/presence.schema.json 형식으로 retained 발행, self.seq 증가
        #   present 면 faces=1, score=0.8~0.98 정도 랜덤 / absent 면 faces=0, score=0
        self.state = state

    def publish_telemetry(self) -> None:
        # TODO(팀원3): shared/schema/telemetry.schema.json 형식 (retained 아님)
        #   fps 7~9, infer_ms 90~120 정도 랜덤, faces/score 는 self.state 에 맞게
        pass


def run_cycle(dev: FakeDevice) -> None:
    # TODO(팀원3): 5초마다 present/absent 전환, 그 사이 TELEMETRY_S 마다 publish_telemetry()
    pass


def run_flicker(dev: FakeDevice) -> None:
    # TODO(팀원3): 0.2~1.5초 랜덤 간격으로 present/absent 전환
    pass


def run_offline(dev: FakeDevice) -> None:
    # TODO(팀원3): present 상태로 5초 → 소켓을 강제로 끊어 LWT 유도
    #   (client.disconnect() 는 정상 종료라 LWT 가 안 나감 → client.socket().close() 등)
    #   → keepalive 의 약 1.5배 기다림 → 다시 connect()
    pass


def run_interactive(dev: FakeDevice) -> None:
    # TODO(팀원3): input() 으로 p / a / e / q 처리
    pass


SCENARIOS = {
    "cycle": run_cycle,
    "flicker": run_flicker,
    "offline": run_offline,
    "interactive": run_interactive,
}


def main() -> None:
    if SCENARIO not in SCENARIOS:
        raise SystemExit(f"unknown SCENARIO={SCENARIO!r}, choose one of {list(SCENARIOS)}")
    random.seed()
    dev = FakeDevice()
    dev.connect()
    SCENARIOS[SCENARIO](dev)
    # 시나리오가 아직 TODO 면 바로 끝나지 않도록 대기
    while True:
        time.sleep(60)


if __name__ == "__main__":
    main()
