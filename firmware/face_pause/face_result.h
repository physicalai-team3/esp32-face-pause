// ★ 계약 (공동 소유): face_sensor(팀원 1) → presence_link(팀원 2) 로 넘기는 쪽지 양식
// 수정 시 [contract] 커밋 + 두 사람 모두 확인. 필드 추가만 허용, 이름 변경·삭제 금지. (docs/03-messages.md §5)
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool     detected;     // score >= FP_SCORE_THRESHOLD 인 얼굴이 1개 이상
    uint8_t  count;        // 검출된 얼굴 수 (FOMO 중심점 개수)
    float    best_score;   // 가장 높은 점수 0~1 (없으면 0)
    uint16_t infer_ms;     // 이번 프레임 추론 시간
    uint32_t frame_id;     // 1씩 증가
    uint32_t ts_ms;        // millis()
} face_result_t;
