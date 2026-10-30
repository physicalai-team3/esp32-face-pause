// 팀원 2: 디바운스 상태 머신 구현
// 완료 기준: docker compose run --rm fw sh test.sh  → 전부 PASS
#include "debounce.h"

void debounce_init(Debouncer *d, DebounceConfig cfg)
{
    d->cfg = cfg;
    d->state = PRESENCE_ABSENT;
    d->hit_streak = 0;
    d->last_seen_ms = 0;
}

bool debounce_update(Debouncer *d, bool detected, uint32_t now_ms)
{
    // TODO(팀원2):
    //  - detected 이면: last_seen_ms 갱신, hit_streak 증가
    //      ABSENT 이고 hit_streak >= cfg.present_frames 이면 → PRESENT, return true
    //  - detected 가 아니면: hit_streak = 0
    //      PRESENT 이고 (now_ms - last_seen_ms) >= cfg.absent_ms 이면 → ABSENT, return true
    //  - millis() 는 약 49일마다 0으로 돌아간다 → 시간 비교는 항상 (now_ms - last_seen_ms) 처럼 뺄셈으로
    (void)d;
    (void)detected;
    (void)now_ms;
    return false;
}

bool debounce_tick(Debouncer *d, uint32_t now_ms)
{
    // TODO(팀원2): 프레임 없이 시간만 흐른 경우. PRESENT 이고 absent_ms 가 지났으면 → ABSENT, return true
    (void)d;
    (void)now_ms;
    return false;
}
