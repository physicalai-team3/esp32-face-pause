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
const STATUS_GRACE_MS = 500;         // 구독 직후 status 가 presence 보다 늦게 올 수 있어 잠깐 기다림
const VIDEO_ID_RE = /^[A-Za-z0-9_-]{11}$/;

const state = {
  player: null,          // YT.Player
  playerReady: false,
  pendingVideoId: null,  // 플레이어 준비 전에 불러온 영상
  started: false,        // ⑧ 시작 버튼을 눌렀나
  faceControl: true,     // ⑥ 토글
  deviceId: CONFIG.defaultDeviceId,
  client: null,          // mqtt.js 클라이언트
  broker: "connecting",  // connecting | up | down
  presence: "unknown",   // present | absent | unknown
  status: "unknown",     // online | offline | error | unknown
  statusReason: "",      // status=error 일 때 reason
  lastTelemetryAt: 0,
  graceTimer: 0,
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
  const s = String(input || "").trim();
  if (VIDEO_ID_RE.test(s)) return s;

  let url;
  try {
    url = new URL(/^[a-z][a-z0-9+.-]*:\/\//i.test(s) ? s : `https://${s}`);
  } catch {
    return null;
  }
  const host = url.hostname.replace(/^(www|m|music)\./, "");
  let id = null;
  if (host === "youtu.be") {
    id = url.pathname.split("/")[1];
  } else if (host === "youtube.com" || host === "youtube-nocookie.com") {
    if (url.pathname === "/watch") {
      id = url.searchParams.get("v");
    } else {
      const m = url.pathname.match(/^\/(?:shorts|embed|live|v)\/([^/]+)/);
      if (m) id = m[1];
    }
  }
  return id && VIDEO_ID_RE.test(id) ? id : null;
}

// ── 안내 문구 ────────────────────────────────────────────────────────
function showMessage(text) {
  const el = $("url-error");
  el.textContent = text;
  el.hidden = !text;
}

// ── ④ 플레이어 ───────────────────────────────────────────────────────
// https://developers.google.com/youtube/iframe_api_reference#onError
const PLAYER_ERRORS = {
  2: "영상 ID가 올바르지 않습니다",
  5: "브라우저에서 이 영상을 재생할 수 없습니다",
  100: "영상을 찾을 수 없습니다 (삭제되었거나 비공개)",
  101: "이 영상은 외부 재생이 허용되지 않습니다",
  150: "이 영상은 외부 재생이 허용되지 않습니다",
  153: "페이지 주소를 확인할 수 없어 재생이 거부되었습니다 (file:// 대신 http://localhost:8080 으로 여세요)",
};

// YouTube IFrame API 스크립트가 로드되면 자동으로 호출된다
window.onYouTubeIframeAPIReady = function () {
  // 초기 영상: 최근 영상 1번 → 없으면 .env 의 DEFAULT_VIDEO_ID
  const recent = loadRecent();
  const initial = (recent[0] && recent[0].id) || CONFIG.defaultVideoId || "";
  state.player = new YT.Player("player", {
    videoId: initial || undefined,
    playerVars: { playsinline: 1, rel: 0, origin: location.origin },
    events: {
      onReady: () => {
        state.playerReady = true;
        // 플레이어가 뜨기 전에 다른 영상을 불러왔으면 그 영상으로
        if (state.pendingVideoId && state.pendingVideoId !== initial) state.player.cueVideoById(state.pendingVideoId);
        state.pendingVideoId = null;
        syncPlayer();
      },
      onError: (e) => showMessage(PLAYER_ERRORS[e.data] || `재생 오류 (코드 ${e.data})`),
      onStateChange: (e) => {
        // 재생/로드된 영상의 제목을 최근 목록에 채운다
        if (e.data === YT.PlayerState.PLAYING || e.data === YT.PlayerState.CUED) rememberTitle();
      },
      // 브라우저가 소리 있는 재생을 막으면 다시 시작 버튼을 눌러야 한다
      onAutoplayBlocked: () => {
        state.started = false;
        $("start-overlay").hidden = false;
      },
    },
  });
};

function loadVideo(videoId) {
  addRecent(videoId, "");
  renderRecent();
  showMessage("");
  if (!state.playerReady) {
    state.pendingVideoId = videoId;   // 플레이어 onReady 에서 불러온다
    return;
  }
  // 얼굴이 있으면 load(바로 재생), 없으면 cue(로드만 하고 정지 상태로 대기)
  // cueVideoById 직후의 playVideo() 는 무시될 수 있어서 처음부터 나눠 부른다
  if (shouldPlay()) state.player.loadVideoById(videoId);
  else state.player.cueVideoById(videoId);
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

// 장치 쪽 문제로 재생하면 안 되는 상태
function blocked() {
  return state.broker !== "up" || state.status === "offline" || state.status === "error";
}

function shouldPlay() {
  return controlling() && !blocked() && state.presence === "present";
}

// 현재 상태대로 플레이어를 맞춘다 (시작, 토글 ON, 장치 복구 때)
function syncPlayer() {
  if (!controlling()) return;
  if (shouldPlay()) play();
  else pause();
}

function onPresence(msg) {
  if (msg.state !== "present" && msg.state !== "absent") return;
  // 오프라인 장치의 presence 는 끊기기 전 retained 값이다. 재접속하면 online 다음에 새 값이 온다
  if (state.status === "offline") return;
  if (msg.state === state.presence) return;   // 상태가 바뀔 때만 동작
  state.presence = msg.state;
  renderBadge();
  if (!controlling()) return;
  if (msg.state === "absent") {
    pause();
  } else if (state.status === "unknown") {
    // retained 메시지는 토픽끼리 순서가 보장되지 않는다 → status(offline 일 수도)를 잠깐 기다림
    clearTimeout(state.graceTimer);
    state.graceTimer = setTimeout(syncPlayer, STATUS_GRACE_MS);
  } else if (!blocked()) {
    play();
  }
}

function onStatus(msg) {
  if (!["online", "offline", "error"].includes(msg.state)) return;
  const wasDown = state.status === "offline" || state.status === "error";
  state.status = msg.state;
  state.statusReason = msg.state === "error" ? msg.reason || "" : "";
  if (msg.state === "offline") {
    // 남아 있는 presence 는 끊기기 전 값이다. 장치가 재접속하면 presence 를 다시 보내므로 그때 반영한다
    state.presence = "unknown";
  }
  if (msg.state !== "online") {
    if (controlling()) pause();
  } else if (wasDown) {
    // error → online (sensor_timeout 복구): 같은 접속이라 presence 는 다시 오지 않는다 → 현재 값으로 맞춤
    syncPlayer();
  }
  renderBadge();
}

function onTelemetry(msg) {
  const fmt = (v, digits) => (typeof v === "number" ? (digits == null ? String(v) : v.toFixed(digits)) : "-");
  $("hud-faces").textContent = fmt(msg.faces);
  $("hud-score").textContent = fmt(msg.score, 2);
  $("hud-fps").textContent = fmt(msg.fps, 1);
  $("hud-infer").textContent = fmt(msg.infer_ms);
  $("hud-rssi").textContent = fmt(msg.rssi);
  state.lastTelemetryAt = Date.now();
  $("hud").dataset.stale = "false";
}

// ── MQTT ─────────────────────────────────────────────────────────────
function deviceTopic(deviceId) {
  return `facepause/${deviceId}/#`;
}

function connectMqtt() {
  if (typeof mqtt === "undefined") {
    state.broker = "down";
    showMessage("mqtt.js 를 불러오지 못했습니다 (인터넷 연결 확인)");
    renderBadge();
    return;
  }
  state.client = mqtt.connect(CONFIG.mqttUrl, { reconnectPeriod: 2000, connectTimeout: 5000 });

  state.client.on("connect", () => {
    state.broker = "up";
    // 재접속 때도 다시 구독 → retained 로 현재 상태를 바로 받는다
    state.client.subscribe(deviceTopic(state.deviceId), { qos: 1 });
    renderBadge();
  });

  const onDown = () => {
    if (state.broker === "down") return;
    state.broker = "down";
    // 다시 붙으면 retained 로 받으므로 지금 값은 버린다
    state.presence = "unknown";
    state.status = "unknown";
    if (controlling()) pause();
    renderBadge();
  };
  state.client.on("close", onDown);
  state.client.on("offline", onDown);

  state.client.on("message", (topic, payload) => {
    const [root, deviceId, kind] = topic.split("/");
    if (root !== "facepause" || deviceId !== state.deviceId) return;   // 장치를 바꾸기 직전 메시지 등
    let msg;
    try {
      msg = JSON.parse(new TextDecoder().decode(payload));
    } catch {
      return;   // 빈 페이로드(retained 삭제)나 잘못된 JSON
    }
    if (!msg || msg.v !== 1 || (msg.device && msg.device !== deviceId)) return;
    if (kind === "presence") onPresence(msg);
    else if (kind === "status") onStatus(msg);
    else if (kind === "telemetry") onTelemetry(msg);
  });
}

function switchDevice(deviceId) {
  if (deviceId === state.deviceId) return;
  const client = state.client;
  if (client && client.connected) client.unsubscribe(deviceTopic(state.deviceId));

  state.deviceId = deviceId;
  state.presence = "unknown";
  state.status = "unknown";
  state.statusReason = "";
  clearTimeout(state.graceTimer);
  if (controlling()) pause();
  resetHud();
  renderBadge();
  saveJson(DEVICE_KEY, deviceId);

  if (client && client.connected) client.subscribe(deviceTopic(deviceId), { qos: 1 });
}

// ── 최근 영상 ────────────────────────────────────────────────────────
function loadRecent() {
  const list = loadJson(RECENT_KEY, []);
  if (!Array.isArray(list)) return [];
  return list
    .map((item) => (typeof item === "string" ? { id: item, title: "" } : item))
    .filter((item) => item && VIDEO_ID_RE.test(item.id));
}

function addRecent(videoId, title) {
  const list = loadRecent().filter((item) => item.id !== videoId);
  const old = loadRecent().find((item) => item.id === videoId);
  list.unshift({ id: videoId, title: title || (old && old.title) || "" });
  saveJson(RECENT_KEY, list.slice(0, RECENT_MAX));
}

function rememberTitle() {
  const data = state.player.getVideoData ? state.player.getVideoData() : null;
  if (!data || !data.video_id || !data.title) return;
  const list = loadRecent();
  const item = list.find((i) => i.id === data.video_id);
  if (!item || item.title === data.title) return;
  item.title = data.title;
  saveJson(RECENT_KEY, list);
  renderRecent();
}

// ── 화면 그리기 ──────────────────────────────────────────────────────
const ERROR_TEXT = {
  camera_init: "⚠ 카메라 오류",
  model_init: "⚠ 모델 오류",
  sensor_timeout: "⚠ 센서 응답 없음",
};

function renderBadge() {
  // 우선순위: 브로커 끊김 > offline > error > present/absent > unknown
  let key = "unknown";
  let text = "연결 대기";
  if (state.broker === "down") {
    key = "offline";
    text = "⚠ 브로커 연결 끊김";
  } else if (state.status === "offline") {
    key = "offline";
    text = "⚠ 장치 오프라인";
  } else if (state.status === "error") {
    key = "error";
    text = ERROR_TEXT[state.statusReason] || "⚠ 장치 오류";
  } else if (state.presence === "present") {
    key = "present";
    text = "👀 시청 중";
  } else if (state.presence === "absent") {
    key = "absent";
    text = "⏸ 자리 비움";
  }
  const badge = $("presence-badge");
  badge.dataset.state = key;
  badge.textContent = text;

  $("broker-dot").dataset.state = state.broker === "up" ? "on" : state.broker === "down" ? "error" : "off";
  $("device-dot").dataset.state =
    state.status === "online" ? "on" : state.status === "offline" || state.status === "error" ? "error" : "off";
}

function resetHud() {
  for (const id of ["hud-faces", "hud-score", "hud-fps", "hud-infer", "hud-rssi"]) $(id).textContent = "-";
  state.lastTelemetryAt = 0;
  $("hud").dataset.stale = "true";
}

function renderRecent() {
  const ul = $("recent-list");
  ul.replaceChildren();
  for (const item of loadRecent()) {
    const li = document.createElement("li");
    const button = document.createElement("button");
    button.type = "button";
    button.textContent = item.title || item.id;   // 제목은 외부 문자열이라 textContent 로만
    button.title = item.id;
    button.addEventListener("click", () => loadVideo(item.id));
    li.appendChild(button);
    ul.appendChild(li);
  }
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
    if (!id) {
      showMessage("올바른 유튜브 링크가 아닙니다");
      return;
    }
    loadVideo(id);
  });

  $("face-control").addEventListener("change", (e) => {
    state.faceControl = e.target.checked;
    syncPlayer();   // 다시 켜면 현재 상태로 바로 맞춤
  });

  $("device-select").addEventListener("change", (e) => switchDevice(e.target.value));

  $("start-button").addEventListener("click", () => {
    state.started = true;
    $("start-overlay").hidden = true;
    // 시작 시점의 현재 상태를 바로 반영
    syncPlayer();
  });

  // HUD 가 오래 갱신되지 않으면 흐리게
  setInterval(() => {
    $("hud").dataset.stale = String(Date.now() - state.lastTelemetryAt > HUD_STALE_MS);
  }, 1000);
}

function main() {
  const saved = loadJson(DEVICE_KEY, CONFIG.defaultDeviceId);
  state.deviceId = CONFIG.devices.includes(saved) ? saved : CONFIG.defaultDeviceId;
  renderDeviceSelect();
  renderRecent();
  renderBadge();
  bindEvents();
  connectMqtt();
}

main();
