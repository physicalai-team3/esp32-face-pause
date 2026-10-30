"""가짜 ESP32 (팀원 3).

진짜 보드와 똑같은 토픽·형식(docs/03-messages.md)으로 발행해서, 보드 없이 웹을 개발/테스트한다.
펌웨어(presence_link)의 행동도 그대로 흉내 낸다:
  - 접속 전에 LWT(status=offline, retained) 등록
  - 접속(재접속)할 때마다 status → 현재 presence 순서로 retained 발행
  - presence 는 상태가 바뀔 때만, telemetry 는 1초마다(retained 아님)
  - sensor_timeout 오류가 풀리면 같은 접속에서 status=online 만 다시 보냄 (presence 는 바뀔 때만)

시나리오 (환경 변수 SCENARIO):
  cycle       : present 5초 ↔ absent 5초 반복
  flicker     : 짧은 간격으로 present/absent 가 빠르게 바뀜 (웹이 꼬이지 않는지 확인)
  offline     : 몇 초 뒤 접속을 끊어 브로커가 LWT(offline)를 보내게 함 → 다시 접속
  sensor      : present → 센서 멈춤(absent → error sensor_timeout) → 센서 복구(online) 반복
  interactive : 키보드 입력 (docker attach fp-sim 으로 붙어서 입력)
                p = present, a = absent, e = 센서 오류(sensor_timeout), o = 센서 복구(online),
                d = 접속 끊기(LWT), r = 재접속, q = 종료
"""

from __future__ import annotations

import json
import os
import random
import signal
import threading
import time

import paho.mqtt.client as mqtt

BROKER = os.getenv("MQTT_BROKER", "localhost")
PORT = int(os.getenv("MQTT_PORT", "1883"))
DEVICE_ID = os.getenv("DEVICE_ID", "sim-01")
SCENARIO = os.getenv("SCENARIO", "cycle")
KEEPALIVE_S = 15
TELEMETRY_S = 1.0
OFFLINE_S = 10          # offline 시나리오에서 끊긴 채로 있는 시간
FW_VERSION = "sim-0.1.0"

TOPIC_PRESENCE = f"facepause/{DEVICE_ID}/presence"
TOPIC_STATUS = f"facepause/{DEVICE_ID}/status"
TOPIC_TELEMETRY = f"facepause/{DEVICE_ID}/telemetry"

BOOT = time.monotonic()


def uptime_ms() -> int:
    return int((time.monotonic() - BOOT) * 1000)


def to_json(payload: dict) -> str:
    # 펌웨어처럼 한 줄, 공백 없이 (PubSubClient 버퍼 256바이트 안에 들어가야 함)
    return json.dumps(payload, separators=(",", ":"))


class FakeDevice:
    def __init__(self) -> None:
        self.seq = 0
        self.state = "absent"
        self.error: str | None = None      # 펌웨어의 s_sensor_error 와 같은 역할
        self.client = self._make_client()
        threading.Thread(target=self._telemetry_loop, daemon=True).start()

    def _make_client(self) -> mqtt.Client:
        client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=DEVICE_ID)
        # 진짜 보드와 같이 LWT 등록 (connect 전에): 비정상 종료 시 브로커가 offline 을 retained 로 발행
        client.will_set(
            TOPIC_STATUS,
            to_json({"v": 1, "device": DEVICE_ID, "state": "offline"}),
            qos=1,
            retain=True,
        )
        client.on_connect = self._on_connect
        client.on_disconnect = self._on_disconnect
        return client

    # ── 접속 ─────────────────────────────────────────────────────────
    def connect(self) -> None:
        # broker 가 아직 안 떠 있어도 죽지 않고 기다린다
        while True:
            try:
                self.client.connect(BROKER, PORT, keepalive=KEEPALIVE_S)
                break
            except OSError as e:
                print(f"[sim] broker {BROKER}:{PORT} not ready ({e}), retry in 2s", flush=True)
                time.sleep(2)
        self.client.loop_start()   # 재접속도 이 스레드가 알아서 한다

    def _on_connect(self, client, userdata, flags, reason_code, properties) -> None:
        if reason_code.is_failure:
            print(f"[sim] connect refused: {reason_code}", flush=True)
            return
        print(f"[sim] connected {BROKER}:{PORT} as {DEVICE_ID}, scenario={SCENARIO}", flush=True)
        # 새로 접속(재접속)할 때마다: status → 현재 presence 1회 (presence_link.cpp 와 같은 순서)
        self.publish_status()
        self.publish_presence(self.state)

    def _on_disconnect(self, client, userdata, flags, reason_code, properties) -> None:
        print(f"[sim] disconnected ({reason_code})", flush=True)

    def drop(self) -> None:
        """비정상 종료 흉내. disconnect() 는 DISCONNECT 를 보내 LWT 가 취소되므로 소켓을 직접 닫는다."""
        self.client.loop_stop()
        sock = self.client.socket()
        if sock is not None:
            sock.close()
        print("[sim] connection dropped -> broker should publish LWT (status=offline)", flush=True)

    def reconnect(self) -> None:
        self.client = self._make_client()
        self.connect()

    def shutdown(self) -> None:
        """종료(q, docker stop): offline 을 남기고 정상 종료."""
        if self.client.is_connected():
            info = self.client.publish(
                TOPIC_STATUS, to_json({"v": 1, "device": DEVICE_ID, "state": "offline"}), qos=1, retain=True
            )
            info.wait_for_publish(timeout=2)
            self.client.disconnect()
        self.client.loop_stop()

    # ── 발행 ─────────────────────────────────────────────────────────
    def _publish(self, topic: str, payload: dict, retain: bool, qos: int = 1, log: bool = True) -> None:
        data = to_json(payload)
        self.client.publish(topic, data, qos=qos, retain=retain)
        if log:
            print(f"[sim] {topic.rsplit('/', 1)[-1]:<8} {data}", flush=True)

    def publish_status(self) -> None:
        # shared/schema/status.schema.json: online 이면 fw, ip / error 이면 reason
        if self.error:
            payload = {"v": 1, "device": DEVICE_ID, "state": "error", "reason": self.error}
        else:
            payload = {"v": 1, "device": DEVICE_ID, "state": "online", "fw": FW_VERSION, "ip": self._local_ip()}
        self._publish(TOPIC_STATUS, payload, retain=True)

    def publish_presence(self, state: str) -> None:
        # shared/schema/presence.schema.json, 펌웨어처럼 seq 는 0부터 메시지마다 1씩
        self.state = state
        present = state == "present"
        payload = {
            "v": 1,
            "device": DEVICE_ID,
            "state": state,
            "faces": 1 if present else 0,
            "score": round(random.uniform(0.80, 0.98), 2) if present else 0,
            "seq": self.seq,
            "uptime_ms": uptime_ms(),
        }
        self.seq += 1
        self._publish(TOPIC_PRESENCE, payload, retain=True)

    def set_presence(self, state: str) -> None:
        # 디바운스가 끝난 상태가 "바뀔 때만" 발행 (재접속 직후 재발행은 _on_connect 가 한다)
        if state != self.state:
            self.publish_presence(state)

    def publish_telemetry(self) -> None:
        # shared/schema/telemetry.schema.json. faces/score 는 디바운스 전 원시값이라 가끔 놓치거나 오검출한다
        if self.error:
            faces, score, fps, infer_ms = 0, 0, 0.0, 0          # 센서가 멈춘 상태: 프레임이 안 들어옴
        elif self.state == "present":
            missed = random.random() < 0.1
            faces, score = (0, 0) if missed else (1, round(random.uniform(0.80, 0.98), 2))
            fps, infer_ms = round(random.uniform(7, 9), 1), random.randint(90, 120)
        else:
            ghost = random.random() < 0.03
            faces, score = (1, round(random.uniform(0.50, 0.60), 2)) if ghost else (0, 0)
            fps, infer_ms = round(random.uniform(7, 9), 1), random.randint(90, 120)
        payload = {
            "v": 1,
            "device": DEVICE_ID,
            "faces": faces,
            "score": score,
            "fps": fps,
            "infer_ms": infer_ms,
            "rssi": random.randint(-65, -50),
            "heap_kb": random.randint(148, 156),
            "psram_kb": random.randint(5820, 5840),
            "uptime_ms": uptime_ms(),
        }
        self._publish(TOPIC_TELEMETRY, payload, retain=False, qos=0, log=False)

    def _telemetry_loop(self) -> None:
        # 시나리오와 상관없이 TELEMETRY_S 마다 (접속돼 있을 때만)
        while True:
            time.sleep(TELEMETRY_S)
            if self.client.is_connected():
                self.publish_telemetry()

    def _local_ip(self) -> str:
        sock = self.client.socket()
        try:
            return sock.getsockname()[0] if sock is not None else "0.0.0.0"
        except OSError:
            return "0.0.0.0"

    # ── 센서 오류 (presence_link.cpp 의 sensor_timeout 처리와 같은 순서) ──
    def sensor_stop(self) -> None:
        # 프레임이 끊기면 FP_ABSENT_MS(1.5초) 뒤 absent 가 먼저 나가고, FP_SENSOR_TIMEOUT_MS(3초)에 error
        self.set_presence("absent")
        self.error = "sensor_timeout"
        self.publish_status()

    def sensor_recover(self) -> None:
        # 같은 접속에서 status=online 만 다시 보낸다 (presence 는 다음 변화 때)
        if self.error == "sensor_timeout":
            self.error = None
            self.publish_status()


def run_cycle(dev: FakeDevice) -> None:
    while True:
        time.sleep(5)
        dev.set_presence("present")
        time.sleep(5)
        dev.set_presence("absent")


def run_flicker(dev: FakeDevice) -> None:
    while True:
        time.sleep(random.uniform(0.2, 1.5))
        dev.set_presence("absent" if dev.state == "present" else "present")


def run_offline(dev: FakeDevice) -> None:
    while True:
        time.sleep(2)
        dev.set_presence("present")
        time.sleep(5)
        dev.drop()
        time.sleep(OFFLINE_S)
        print("[sim] reconnecting", flush=True)
        dev.reconnect()


def run_sensor(dev: FakeDevice) -> None:
    while True:
        time.sleep(3)
        dev.set_presence("present")
        time.sleep(5)
        print("[sim] sensor stopped", flush=True)
        time.sleep(1.5)
        dev.set_presence("absent")
        time.sleep(1.5)
        dev.sensor_stop()
        time.sleep(5)
        print("[sim] sensor recovered", flush=True)
        dev.sensor_recover()


def run_interactive(dev: FakeDevice) -> None:
    print("[sim] keys: p=present a=absent e=sensor error o=sensor ok d=drop(LWT) r=reconnect q=quit", flush=True)
    while True:
        try:
            key = input().strip().lower()
        except EOFError:
            # tty 없이 실행됨 (stdin_open 없음) → 입력 없이 계속 떠 있는다
            print("[sim] no stdin; attach with a tty to use keys", flush=True)
            return
        if key == "p":
            dev.set_presence("present")
        elif key == "a":
            dev.set_presence("absent")
        elif key == "e":
            dev.sensor_stop()
        elif key == "o":
            dev.sensor_recover()
        elif key == "d":
            dev.drop()
        elif key == "r":
            dev.reconnect()
        elif key == "q":
            raise SystemExit(0)
        elif key:
            print(f"[sim] unknown key {key!r}", flush=True)


SCENARIOS = {
    "cycle": run_cycle,
    "flicker": run_flicker,
    "offline": run_offline,
    "sensor": run_sensor,
    "interactive": run_interactive,
}


def _exit_on_signal(signum, frame) -> None:
    raise SystemExit(0)


def main() -> None:
    if SCENARIO not in SCENARIOS:
        raise SystemExit(f"unknown SCENARIO={SCENARIO!r}, choose one of {list(SCENARIOS)}")
    # docker stop(SIGTERM)도 q 와 같이 정리하고 끝낸다
    signal.signal(signal.SIGTERM, _exit_on_signal)
    random.seed()
    dev = FakeDevice()
    dev.connect()
    try:
        SCENARIOS[SCENARIO](dev)
        # interactive 에서 stdin 이 없으면 여기로 온다
        while True:
            time.sleep(60)
    except (SystemExit, KeyboardInterrupt):
        print("[sim] bye", flush=True)
        dev.shutdown()


if __name__ == "__main__":
    main()
