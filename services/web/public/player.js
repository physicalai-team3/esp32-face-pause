// 팀원 3: 유튜브 플레이어 + MQTT 구독 → 재생/정지
// 설계: docs/06-web-ui.md, 메시지 형식: docs/03-messages.md
"use strict";

const CONFIG = window.FP_CONFIG || {
  mqttUrl: "ws://localhost:9001",
  defaultDeviceId: "xiao-01",
  defaultVideoId: "",
  devices: ["xiao-01", "sim-01"],
};

const RECENT_KEY = "fp.recent";      // 최근 영상 (localStorage)
const DEVICE_KEY = "fp.device";      // 마지막 선택 장치
const RECENT_MAX = 5;
const HUD_STALE_MS = 3000;

const state = {
  player: null,          // YT.Player
  playerReady: false,
  started: false,        // ⑧ 시작 버튼을 눌렀나
  faceControl: true,     // ⑥ 토글
  deviceId: CONFIG.defaultDeviceId,
  client: null,          // mqtt.js 클라이언트
  presence: "unknown",   // present | absent | unknown
  status: "unknown",     // online | offline | error | unknown
  lastTelemetryAt: 0,
};

const $ = (id) => document.getElementById(id);

// ── 저장소 (private 창 등에서 실패할 수 있으므로 try/catch) ──────────────
function loadJson(key, fallback) {
  try {
    const v = localStorage.getItem(key);
    return v ? JSON.parse(v) : fallback;
  } catch {
    return fallback;
  }
}

function saveJson(key, value) {
  try {
    localStorage.setItem(key, JSON.stringify(value));
  } catch {
    /* 저장 실패는 무시 */
  }
}

// ── ② 링크 → 영상 ID ─────────────────────────────────────────────────
// docs/06-web-ui.md §3 의 표에 있는 형식을 모두 처리. 실패하면 null.
function parseVideoId(input) {
  // TODO(팀원3): watch?v=, youtu.be/, shorts/, embed/, m.youtube.com, ID만 입력
  //              영상 ID 형식: /^[A-Za-z0-9_-]{11}$/
  void input;
  return null;
}

// ── ④ 플레이어 ───────────────────────────────────────────────────────
// YouTube IFrame API 스크립트가 로드되면 자동으로 호출된다
window.onYouTubeIframeAPIReady = function () {
  // TODO(팀원3): new YT.Player("player", { videoId, playerVars, events: { onReady, onError } })
  //   - 초기 영상: 최근 영상 1번 → 없으면 CONFIG.defaultVideoId
  //   - onReady: state.playerReady = true
  //   - onError 101/150: "이 영상은 외부 재생이 허용되지 않습니다" 표시
};

function loadVideo(videoId) {
  // TODO(팀원3): state.player.cueVideoById(videoId)
  //   얼굴 제어 중이고 presence 가 present 면 바로 playVideo()
  //   최근 영상 목록 맨 앞에 추가 (중복 제거, 최대 RECENT_MAX) → saveJson → renderRecent()
  void videoId;
}

function play() {
  if (state.playerReady) state.player.playVideo();
}

function pause() {
  if (state.playerReady) state.player.pauseVideo();
}

// ── 제어 규칙 (docs/06-web-ui.md §4) ─────────────────────────────────
function controlling() {
  return state.started && state.faceControl;
}

function onPresence(msg) {
  // TODO(팀원3):
  //   - msg.state 가 이전 state.presence 와 같으면 무시 (상태가 바뀔 때만 동작)
  //   - state.presence 갱신 → renderBadge()
  //   - controlling() 이면 present → play(), absent → pause()
  void msg;
}

function onStatus(msg) {
  // TODO(팀원3):
  //   - state.status 갱신 → renderBadge(), 장치 점(device-dot) 갱신
  //   - offline / error 이면 controlling() 일 때 pause()
  void msg;
}

function onTelemetry(msg) {
  // TODO(팀원3): HUD 숫자 갱신, state.lastTelemetryAt = Date.now()
  void msg;
}

// ── MQTT ─────────────────────────────────────────────────────────────
function connectMqtt() {
  // TODO(팀원3):
  //   state.client = mqtt.connect(CONFIG.mqttUrl, { reconnectPeriod: 2000 })
  //   - "connect": broker-dot on, subscribe(`facepause/${state.deviceId}/#`)
  //   - "close" / "offline": broker-dot off, pause() (controlling 일 때)
  //   - "message": 토픽 마지막 부분(presence/status/telemetry)에 따라 JSON.parse 후 onXxx 호출
  //                v !== 1 이거나 파싱 실패면 무시
}

function switchDevice(deviceId) {
  // TODO(팀원3): 이전 장치 unsubscribe → 새 장치 subscribe, 상태 초기화(unknown), saveJson(DEVICE_KEY)
  state.deviceId = deviceId;
}

// ── 화면 그리기 ──────────────────────────────────────────────────────
function renderBadge() {
  // TODO(팀원3): 우선순위 offline/error > present/absent > unknown
  //   present "👀 시청 중" / absent "⏸ 자리 비움" / offline "⚠ 장치 오프라인" / error "⚠ 카메라 오류"
  //   $("presence-badge").dataset.state = ...; textContent = ...
}

function renderRecent() {
  // TODO(팀원3): loadJson(RECENT_KEY, []) 를 <li> 로, 클릭 시 loadVideo(id)
}

function renderDeviceSelect() {
  const select = $("device-select");
  select.innerHTML = "";
  for (const id of CONFIG.devices) {
    const opt = document.createElement("option");
    opt.value = id;
    opt.textContent = id;
    select.appendChild(opt);
  }
  select.value = state.deviceId;
}

// ── 이벤트 연결 ──────────────────────────────────────────────────────
function bindEvents() {
  $("url-form").addEventListener("submit", (e) => {
    e.preventDefault();
    const id = parseVideoId($("url-input").value.trim());
    const err = $("url-error");
    if (!id) {
      err.textContent = "올바른 유튜브 링크가 아닙니다";
      err.hidden = false;
      return;
    }
    err.hidden = true;
    loadVideo(id);
  });

  $("face-control").addEventListener("change", (e) => {
    state.faceControl = e.target.checked;
  });

  $("device-select").addEventListener("change", (e) => switchDevice(e.target.value));

  $("start-button").addEventListener("click", () => {
    state.started = true;
    $("start-overlay").hidden = true;
    // 시작 시점의 현재 상태를 바로 반영
    if (state.presence === "present" && state.status !== "offline" && state.status !== "error") play();
  });

  // HUD 가 오래 갱신되지 않으면 흐리게
  setInterval(() => {
    $("hud").dataset.stale = String(Date.now() - state.lastTelemetryAt > HUD_STALE_MS);
  }, 1000);
}

function main() {
  state.deviceId = loadJson(DEVICE_KEY, CONFIG.defaultDeviceId);
  renderDeviceSelect();
  renderRecent();
  renderBadge();
  bindEvents();
  connectMqtt();
}

main();
