---
version: 1
slug: "docs-design-prototype-main-window-html"
primary_target: "docs/design/prototype/main-window.html"
related_targets: []
---

# 메인 창 (surface brief)

Mode: Operate. 대상: 터미널·IDE 옆에 FileScope를 띄운 개발자. 범위: P0 전체 + 오른쪽 '변경 내용' 패널(사용자 요구로 P1에서 승격: 텍스트 Diff, 이미지 전후 비교, 문서 내부 텍스트 블록·첨부 이미지 단위 변경). 데모 화면 미정 → 1366px 노트북과 프로젝터 대비 모두 견딘다.
결정: 오류·경고는 색이 아니라 반전 띠 + 경고 아이콘. Diff/버전 비교 화면만 git 관례(초록=추가, 빨강=삭제)를 따르고, 메인 창 변동은 빨강=증가/파랑=감소.
산출물: HTML/CSS 디자인 시안(`docs/design/prototype/main-window.html`). GTK3 구현은 Codex 몫이며 이 시안과 DESIGN.md가 승인 기준이다.

## Direction contract

THESIS: 파일 변경을 시세 틱으로 다룬다. 바뀐 행이 한 번 번쩍이고 변동 열에 증감이 남는다. 회색 사이드바+목록에 로그를 덧붙인 GNOME Files 배치를 거부한다.
OWN-WORLD: 남흑색 전광판 바탕, 빨강=생성·증가, 파랑=삭제·감소, 회색=보합, 노랑=선택·포커스 전용. 고정폭 숫자, 1px 괘선, 상자 대신 크기·굵기 대비. 모든 이벤트에 ▲ ▼ ● ✕ → ◦ 기호.
STORY: 개발자는 외부 변경이 즉시 보이고 무엇이 어떻게 바뀌었는지(줄, 이미지, 문서 안 첨부물까지) 바로 읽힌다는 걸 이해하고, 놓친 변경이 없다는 걸 믿고, 로그를 걸러 무엇이 언제 바뀌었는지 확인한다.
FIRST VIEWPORT: 헤더바(이동 버튼·브레드크럼·오른쪽 실시간 지표), 탭 줄, 왼쪽 240px 즐겨찾기+트리, 가운데 상세 목록(이름·크기·변동·이벤트·수정 시각·권한), 오른쪽 440px 변경 내용 패널(선택한 파일의 직전 버전 대비 변경, 접기 가능), 하단 232px 체결식 이벤트 로그+세션 집계, 상태 표시줄. 주 작업은 목록, 증거는 변경 내용 패널.
FORM: 증권 시세 전광판, 자체 후보 목록 5번째. seed:1ca032e434b7648d
FINISH: unreviewed and undocumented is unfinished; this build ends with the finish review, the verdict, DESIGN.md, and every shipping raster carrying its provenance

Signature interaction: 외부 이벤트 도착 → 행 배경 틱 플래시(빨강/파랑, 약 600ms 감쇠) → 변동 열 증감 유지 → 로그 맨 위에 같은 줄 추가. 모션 감소 시 플래시 없이 색만 남는다.
