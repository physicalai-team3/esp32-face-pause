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

// PRESENT 이고 마지막 검출 후 absent_ms 가 지났으면 → ABSENT.
// millis() 는 약 49일마다 0으로 돌아가므로 시각끼리 대소 비교하지 않고 경과 시간(뺄셈)으로 비교한다.
static bool check_absent_timeout(Debouncer *d, uint32_t now_ms)
{
    if (d->state == PRESENCE_PRESENT && (uint32_t)(now_ms - d->last_seen_ms) >= d->cfg.absent_ms) {
        d->state = PRESENCE_ABSENT;
        return true;
    }
    return false;
}

bool debounce_update(Debouncer *d, bool detected, uint32_t now_ms)
{
    if (!detected) {
        d->hit_streak = 0;
        return check_absent_timeout(d, now_ms);
    }

    d->last_seen_ms = now_ms;
    if (d->hit_streak < UINT8_MAX) d->hit_streak++;   // 계속 보이면 255에서 멈춤 (0으로 돌아가지 않게)

    if (d->state == PRESENCE_ABSENT && d->hit_streak >= d->cfg.present_frames) {
        d->state = PRESENCE_PRESENT;
        return true;
    }
    return false;
}

bool debounce_tick(Debouncer *d, uint32_t now_ms)
{
    // 프레임 없이 시간만 흐른 경우 (센서가 멈췄거나 느릴 때)
    return check_absent_timeout(d, now_ms);
}
