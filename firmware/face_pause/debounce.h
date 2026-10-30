// 팀원 2: 디바운스 상태 머신 (docs/01-architecture.md §4)
// Arduino에 의존하지 않는 순수 로직 → PC에서 단위 테스트 (firmware/test.sh)
#pragma once

#include <stdbool.h>
#include <stdint.h>

enum PresenceState {
    PRESENCE_ABSENT = 0,
    PRESENCE_PRESENT = 1,
};

struct DebounceConfig {
    uint8_t  present_frames;  // 연속 N프레임 검출 → PRESENT   (FP_PRESENT_FRAMES)
    uint32_t absent_ms;       // 마지막 검출 후 N ms 없음 → ABSENT (FP_ABSENT_MS)
};

struct Debouncer {
    DebounceConfig cfg;
    PresenceState  state;
    uint8_t        hit_streak;    // 연속 검출 프레임 수
    uint32_t       last_seen_ms;  // 마지막으로 얼굴이 검출된 시각
};

// 초기 상태는 ABSENT
void debounce_init(Debouncer *d, DebounceConfig cfg);

// 프레임 결과가 들어올 때마다 호출. 상태가 바뀌었으면 true.
bool debounce_update(Debouncer *d, bool detected, uint32_t now_ms);

// 프레임이 안 들어올 때도 주기적으로 호출 (ABSENT 타임아웃 확인). 상태가 바뀌었으면 true.
bool debounce_tick(Debouncer *d, uint32_t now_ms);

static inline const char *presence_state_str(PresenceState s)
{
    return s == PRESENCE_PRESENT ? "present" : "absent";
}
