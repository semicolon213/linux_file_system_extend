---
name: FileScope
description: 외부 변경을 시세 틱으로 보여 주는 Linux 파일 탐색기 (GTK3, 다크 P0)
colors:
  bg: "#10151c"
  bg-2: "#141b24"
  raise: "#18202a"
  hover: "#1c2632"
  line: "#263241"
  line-soft: "#1b2430"
  line-float: "#33435a"
  ink: "#e6e9ee"
  ink-2: "#b9c1cc"
  dim: "#8a94a3"
  up: "#f0525a"
  up-wash: "rgba(240,82,90,.11)"
  up-flash: "rgba(240,82,90,.42)"
  down: "#4d9bff"
  down-wash: "rgba(77,155,255,.11)"
  down-flash: "rgba(77,155,255,.40)"
  sel: "#f5c84b"
  sel-wash: "rgba(245,200,75,.10)"
  add: "#3fb950"
  add-wash: "rgba(63,185,80,.14)"
  add-ink: "#b6f0bf"
  del: "#f85149"
  del-wash: "rgba(248,81,73,.14)"
  del-ink: "#ffc2bd"
  media-well: "#0c1016"
  scroll-thumb: "#2f3d4f"
typography:
  body:
    fontFamily: "Ubuntu, Noto Sans KR, Noto Sans CJK KR, Cantarell, system-ui, sans-serif"
    fontSize: "13px"
    fontWeight: 400
    lineHeight: 1.45
  title:
    fontFamily: "Ubuntu, Noto Sans KR, Noto Sans CJK KR, Cantarell, system-ui, sans-serif"
    fontSize: "16px"
    fontWeight: 600
  row-name:
    fontFamily: "Ubuntu, Noto Sans KR, Noto Sans CJK KR, Cantarell, system-ui, sans-serif"
    fontSize: "14px"
    fontWeight: 500
  heading:
    fontFamily: "Ubuntu, Noto Sans KR, Noto Sans CJK KR, Cantarell, system-ui, sans-serif"
    fontSize: "13px"
    fontWeight: 600
  label:
    fontFamily: "Ubuntu, Noto Sans KR, Noto Sans CJK KR, Cantarell, system-ui, sans-serif"
    fontSize: "12px"
    fontWeight: 500
  group-label:
    fontFamily: "Ubuntu, Noto Sans KR, Noto Sans CJK KR, Cantarell, system-ui, sans-serif"
    fontSize: "11px"
    fontWeight: 600
  data:
    fontFamily: "Ubuntu Mono, Noto Sans Mono, DejaVu Sans Mono, ui-monospace, monospace"
    fontSize: "13px"
    fontFeature: "tnum"
  data-dense:
    fontFamily: "Ubuntu Mono, Noto Sans Mono, DejaVu Sans Mono, ui-monospace, monospace"
    fontSize: "12.5px"
    lineHeight: 1.6
    fontFeature: "tnum"
  event-code:
    fontFamily: "Ubuntu Mono, Noto Sans Mono, DejaVu Sans Mono, ui-monospace, monospace"
    fontSize: "12px"
    letterSpacing: "0.02em"
  figure:
    fontFamily: "Ubuntu Mono, Noto Sans Mono, DejaVu Sans Mono, ui-monospace, monospace"
    fontSize: "17px"
    fontWeight: 600
    fontFeature: "tnum"
  figure-lg:
    fontFamily: "Ubuntu Mono, Noto Sans Mono, DejaVu Sans Mono, ui-monospace, monospace"
    fontSize: "19px"
    fontWeight: 600
    fontFeature: "tnum"
rounded:
  hair: "2px"
  xs: "3px"
  sm: "4px"
  md: "6px"
  pill: "12px"
spacing:
  2xs: "4px"
  xs: "6px"
  sm: "8px"
  md: "12px"
  lg: "16px"
  xl: "32px"
components:
  headerbar:
    backgroundColor: "{colors.raise}"
    textColor: "{colors.ink}"
    height: "48px"
    padding: "0 12px"
  button:
    textColor: "{colors.ink-2}"
    rounded: "{rounded.sm}"
    height: "30px"
    padding: "0 8px"
  button-hover:
    backgroundColor: "{colors.hover}"
    textColor: "{colors.ink}"
  button-pressed:
    backgroundColor: "{colors.hover}"
    textColor: "{colors.ink}"
  button-primary:
    textColor: "{colors.ink}"
    rounded: "{rounded.sm}"
    height: "30px"
  search-field:
    textColor: "{colors.ink}"
    rounded: "{rounded.sm}"
    height: "30px"
    width: "220px"
  tab-bar:
    backgroundColor: "{colors.bg}"
    height: "34px"
  tab:
    textColor: "{colors.dim}"
    padding: "0 10px 0 14px"
  tab-selected:
    textColor: "{colors.ink}"
  sidebar:
    width: "240px"
    textColor: "{colors.ink-2}"
  sidebar-node-selected:
    backgroundColor: "{colors.sel-wash}"
    textColor: "{colors.ink}"
  file-list-header:
    backgroundColor: "{colors.bg}"
    textColor: "{colors.dim}"
    typography: "{typography.label}"
    height: "32px"
  file-row:
    textColor: "{colors.ink}"
    height: "34px"
    padding: "0 12px"
  file-row-hover:
    backgroundColor: "{colors.hover}"
  file-row-selected:
    backgroundColor: "{colors.sel-wash}"
  file-row-tick-up:
    backgroundColor: "{colors.up-flash}"
  file-row-tick-down:
    backgroundColor: "{colors.down-flash}"
  file-row-recent-up:
    backgroundColor: "{colors.up-wash}"
  file-row-recent-down:
    backgroundColor: "{colors.down-wash}"
  file-row-gone:
    textColor: "{colors.dim}"
  band-alert:
    backgroundColor: "{colors.ink}"
    textColor: "{colors.bg}"
    padding: "8px 12px"
  band-quiet:
    backgroundColor: "{colors.raise}"
    textColor: "{colors.ink}"
    padding: "8px 12px"
  inspector:
    backgroundColor: "{colors.bg-2}"
    width: "440px"
  diff-block:
    typography: "{typography.data-dense}"
    rounded: "{rounded.sm}"
  diff-line-add:
    backgroundColor: "{colors.add-wash}"
    textColor: "{colors.add}"
  diff-line-del:
    backgroundColor: "{colors.del-wash}"
    textColor: "{colors.del}"
  diff-hunk:
    backgroundColor: "{colors.raise}"
    textColor: "{colors.dim}"
    padding: "2px 8px"
  prose-ins:
    backgroundColor: "{colors.add-wash}"
    textColor: "{colors.add-ink}"
  prose-del:
    backgroundColor: "{colors.del-wash}"
    textColor: "{colors.del-ink}"
  image-well:
    backgroundColor: "{colors.media-well}"
    rounded: "{rounded.sm}"
  segmented:
    textColor: "{colors.dim}"
    rounded: "{rounded.sm}"
    padding: "3px 10px"
  segmented-pressed:
    backgroundColor: "{colors.hover}"
    textColor: "{colors.ink}"
  event-log:
    backgroundColor: "{colors.bg-2}"
    height: "232px"
  event-row:
    typography: "{typography.data-dense}"
    height: "24px"
    padding: "0 12px"
  chip:
    textColor: "{colors.dim}"
    rounded: "{rounded.pill}"
    height: "24px"
    padding: "0 10px"
  chip-pressed:
    textColor: "{colors.ink}"
  tally:
    typography: "{typography.figure-lg}"
    width: "260px"
  statusbar:
    backgroundColor: "{colors.raise}"
    textColor: "{colors.dim}"
    height: "28px"
  menu:
    backgroundColor: "{colors.raise}"
    textColor: "{colors.ink}"
    rounded: "{rounded.md}"
    padding: "4px"
  toast:
    backgroundColor: "{colors.raise}"
    textColor: "{colors.ink}"
    rounded: "{rounded.md}"
    padding: "8px 8px 8px 14px"
  tray-icon:
    size: "18px"
  summary-window:
    backgroundColor: "{colors.bg}"
    rounded: "{rounded.md}"
    width: "360px"
  summary-headerbar:
    backgroundColor: "{colors.raise}"
    textColor: "{colors.ink}"
    typography: "{typography.heading}"
    height: "40px"
    padding: "0 6px 0 14px"
  summary-status:
    textColor: "{colors.ink}"
    typography: "{typography.title}"
    padding: "16px 16px 12px"
  summary-path:
    textColor: "{colors.ink}"
    typography: "{typography.data}"
    padding: "7px 16px"
  summary-tally:
    typography: "{typography.figure}"
    padding: "12px 16px"
  summary-tally-zero:
    textColor: "{colors.dim}"
  summary-row:
    height: "44px"
    padding: "0 16px"
  summary-band:
    backgroundColor: "{colors.raise}"
    textColor: "{colors.ink-2}"
    rounded: "{rounded.sm}"
    padding: "10px 12px"
  summary-button:
    textColor: "{colors.ink-2}"
    rounded: "{rounded.sm}"
    height: "30px"
    padding: "0 12px"
---

# Design System: FileScope

이 문서는 승인된 시안 `docs/design/prototype/main-window.html`에서 실제로 구현된 값만 옮겨 적은 것이다. 시안과 이 문서가 GTK3 구현(Codex)의 승인 기준이다. 토큰 값은 위 frontmatter가 정본이며, 본문은 쓰임새와 규칙을 설명한다.

## Overview

**Creative North Star: "시세 전광판 (The Quote Board)"**

FileScope는 파일 변경을 증권 시세 틱처럼 다룬다. 남흑색 바탕 위에서 외부 프로그램이 파일을 바꾸면 그 행이 한 번 번쩍이고, 변동 열에 부호 붙은 증감이 남고, 같은 줄이 하단 체결 로그 맨 위에 쌓인다. 화면은 회색 사이드바에 목록을 얹은 일반 파일 관리자가 아니라, 숫자가 고정폭으로 줄 맞춰 흐르는 전광판이다.

밀도는 높고 장식은 없다. 위계는 상자가 아니라 글자 크기와 굵기의 대비로 만든다. 구획은 1px 괘선뿐이고 카드는 쓰지 않는다. 색은 의미가 정해진 몇 가지만 쓴다. 빨강은 생성·증가, 파랑은 삭제·감소, 회색은 보합, 노랑은 선택과 포커스다. 모든 이벤트에는 색과 별개로 읽히는 기호(▲ ▼ ● ✕ → ◦)가 붙는다. 이 기호는 아이콘이 아니라 표기법이다.

데모는 1366px 노트북과 프로젝터 양쪽에서 견뎌야 한다. 다크가 P0이고, 라이트 테마(P1)는 아직 정하지 않았다(아래 Colors의 미결 항목 참고).

**Key Characteristics:**
- 남흑색 단일 바탕, 표면 차이는 명도 몇 단계로만 낸다.
- 빨강=생성·증가, 파랑=삭제·감소, 회색=보합, 노랑=선택·포커스 전용.
- 모든 이벤트에 색 외 기호를 단다: ▲ 증가, ▼ 감소, ● 생성, ✕ 삭제, → 이동, ◦ 속성.
- 1px 괘선, 카드 없음, 크기·굵기 대비로 위계를 만든다.
- 데이터와 측정값에만 고정폭 숫자.
- 시그니처: 외부 이벤트 → 행 틱 플래시 → 변동 열 유지 → 로그 맨 위 추가.

## Colors

남흑색 바탕에 의미가 고정된 시세 색 두 개, 선택용 노랑 하나, Diff 화면 전용 git 색 두 개를 얹는다.

### Primary
- **시세 빨강 (Ticker Red)** (`up`): 생성(●)과 크기 증가(▲). 변동 열 숫자, 이벤트 열, 로그의 이벤트 칸, 집계의 생성·수정 숫자. 틱 플래시는 `up-flash`(42%)에서 시작해 `up-wash`(11%)로 내려앉는다.
- **시세 파랑 (Ticker Blue)** (`down`): 삭제(✕)와 크기 감소(▼). 빨강과 같은 자리에 쓰고, 플래시·잔상은 `down-flash`(40%) → `down-wash`(11%). 삭제되어 남아 있는 행의 취소선 색도 이 파랑이다.

### Secondary
- **선택 노랑 (Selection Amber)** (`sel`): 선택과 포커스에만 쓴다. 선택 행·선택 트리 노드의 위아래 1px 괘선, 포커스 링(2px), 선택 탭의 밑줄(2px), 검색 칸 포커스 테두리, 슬라이더 강조색, 텍스트 커서와 텍스트 선택. 선택 행 바탕은 `sel-wash`(10%).

### Tertiary
- **Diff 초록 (Git Added)** (`add`, `add-wash`, `add-ink`): 변경 내용 패널의 Diff와 버전 비교에서만 "추가"를 뜻한다. 줄 Diff의 + 기호, 추가 줄 바탕, 문서 단어 단위 삽입(`add-ink` 글자 + `add-wash` 바탕 + `add` 밑줄), 현재 버전 그림 테두리(50% 알파).
- **Diff 빨강 (Git Removed)** (`del`, `del-wash`, `del-ink`): 같은 화면에서 "삭제"를 뜻한다. − 기호, 삭제 줄 바탕, 단어 단위 삭제(`del-ink` 글자 + 취소선 `del`), 이전 버전 그림 테두리(50% 알파).

### Neutral
- **남흑 바탕 (Ink-Navy Ground)** (`bg`): 창, 목록, 탭 줄, 열 머리글.
- **패널 바탕** (`bg-2`): 변경 내용 패널과 하단 로그. 목록보다 한 단계 밝다.
- **떠 있는 면** (`raise`): 헤더바, 상태 표시줄, 조용한 띠, 컨텍스트 메뉴, Diff 덩어리 머리(hunk), 코드 조각 바탕, 스켈레톤.
- **호버** (`hover`): 행·버튼·노드 호버와 눌린 토글 바탕.
- **괘선** (`line`): 구역 경계, 버튼·입력 테두리, 열 머리글 밑줄. **옅은 괘선** (`line-soft`): 목록 행 사이, 탭 사이.
- **잉크** (`ink`): 본문 글자. 반전 띠에서는 바탕색이 된다. 감시 중 표시 점도 `ink`(흰 점)다. **보조 잉크** (`ink-2`): 버튼 글자, 패널 보조 문장. **흐린 잉크** (`dim`): 보합(—, ◦, →), 라벨, 시각, 권한 문자열.
- **떠 있는 면 테두리** (`line-float`): 메뉴, 토스트, 요약 창처럼 다른 내용 위에 뜨는 면의 1px 테두리. 시안 CSS에는 아직 리터럴 `#33435a`로 적혀 있다.
- **그림 바닥** (`media-well`): 이미지 비교 칸 바탕. **스크롤바** (`scroll-thumb`).

### Named Rules

**The Fixed Meaning Rule.** 빨강은 생성·증가, 파랑은 삭제·감소만 뜻한다. 오류와 위험에는 절대 빨강을 쓰지 않는다. 오류·경고는 반전 띠와 경고 아이콘으로 알린다.

**The Yellow Is Attention Rule.** 노랑은 "지금 내가 보고 있는 것"(선택, 포커스)에만 쓴다. 상태, 경고, 강조 장식에 노랑을 쓰지 않는다.

**The Two Dialects Rule.** 메인 창의 변동은 시세 관례(빨강▲/파랑▼), 변경 내용 패널의 Diff는 git 관례(초록+/빨강−)를 따른다(결정 B). 두 방언은 각자 영역에서만 쓰고 섞지 않는다. 두 방언 모두 기호를 함께 단다. 시세 기호(▲ ▼ ● ✕)는 Diff 영역에 쓰지 않는다. 복합 문서 항목 태그도 Diff 체계(`+ 추가`, `± 교체`)를 따른다.

**받아들인 위험 (결정 B).** `up`(시세 빨강)과 `del`(Diff 빨강)은 눈으로 거의 구분되지 않는다. 뜻이 다른 두 빨강은 색이 아니라 영역(메인 창 변동 대 변경 내용 패널 Diff)과 기호(▲ 대 + / −)로 구분한다. 한 화면 한 영역에 두 빨강을 나란히 두지 않는다.

**The Glyph Always Rule.** 색이 붙는 모든 이벤트와 Diff 줄에는 기호가 함께 간다. 색을 빼도 뜻이 읽혀야 한다.

### 미결: 라이트 테마 (P1)

라이트 테마는 결정되지 않았다. 의도한 번역은 "신문 증권면": 흰 종이 바탕에 같은 빨강/파랑 역할, 검은 괘선이다. 토큰 세트는 아직 없으며, 라이트 값을 만들 때 이 문서를 다시 갱신한다.

## Typography

**Body Font:** Ubuntu (Noto Sans KR, Noto Sans CJK KR, Cantarell, system sans로 대체)
**Label/Mono Font:** Ubuntu Mono (Noto Sans Mono, DejaVu Sans Mono로 대체)

**Character:** Ubuntu 산세리프가 이름과 문장을, Ubuntu Mono가 숫자를 맡는다. 고정폭은 데이터와 측정값에만 써서 숫자 열이 자릿수대로 줄을 맞춘다.

### Hierarchy
- **Title** (600, 16px): 변경 내용 패널의 파일 이름, 빈 상태 제목.
- **Row Name** (500, 14px): 목록의 파일 이름. 폴더는 600. 현재 경로의 마지막 브레드크럼도 600/14px.
- **Heading** (600, 13px): "이벤트 로그", "이번 세션" 같은 구역 제목.
- **Body** (400, 13px, 1.45): 기본 글자, 트리 노드, 버튼.
- **Label** (500, 12px): 열 머리글(흐린 잉크), 지표 라벨, 패널 메타, 칩, 상태 표시줄.
- **Group Label** (600, 11px, 흐린 잉크): 사이드바의 "즐겨찾기 / 폴더 / 시스템" 묶음 이름.
- **Data** (mono, 13px, tnum): 크기, 변동, 수정 시각, 권한. 변동 열에 색이 붙으면 600.
- **Data Dense** (mono, 12.5px, 1.6, tnum): 줄 Diff, 이벤트 로그 행.
- **Event Code** (mono, 12px, 자간 0.02em): 목록의 이벤트 열(`▲ MODIFY`).
- **Figure** (mono 600, 17px): 헤더바 실시간 지표 숫자. **Figure Large** (mono 600, 19px): 세션 집계 숫자. 기호는 13px로 숫자 앞에 붙는다.

### Named Rules

**The Mono Is For Numbers Rule.** 고정폭은 데이터와 측정값(크기, 시각, 권한, 해시, 경로, Diff 본문, 지표)에만 쓴다. 제목·버튼·안내 문장은 산세리프다.

**The Contrast Not Boxes Rule.** 위계는 16/14/13/12/11px와 400/500/600 굵기 대비로 만든다. 묶음을 강조하려고 상자나 바탕을 추가하지 않는다.

## Layout

창은 위에서 아래로 다섯 줄이다: 헤더바 48px, 탭 줄 34px, 작업 영역(남는 높이), 이벤트 로그 232px, 상태 표시줄 28px. 작업 영역은 세 칸이다: 사이드바 240px, 파일 목록(남는 폭), 변경 내용 패널 440px. 패널은 헤더바 토글로 접으며, 접으면 목록이 폭을 가져간다. 로그는 이벤트 표(남는 폭)와 세션 집계 260px 두 칸이다. 최소 창 폭은 1024px.

목록 열 폭: 이름(남는 폭), 크기 92px, 변동 112px, 이벤트 96px, 수정 시각 122px, 권한 104px. 크기·변동은 오른쪽 정렬.

행 높이: 목록 34px, 열 머리글 32px, 로그 24px, 로그 도구줄 38px. 컨트롤 높이: 버튼·검색 30px, 띠 안 버튼 26px, 칩 24px.

간격은 4/6/8/12/16px 리듬이다. 셀 좌우 12px, 패널 안쪽 16px, 트리 깊이당 들여쓰기 14px. 빈 상태 블록만 56px 32px 여백에 폭 520px 이하.

창 크기 대응:
- 폭 1439px 이하: 패널 380px.
- 폭 1279px 이하: 사이드바 200px, 패널 320px, 헤더 검색 160px, 반영 지연 지표 숨김, 목록의 이벤트·권한 열 숨김.
- 높이 800px 이하: 로그 196px.

## Elevation & Depth

평면이다. 깊이는 명도 단계(`media-well` < `bg` < `bg-2` < `raise` < `hover`)와 1px 괘선으로만 낸다. 그림자는 화면 위에 떠서 다른 내용을 가리는 두 요소에만 쓴다.

### Shadow Vocabulary
- **떠 있는 층** (`box-shadow: 0 8px 24px rgba(0,0,0,.45)`): 컨텍스트 메뉴와 토스트. 테두리는 1px `line-float`.
- **떠 있는 창** (`box-shadow: 0 12px 32px rgba(0,0,0,.55)`): 트레이 요약 창. GTK에서는 창 관리자·CSD 장식이 그리는 그림자이므로 직접 그리지 않는다(시안은 근사치).
- 그 외에는 그림자를 쓰지 않는다.

### Named Rules

**The Flat Board Rule.** 고정된 면(헤더, 목록, 패널, 로그)에는 그림자가 없다. 구획은 1px 괘선이고, 표면 차이는 명도 한 단계다.

## Shapes

모서리는 작고 각지다. 버튼·입력·Diff 덩어리·그림 칸은 4px(`sm`), 메뉴·토스트·요약 창은 6px(`md`), 코드 조각은 3px(`xs`), 스켈레톤 막대는 2px(`hair`)다. 둥근 알약(12px `pill`)은 로그 거르기 칩에만 쓴다. 원형은 감시 표시 점(메인 창 7px, 요약 창 9px) 하나다. 행과 구역은 모서리 없이 괘선으로만 나뉜다. 선택은 테두리 상자가 아니라 위아래 두 줄의 1px 노랑 괘선이다.

## Components

### Headerbar
- **구성:** 이동 버튼 묶음(뒤로·앞으로·상위), 브레드크럼, 폴더 검색(220px), 오른쪽 실시간 지표(감시 경로, 최근 1분 이벤트, 반영 지연 P95), 패널 토글·메뉴.
- **지표:** 라벨은 Label/흐린 잉크, 숫자는 Figure/잉크. 숫자만 고정폭이다.
- **브레드크럼:** 상위 경로는 흐린 잉크, 현재 위치는 잉크 600/14px. 구분 셰브런 12px.

### Buttons
- **Shape:** 4px 모서리, 1px `line` 테두리, 투명 바탕, 높이 30px, 아이콘만 있으면 최소 폭 30px.
- **기본:** `ink-2` 글자. **Hover:** `hover` 바탕 + `ink` 글자. **비활성:** 불투명도 40%.
- **눌린 토글:** `hover` 바탕, 테두리 `#3a4a5e`(일회성 값).
- **Primary:** 바탕 없이 테두리만 `ink-2`로 밝히고 글자는 `ink`. 확정 버튼(되돌리기, 복원)에 쓴다.
- **포커스:** 2px `sel` 링, 안쪽으로 2px 들여 그린다(outline-offset −2px).

### Tabs
- 탭 줄은 `bg` 바탕, 탭 사이 `line-soft` 괘선. 기본 `dim` 글자, 선택 탭은 `ink` 글자와 아래 2px `sel` 밑줄. 닫기 버튼 12px.

### Sidebar
- 묶음 이름은 Group Label. 노드는 Body/`ink-2`, 호버 시 `hover` 바탕.
- **선택 노드:** `sel-wash` 바탕 + 위아래 1px `sel` 괘선. 옆 줄무늬는 없다.
- **트리 변동:** 폴더 오른쪽에 하위 변동을 고정폭 12px로(`▲ 7`, `→ 1`), 시세 색을 따른다.
- **감시 표시:** 7px 점. 감시 중은 `ink` 채운 점(흰 점), 감시 안 함은 `dim` 1px 테두리 빈 점. 빨강은 쓰지 않는다(빨강은 생성·증가 전용).

### File List (시그니처 화면)
- **열 머리글:** `bg` 바탕, Label/흐린 잉크, 아래 1px `line`. 정렬 중인 열은 `ink`와 10px 셰브런.
- **행:** 34px, 행 사이 1px `line-soft`. 이름 칸은 16px 종류 아이콘(흐린 잉크) + Row Name.
- **크기:** 파일은 `48.2 KB`처럼, 폴더는 흐린 `42개`.
- **변동:** 증가 `▲ +1.2 KB`(빨강 600), 감소 `▼ −41.0 KB`(파랑 600), 생성 `● 신규`(빨강), 삭제 `✕ 삭제`(파랑), 이동 `→ 1`과 보합 `—`(흐린 잉크). 음수 부호는 하이픈이 아니라 U+2212(−)다.
- **이벤트:** Event Code로 `▲ MODIFY`, `● CREATE`, `✕ DELETE`, `→ MOVE`, `◦ ATTRIB`. 색은 변동과 같은 규칙(MOVE·ATTRIB은 흐림).
- **수정 시각:** 오늘은 `HH:MM:SS`, 어제는 `어제 HH:MM`, 그 이전은 `MM-DD HH:MM`.
- **Hover:** `hover` 바탕. **선택:** `sel-wash` 바탕 + 위아래 1px `sel` 괘선. 옆 줄무늬 없음.
- **삭제되어 남은 행:** 글자 `dim`, 이름과 크기에 `down` 색 취소선. 약 30초 뒤 600ms 동안 사라지며 목록에서 빠진다(모션 감소 시 즉시). 선택된 행은 빼지 않는다.
- **오래된 목록(stale):** 감시 폴더가 사라지면(DELETE_SELF) 모든 행이 `dim`이 되고 잔상 바탕과 시세 색이 빠진다.

### Tick (시그니처 상호작용)
1. 외부 이벤트가 도착하면 그 행 전체 바탕이 `up-flash`/`down-flash`에서 `up-wash`/`down-wash`로 약 600ms 동안 내려앉는다. 곡선은 강한 ease-out(`cubic-bezier(.16,1,.3,1)`).
2. 변동 열은 부호 붙은 증감을 계속 보여 준다.
3. 같은 내용의 줄이 이벤트 로그 맨 위에 추가되고, 그 로그 줄도 `#243041`에서 투명으로 1.2s 동안 빠진다.
4. 잔상 바탕(wash)은 약 12초 뒤 걷히며, 걷힐 때 1.6s 동안 서서히 사라진다.
5. 보합 이벤트(MOVE, ATTRIB)는 플래시와 잔상이 없다.
6. 모션 감소 설정에서는 플래시와 로그 깜빡임 없이 잔상 바탕만 바로 칠해지고, 12초 뒤 바로 걷힌다.

### Bands (알림 띠)
- **반전 띠 (활성 오류·경고):** `ink` 바탕, `bg` 글자 500, 굵은 선(2px) 경고 아이콘, 오른쪽 끝에 `bg` 테두리 26px 버튼. 권한 오류("읽을 권한이 없습니다")와 감시 폴더 삭제(DELETE_SELF)에 쓴다. 목록 바로 위에 붙는다.
- **조용한 띠 (해결된 알림·안내):** `raise` 바탕, `ink` 글자, 아래 1px `line`. 다시 검사로 해결된 overflow 알림, 감시하지 않는 폴더 안내, 외부 삭제된 파일 설명에 쓴다. overflow 띠는 로그 위에 4px 8px 4px 12px, 12px 글자로 붙고 "확인"으로 닫는다.
- 두 띠 모두 빨강을 쓰지 않는다.

### Change-Detail Panel (변경 내용 패널)
- **머리:** 파일 이름(Title), 경로(mono 12px 흐림), 메타 줄(이벤트 기호·이름, `v6 → v7`, 직전 버전 시각), 동작 버튼(되돌리기, 전체 이력).
- **요약 줄:** 12px `ink-2`, 숫자는 mono 600(`+4 줄 추가`, `−2 줄 삭제`).
- **텍스트 파일:** 통합 줄 Diff. 열은 이전 줄 번호 34px, 현재 줄 번호 34px, 기호 16px, 본문. 추가 줄은 `add-wash` 바탕 + `add` 색 +, 삭제 줄은 `del-wash` 바탕 + `del` 색 −. 덩어리 머리(`@@ … @@`)는 `raise` 바탕 흐린 글자. 1px `line` 테두리, 4px 모서리.
- **이미지:** "나란히 / 겹쳐 밀기" 분할 버튼. 나란히는 두 칸 그리드(간격 8px), 이전 칸 테두리 `del` 50%, 현재 칸 `add` 50%, 아래 캡션(버전, 용량). 겹쳐 밀기는 현재 버전 위에 이전 버전을 슬라이더 위치에서 잘라 겹치고, 경계에 2px `ink` 손잡이선을 긋는다. 슬라이더 강조색은 `sel`. 바뀐 영역은 이미지 위에 어두운 잉크(#1d2733) 4px/3px 점선 상자로 표시한다. 노랑이 아니다(노랑은 선택·포커스 전용). 요약 줄에 크기·용량·바뀐 영역 수.
- **복합 문서(md, docx):** 텍스트 블록과 첨부 이미지별로 접을 수 있는 항목. 텍스트 블록은 단어 단위 삽입·삭제(`prose-ins`/`prose-del`, 앞에 + / − 기호), 표는 줄 Diff, 첨부 이미지는 교체면 나란히 비교, 추가면 한 칸에 "새 첨부". 항목 머리의 태그는 Diff 체계를 따른다: `+ 추가`는 `add` 초록, `± 교체`와 `수정`은 `ink-2`. 1366px 이하에서는 텍스트 블록을 접은 채로 시작한다.
- **바이너리:** 내용 비교 없이 종류, 마지막 크기, SHA-256, 보관 버전을 이름/값 두 열(값은 mono)로 보여 준다.
- **새 파일:** 이전 버전이 없다고 알리고 첫 버전 전체를 추가 줄로 보여 준다.
- **폴더:** 이 세션의 하위 변경을 로그와 같은 행 형식으로.
- **되돌리기 확인:** 패널 안에 1px `ink-2` 테두리 상자로 열리고, 지금 버전도 이력에 남는다고 알린다. 취소 버튼에 포커스가 먼저 간다.

### Event Log
- 도구줄: Heading "이벤트 로그", 칩(전체 / 생성·삭제 / 수정 / 이동 / 속성), 경로 거르기 검색(200px, 26px).
- **칩:** 24px 알약, 1px `line` 테두리, `dim` 글자. 눌린 칩은 `ink` 글자와 `ink-2` 테두리.
- **행:** Data Dense, 24px. 시각 `HH:MM:SS.mmm`(흐림, 118px), 이벤트(600, 시세 색, 110px), 경로, 값(오른쪽 정렬, 150px). 행을 누르면 목록에서 해당 파일을 선택한다. 최대 500줄.
- **세션 집계:** 생성·수정·삭제·이동·속성 건수를 Figure Large로, 기호를 앞에 붙인다. 생성·수정은 `up`, 삭제는 `down`, 이동·속성은 `dim`.

### Statusbar
- `raise` 바탕, 위 1px `line`, Label/흐린 잉크. 항목 수, 선택, 오른쪽에 이력 보관 용량, 디스크 여유, inotify watch 사용량(mono).

### Menu & Toast
- **컨텍스트 메뉴:** `raise` 바탕, 1px `line-float` 테두리, 6px 모서리, 떠 있는 층 그림자, 최소 220px. 항목 오른쪽에 단축키(mono 11px 흐림). 키보드 ↑↓, Esc로 닫고 행으로 포커스를 돌린다.
- **토스트:** 로그 위 44px에 가운데 정렬, 조용한 스타일(`raise` 바탕, `ink` 글자, 1px `line-float` 테두리, 떠 있는 층 그림자), "실행 취소" 버튼(26px), 5초 뒤 사라진다. 반전 스타일은 진행 중인 오류·경고 전용이라 토스트에 쓰지 않는다. 200ms ease-out으로 8px 올라오며 나타난다.

### Tray Icon (트레이 아이콘)
- 단색 symbolic 아이콘(18px, 1.5px 선). 상태는 색이 아니라 모양으로 구분한다.
  - 감시 중: 폴더 + 오른쪽 위 채운 점.
  - 일시 정지: 폴더 + 정지 막대 두 줄.
  - 대기열 넘침·오류: 폴더 + 느낌표. 사용자가 요약 창을 열어 확인하면 감시 중 아이콘으로 돌아간다(안내 띠는 창에 남는다).
- accessible name에 상태를 넣는다("FileScope: 감시 중").

### Tray Menu (네이티브 메뉴)
- AppIndicator가 띄우는 GtkMenu이며 시스템 Yaru 테마가 그린다. FileScope는 스타일하지 않고 항목 구성과 순서만 정한다. 시안의 메뉴 모양(`--sys-*` 값)은 근사치이며 토큰이 아니다.
- 순서(구분선으로 묶음):
  1. 상태 한 줄(`감시 중 · 경로 3개`, `일시 정지됨 · 경로 3개`, `대기열 넘침 · 다시 검사함`)과 감시 경로 3줄. 모두 비활성 항목이다.
  2. 최근 변경 3건: 기호 + 파일 이름 + 이벤트 라벨, 오른쪽에 시각. 색은 없다. 변경이 없으면 "아직 바뀐 파일이 없습니다".
  3. 요약 열기, 메인 창 열기.
  4. 감시 일시 정지 / 감시 재개(상태에 따라 바뀜), 감시 폴더 변경….
  5. FileScope 종료.

### Summary Window (트레이 요약 창)
메인 창을 닫은 채 감시 상황을 한눈에 보는 작은 창이다. 메인 창과 같은 토큰과 규칙을 그대로 쓴다.
- **창:** 폭 360px, `bg` 바탕, 1px `line-float` 테두리, 6px 모서리, 떠 있는 창 그림자. 시안은 X11 기준으로 화면 오른쪽 위(상단 표시줄 아래 16px, 오른쪽 24px)에 둔다.
- **헤더바:** 40px, `raise` 바탕, 아래 1px `line`. 제목 "FileScope 요약"(Heading), 오른쪽 28px 닫기 버튼(`dim`, 호버 시 `hover` 바탕). Esc로도 닫는다.
- **상태 줄:** "감시 중" / "일시 정지됨"을 Title(16px 600)로. 앞에 9px `ink` 흰 점(감시 중) 또는 18px 정지 아이콘(일시 정지). 오른쪽에 "최근 1분 N건"(라벨 12px `dim`, 숫자 mono 14px 600 `ink`). 일시 정지 중이면 0.
- **감시 경로:** 행마다 폴더 아이콘(`dim`), 경로(Data, mono 13px `ink`), 오른쪽 범위("하위 폴더 포함"/"이 폴더만", 12px `dim`). 위아래와 행 사이 1px `line-soft`.
- **세션 집계:** 4칸 그리드(생성, 수정, 삭제, 이동). 라벨 11px `dim`, 숫자 Figure(mono 17px 600)에 12px 기호를 앞에 붙인다. 색은 메인 창 집계와 같고 0건은 `dim`. 아래 1px `line`.
- **최근 변경:** 구역 제목 12px 600 `ink-2`, 오른쪽에 "눌러서 변경 내용 보기"(`dim`). 최대 5행, 행 높이 44px, 행 사이 1px `line-soft`. 칸은 64px | 남는 폭 | 자동 | 64px:
  - 기호 + 한국어 라벨(`▲ 수정`, `● 생성`, `✕ 삭제`, `→ 이동`), mono 12px 600, 시세 색.
  - 파일 이름 14px 500, 아래 경로 mono 11px `dim`.
  - 증감(mono 12px 600, 시세 색, 음수는 U+2212).
  - 시각 `HH:MM:SS`(mono 12px `dim`).
  - 틱 플래시와 12초 잔상은 메인 창과 같다(600ms, 걷힐 때 1.6s, 모션 감소 시 플래시 없음). 삭제 행은 이름에 `down` 취소선과 `dim` 글자.
  - 행을 누르면 메인 창을 열고 그 파일을 선택해 변경 내용 패널을 보여 준다.
  - 변경이 없으면 "아직 바뀐 파일이 없습니다"(14px `ink`)와 안내 문장(`ink-2`).
- **상태 띠:** 집계 아래에 들여 붙는 조용한 띠(`raise` 바탕, 1px `line` 테두리, 4px 모서리, 12px `ink-2` 글자, 아이콘은 `ink`). 일시 정지 안내(정지 아이콘), 대기열 넘침 후 재검사(경고 아이콘 + mono 시각). 둘 다 해결됐거나 사용자가 의도한 상태라 반전 띠를 쓰지 않는다.
- **일시 정지 상태:** 최근 변경과 집계를 불투명도 55%로 흐린다.
- **하단 버튼:** 32px 높이, 좌우 12px. [폴더 변경] [일시 정지 / 감시 재개] 왼쪽, [메인 창]은 primary(`ink-2` 테두리, `ink` 글자)로 오른쪽 끝.
- **토스트:** 창 오른쪽 아래, 조용한 스타일(`raise`, 1px `line-float`), 버튼 없이 3.2초.

### States
- **빈 폴더:** 목록 자리에 Title "이 폴더는 비어 있습니다", 감시 중이라 바로 나타난다는 안내와 예시 명령(`touch notes.md`, mono 코드 조각), 새 파일·새 폴더 버튼.
- **불러오는 중:** 스켈레톤 행 9줄. 10px 높이 2px 모서리 막대가 `raise` → `#212c39` → `raise`로 1.4s마다 흐른다(모션 감소 시 정지). 상태 표시줄에 "불러오는 중… 3,412개".
- **권한 오류:** 반전 띠 + 목록 자리 설명(root 없이 실행하므로 읽을 수 없음) + 상위 폴더 버튼. 로그와 집계는 비우고 지표는 0.
- **감시하지 않는 폴더:** 조용한 띠 + 경고 아이콘 + "감시 시작" 버튼. 목록은 정상 표시.
- **감시 폴더 삭제(DELETE_SELF):** 반전 띠 + "상위 폴더로 이동". 목록은 마지막 상태로 남되 stale(흐림) 처리, 트리 변동 숫자와 overflow 띠는 숨긴다.
- **Overflow:** 조용한 띠로 "대기열이 넘쳐 다시 검사해 맞췄다"는 해결 알림을 시각과 함께 남긴다.

## Do's and Don'ts

### Do:
- **Do** 빨강(`up`)은 생성·증가, 파랑(`down`)은 삭제·감소에만 쓰고, 항상 ▲ ▼ ● ✕ 기호를 함께 단다.
- **Do** 보합(이동, 속성, 변화 없음)은 `dim`과 → ◦ — 기호로 표시한다.
- **Do** 선택은 `sel-wash` 바탕과 위아래 1px `sel` 괘선으로, 포커스는 2px `sel` 링으로 표시한다.
- **Do** 활성 오류·경고는 반전 띠(`ink` 바탕, `bg` 글자) + 경고 아이콘으로, 해결된 알림은 조용한 띠(`raise`)로 알린다.
- **Do** Diff와 버전 비교에서는 git 관례(초록 +, 빨강 −)를 따르고 기호를 글자로 넣는다.
- **Do** 크기, 시각, 권한, 해시, 지표 숫자는 Ubuntu Mono 고정폭으로 쓴다.
- **Do** 음수에는 U+2212(−)를 쓴다.
- **Do** 모션 감소 설정에서는 플래시를 빼고 잔상 바탕만 남긴다.

### Don't:
- **Don't** 오류, 경고, 위험 작업(영구 삭제 등)에 빨강을 쓰지 않는다.
- **Don't** 반전 스타일(`ink` 바탕)을 토스트, 일시 정지 안내, 해결된 알림에 쓰지 않는다. 진행 중인 오류·경고 전용이다.
- **Don't** 감시 상태 표시에 시세 색을 쓰지 않는다. 감시 중은 `ink` 흰 점, 트레이 아이콘은 단색 모양으로 구분한다.
- **Don't** 노랑을 선택·포커스 밖에서 쓰지 않는다.
- **Don't** 선택 행에 옆 줄무늬(side stripe)를 넣지 않는다.
- **Don't** 카드나 상자로 묶지 않는다. 구획은 1px 괘선, 위계는 글자 크기·굵기다.
- **Don't** 고정 면(헤더, 목록, 패널, 로그)에 그림자를 넣지 않는다.
- **Don't** 이벤트 기호(▲ ▼ ● ✕ → ◦)를 아이콘처럼 키우거나 장식으로 쓰지 않는다. 글자와 같은 크기의 표기법이다.
- **Don't** 메인 창 변동에 git 초록을, Diff에 시세 파랑을 섞지 않는다.
- **Don't** 문장과 라벨을 고정폭으로 쓰지 않는다.

## GTK3 구현 메모

시안은 HTML/CSS지만 구현은 GTK3/C17이다. 아래는 리뷰에서 나온 필수 사항이다.

### 색 토큰 → GTK CSS

전역 스타일시트(`GtkCssProvider`, `GTK_STYLE_PROVIDER_PRIORITY_APPLICATION`)에서 frontmatter 토큰을 `@define-color`로 그대로 옮긴다. 이름은 `fs_` 접두사 + 토큰 이름(하이픈은 밑줄).

```css
@define-color fs_bg #10151c;        @define-color fs_bg_2 #141b24;
@define-color fs_raise #18202a;     @define-color fs_hover #1c2632;
@define-color fs_line #263241;      @define-color fs_line_soft #1b2430; @define-color fs_line_float #33435a;
@define-color fs_ink #e6e9ee;       @define-color fs_ink_2 #b9c1cc;   @define-color fs_dim #8a94a3;
@define-color fs_up #f0525a;        @define-color fs_up_wash alpha(@fs_up, 0.11);     @define-color fs_up_flash alpha(@fs_up, 0.42);
@define-color fs_down #4d9bff;      @define-color fs_down_wash alpha(@fs_down, 0.11); @define-color fs_down_flash alpha(@fs_down, 0.40);
@define-color fs_sel #f5c84b;       @define-color fs_sel_wash alpha(@fs_sel, 0.10);
@define-color fs_add #3fb950;       @define-color fs_add_wash alpha(@fs_add, 0.14);   @define-color fs_add_ink #b6f0bf;
@define-color fs_del #f85149;       @define-color fs_del_wash alpha(@fs_del, 0.14);   @define-color fs_del_ink #ffc2bd;
@define-color fs_media_well #0c1016; @define-color fs_scroll_thumb #2f3d4f;

*:focus { outline: 2px solid @fs_sel; outline-offset: -2px; }
treeview.view:selected { background-color: @fs_sel_wash; color: @fs_ink; }
```

C 코드(셀 데이터 함수, cairo, Pango)에서 쓰는 색은 같은 값을 `GdkRGBA` 상수로 한 곳(`fs_colors.h` 등)에 두고, CSS와 값이 어긋나지 않게 한다.

### 위젯 대응

| 구성 요소 | GTK3 위젯 |
|---|---|
| 헤더바 | `GtkHeaderBar`(커스텀 타이틀바), 버튼은 `GtkButton`, 검색은 `GtkSearchEntry` |
| 탭 | `GtkNotebook`(탭 머리만 테마), 선택 탭 밑줄은 `notebook > header tab:checked { box-shadow: inset 0 -2px @fs_sel; }` |
| 사이드바 즐겨찾기 / 폴더 트리 | `GtkListBox` / `GtkTreeView`(`GtkTreeStore`) |
| 파일 목록 | `GtkTreeView` + `GtkListStore`(아래 1번) |
| 알림 띠 | `GtkRevealer` 안의 `GtkBox`에 `.fs-band`, `.fs-band.quiet` 스타일 클래스(`GtkInfoBar`는 자체 테마가 강해 쓰지 않는다) |
| 변경 내용 패널 | `GtkScrolledWindow` + `GtkBox`, 복합 문서 항목은 `GtkExpander`, 이미지 비교 방식은 `.linked` `GtkBox` 안 `GtkToggleButton` 두 개, 겹쳐 밀기 슬라이더는 `GtkScale` |
| 이벤트 로그 | `GtkTreeView`(최대 500행), 칩은 `GtkToggleButton.fs-chip`(한 번에 하나만 눌림) |
| 세션 집계 | `GtkGrid` + `GtkLabel` |
| 상태 표시줄 | `GtkBox` + `GtkLabel` |
| 컨텍스트 메뉴 / 토스트 | `GtkMenu`(또는 `GtkPopover`) / `GtkOverlay` 위 `GtkRevealer` |
| 트레이 아이콘·메뉴 | `AppIndicator`(libayatana-appindicator3) + `GtkMenu`(시스템 테마) |
| 트레이 요약 창 | `GtkWindow` + `GtkHeaderBar`, 최근 변경은 `GtkListBox`, 집계는 `GtkGrid` |
| 되돌리기 확인 | 패널 안 인라인 `GtkBox`(모달 대화상자가 아님) |

### 1. 파일 목록과 틱

- 1만 파일 목록 NFR 때문에 파일 목록은 반드시 `GtkTreeView`다. `GtkListBox`는 행마다 위젯을 만들어 이 규모를 감당하지 못한다.
- 틱은 CSS 애니메이션이 아니다. 모델에 행별 "잔상 방향"(없음/증가/감소)과 "틱 시작 시각"(`g_get_monotonic_time()`) 열을 두고, 셀 데이터 함수가 경과 시간으로 `cell-background-rgba`를 `*_flash`에서 `*_wash`로 보간한다. 약 600ms 동안 `gtk_widget_add_tick_callback()`(프레임 클럭) 또는 `g_timeout_add()`로 다시 그리게 하고, 끝나면 콜백을 제거한다. 곡선은 ease-out(예: `1 - pow(2, -10t)`)으로 시안의 `cubic-bezier(.16,1,.3,1)`에 맞춘다.
- 잔상은 모델 열이다. 약 12초 뒤 `g_timeout_add_seconds()`로 그 열을 지운다. 시안처럼 걷힐 때 1.6s 페이드를 주려면 같은 보간 경로로 알파를 0까지 내린다.
- 모션 감소(`gtk-enable-animations`가 FALSE)이면 보간 없이 바로 `*_wash`를 칠하고, 12초 뒤 바로 지운다.
- 선택은 `GtkTreeView` 자체 선택(`GtkTreeSelection`)을 쓰고 `treeview.view:selected`로 노랑 바탕을 입힌다. 행마다 inset box-shadow를 줄 수 없으므로 위아래 1px 노랑 괘선은 `draw` 시그널에 `g_signal_connect_after`로 붙여 `gtk_tree_view_get_background_area()` 위치에 cairo로 긋는다. 선택된 행에서는 셀 데이터 함수가 `cell-background-set`을 끄고 테마 선택색이 보이게 한다.
- 삭제되어 남은 행의 취소선은 `GtkCellRendererText`의 `markup`에 `<span strikethrough="true" strikethrough_color="#4d9bff">`로 넣는다(렌더러 속성에는 취소선 색이 없다). 빠질 때의 600ms 페이드는 `foreground-rgba` 알파 보간으로 대신한다.
- stale 상태는 목록에 `.fs-stale` 클래스를 붙이고, 셀 데이터 함수가 잔상과 시세 색을 무시하고 `fs_dim`으로 그린다.

### 2. 변경 내용 패널

- 이미지 겹쳐 밀기는 `GtkDrawingArea`에서 두 버전을 `GdkPixbuf`로 그리고, 이전 버전은 슬라이더 위치에서 `cairo_rectangle` + `cairo_clip`으로 잘라 그린다. GTK3 CSS에는 `clip-path`가 없다. 손잡이선(2px `fs_ink`)과 바뀐 영역 점선 상자(`cairo_set_dash`)도 같은 `draw` 함수에서 긋는다.
- 문서의 단어 단위 Diff는 `GtkLabel`(또는 `GtkTextView`)에 Pango 마크업으로 넣는다. 삽입은 `background`(add-wash를 불투명하게 합성한 값 또는 `bgalpha`), `foreground="#b6f0bf"`, `underline="single" underline_color="#3fb950"`, 삭제는 `foreground="#ffc2bd"`, `strikethrough="true" strikethrough_color="#f85149"`. CSS `::before`가 없으므로 + / − 기호는 마크업 안에 글자로 직접 넣는다.
- 줄 Diff는 `GtkTextView`(편집 불가, monospace)와 `GtkTextTag`(`paragraph-background-rgba`로 줄 바탕)로 그린다. 줄 번호와 + / − 기호도 줄 텍스트 앞에 글자로 넣는다.

### 3. 트레이 (AppIndicator)와 요약 창

- 트레이는 libayatana-appindicator3로 만든다. `app_indicator_set_menu()`로 GtkMenu를 붙이고, 상태가 바뀌면 `app_indicator_set_icon_full()`로 상태별 symbolic 아이콘(감시 중 / 일시 정지 / 넘침·오류)을 바꾸며 두 번째 인자에 상태 설명을 넣는다.
- GNOME(Ubuntu AppIndicator 확장)은 아이콘 클릭 좌표를 앱에 주지 않고 메뉴만 띄운다. 그래서 클릭하면 바로 열리는 팝오버를 만들 수 없고, 요약 창은 메뉴의 "요약 열기" 항목으로 연다.
- 메뉴는 일반 `GtkMenu` + `GtkMenuItem`(정보 줄은 `gtk_widget_set_sensitive(FALSE)`, 묶음은 `GtkSeparatorMenuItem`)이며 CSS를 입히지 않는다. 최근 변경 3건은 라벨 텍스트만 바꿔 갱신한다.
- 요약 창은 일반 `GtkWindow` + `GtkHeaderBar`(제목 "FileScope 요약", 닫기 버튼)다. 창 제목에 앱 이름을 유지해 작업 전환기에서 구분되게 한다.
- 배치: X11에서는 `gtk_window_move()`로 화면(작업 영역) 오른쪽 위로 옮긴다. Wayland에서는 `gtk_window_move()`가 무시되므로 컴포지터 배치를 그대로 허용한다. layer-shell 같은 우회는 가정하지 않는다. 시안의 위치는 X11 기준이다.
- 요약 창의 최근 변경 5행은 `GtkListBox`로 충분하다(행 수가 고정). 틱은 파일 목록과 같은 보간 함수를 쓰고, 행마다 CSS를 바꾸는 대신 행 위젯의 `draw` 핸들러에서 보간한 색으로 cairo 배경을 칠한다.

### 4. 글꼴

- Ubuntu에서는 시스템 글꼴 스택을 쓴다: 본문 Ubuntu → Noto Sans CJK KR, 숫자 Ubuntu Mono. GTK CSS `font-family`로 지정하되 사용자 글꼴 크기 설정(글꼴 확대)을 깨지 않게 한다.
- 시안 스크린샷(`.impeccable/review/*.png`)은 Windows 대체 글꼴로 렌더링된 것이다. 글자 폭, 열 폭(특히 변동 112px, 수정 시각 122px), 기호 ▲ ▼ ● ✕ → ◦의 글꼴 지원과 높이는 실제 Ubuntu에서 다시 확인해야 한다.
- 12.5px 같은 소수 크기는 Pango에서 그대로 쓸 수 있지만 HiDPI 배율에서 다시 확인한다.
