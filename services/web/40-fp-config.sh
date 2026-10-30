#!/bin/sh
# 정적 페이지는 환경 변수를 읽을 수 없으므로, 컨테이너 시작 시 config.js 로 만들어 준다.
set -e

cat > /usr/share/nginx/html/config.js <<EOF
window.FP_CONFIG = {
  mqttUrl: "${MQTT_WS_URL:-ws://localhost:9001}",
  defaultDeviceId: "${DEFAULT_DEVICE_ID:-xiao-01}",
  defaultVideoId: "${DEFAULT_VIDEO_ID:-}",
  devices: ["xiao-01", "sim-01"]
};
EOF

echo "[fp-config] config.js: mqttUrl=${MQTT_WS_URL:-ws://localhost:9001}"
