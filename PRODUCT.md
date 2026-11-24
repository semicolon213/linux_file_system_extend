# Product

<!-- impeccable:product-schema 1 -->

## Platform

linux-desktop

<!-- 스키마 값(web/ios/android/adaptive)에 해당하지 않는 네이티브 Linux 데스크톱 앱이다. GNOME/GTK3 데스크톱 관례를 따른다. -->

## Stack

C17 + GTK3, GLib/GIO, inotify, SQLite, CMake/Ninja. 대상 OS는 Ubuntu 22.04/24.04 LTS.
툴킷은 수업 지정 없음, 2026-11-24 사용자 확정. 현재 저장소의 C++17/Qt 6 스캐폴드(`CMakeLists.txt`, `src/`)와 README는 이 결정에 맞춰 교체 예정이다.

## Users

주 사용자는 개발자다. 터미널이나 IDE에서 프로젝트 파일을 바꾸는 동안, 그 변경을 새로고침 없이 탐색기에서 확인하고 필요하면 이전 버전으로 되돌린다.
시스템 관리자(경로 감시, 권한·무결성 확인)와 일반 사용자(일상 탐색, 안전한 파일 작업)는 보조 대상이다.

## Product Purpose

FileScope는 외부 프로그램이 일으킨 파일 변경을 실시간으로 보여 주고, 파일 작업을 안전하게 수행하게 하는 네이티브 파일 탐색기다.
핵심 시나리오: 프로젝트 열기 → 감시 등록 → 외부에서 파일 생성/수정/이동/삭제 → 목록과 이벤트 로그 갱신 → 보관된 버전 비교 및 복원.
성공 기준: P0 요구사항과 품질 게이트 통과, 실제 Ubuntu에서 GUI 데모(수업 과제 평가), 성능 측정 환경과 결과 기록.

## Positioning

일반 파일 탐색은 Nautilus 같은 기본 탐색기로 충분하다. FileScope의 차별점은 inotify 기반 실시간 관측(이벤트 정규화, cookie로 이동 연결, overflow 후 상태 재검증)과 SQLite 이력에 따른 버전 비교·복원을 탐색 화면 안에 묶은 것이다.

## Operating Context

- 개발자가 터미널, IDE와 FileScope를 나란히 띄워 놓고 쓴다.
- 평가는 실제 Ubuntu 머신에서 하는 라이브 GUI 데모로 이뤄진다.
- 개발 과정은 Claude(Coordinator·QA)와 Codex(구현)가 나눠 맡는 하네스(`harness/`) 흐름을 따른다.

## Capabilities and Constraints

- 기능 범위와 ID: `docs/product/FEATURE_SPEC.md` (P0 핵심 → P3 실험). 모두 계획 상태이며 구현된 기능은 아직 없다.
- 릴리스 순서: R0 계약·빌드·하네스 → R1 탐색/CRUD/감시/작업 큐 → R2 이력/검색/미리보기/휴지통 → R3 이후 (`docs/product/ROADMAP.md`).
- 비기능 목표(측정값 아님): 시작 2초, 이벤트 UI 반영 P95 100ms, 1만 파일 목록 1초, root 없이 실행, 오프라인 핵심 기능 (`docs/product/NFR.md`).
- 오래 걸리는 I/O는 worker에서 처리하고, GTK 위젯은 메인 스레드에서만 다룬다.
- 보장하지 않는 것: 전체 시스템 감시, 완전한 복구, 악성코드 차단.
- UI 테마 우선순위: 다크(UI-001)가 P0, 라이트·시스템 테마는 P1.
- 변경 내용 표시(2026-11-24 사용자 요구): 파일이 바뀌면 무엇이 어떻게 바뀌었는지 메인 창에서 바로 보여 준다. 텍스트는 줄 단위 Diff, 이미지는 전후 비교, 문서(md, docx 등)는 내부 텍스트 블록과 첨부 이미지 단위 변경까지. 직전 버전 보관(HIS-001/005)이 전제다.
- 트레이 요약(2026-11-24 사용자 요구): 창을 닫아도 트레이 아이콘으로 감시를 이어 간다. Ubuntu 기본 트레이(AppIndicator)는 클릭 시 네이티브 메뉴만 띄우므로, 메뉴에 상태·감시 경로·최근 변경 3건·일시 정지를 바로 보이고 "요약 열기"로 별도 요약 창을 연다(사용자 확정). 이벤트 색은 메인 창 규칙을 따른다.
- Diff 색은 git 관례(초록=추가, 빨강=삭제)를 따르고, 목록의 변동 표시는 빨강=증가, 파랑=감소다. 둘 다 기호(+/−, ▲/▼)를 함께 단다.

## Brand Commitments

- 제품 이름은 FileScope다. README와 창 제목의 "Linux File Explorer"는 임시 이름이다.
- 시각 디자인은 사용자가 Impeccable로 직접 만든다. 에이전트는 색상, 타이포그래피, 간격, 레이아웃, 아이콘, 애니메이션, 테마를 임의로 정하지 않는다 (`docs/design/DESIGN_OWNERSHIP.md`).

## Evidence on Hand

- 제품·아키텍처·테스트 문서: `docs/`, 작업 하네스: `harness/`.
- 아직 없는 것: 실제 사용자, 성능 측정 결과, 스크린샷, 데모 영상. NFR 수치는 목표일 뿐이니 결과처럼 쓰지 않는다.

## Product Principles

1. 관측이 먼저다. 외부 변경이 화면에 빠르고 정확하게 드러나야 하며, 이벤트 유실(overflow)도 숨기지 않고 알린다.
2. 파일 작업은 되돌릴 수 있어야 한다. 휴지통, 이력, 위험 작업 확인을 영구 삭제보다 앞에 둔다.
3. 긴 작업이 UI를 멈추게 하지 않는다. 진행률과 취소를 항상 제공한다.
4. 데모에서 증명할 수 있는 P0를 완성하는 것이 P1 이상의 기능 수보다 중요하다.

## Accessibility & Inclusion

모든 핵심 작업 키보드 접근(NFR-019), 보이는 포커스, accessible name, 색상 외 수단으로 상태 구분, HiDPI와 글꼴 확대, 모션 감소를 지원한다. GNOME Orca 화면 읽기 프로그램과 호환되어야 하며, 개발용 Orca coordinator와는 구분한다 (`docs/design/ACCESSIBILITY.md`).
