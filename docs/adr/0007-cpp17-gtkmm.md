# ADR-0007 구현 언어 C++17 + gtkmm-3.0

상태: 승인 (2026-11-24, 사용자). ADR-0001의 언어 결정(C17)을 바꾼다. 아키텍처(modular monolith, event-driven, 계층 의존)와 GTK3 툴킷은 그대로다.

## 맥락
수업 요구사항으로 구현 언어가 C++여야 한다. GTK3 화면 설계(루트 DESIGN.md "GTK3 구현 메모")와 R1 계약(docs/DESIGN.md의 수치·동작)은 이미 승인되어 있고, src/core·src/job·src/nav와 빌드 하네스는 C17로 구현되어 VERIFY 상태다.

## 결정
- 언어: C++17 (GCC/Clang, `-std=c++17`, 확장 없음). C 소스는 두지 않는다.
- GUI: GTK3를 C++ 바인딩 gtkmm-3.0으로 쓴다(glibmm-2.4, giomm-2.4, sigc++-2.0을 함께 쓴다). 루트 DESIGN.md의 GTK3 위젯 대응은 gtkmm 클래스로 그대로 옮긴다(GtkTreeView → `Gtk::TreeView`, GtkDrawingArea + cairo → `Gtk::DrawingArea` + `Cairo::Context`). gtkmm에 래퍼가 없는 기능(예: libayatana-appindicator3, 일부 GTK 함수)은 C API를 C++에서 직접 부른다(`gobj()`).
- 바뀌는 것은 언어 바인딩뿐이다. 이벤트 필드, size_delta/root_total, MOVE 시간창, 배치, 큐 크기, 작업 상태, 오류 코드, 보호 경로, 탐색 동작, 1분봉 규칙 같은 계약 수치와 동작은 바꾸지 않는다.
- C++ 관용 규칙(오류 모델, 콜백, 스레드 전달, 경로 타입, 이름 공간, 파일 배치, 테스트)은 ADR-0008.

## 대안
- C17 유지: 수업 요구사항을 어긴다.
- Qt 6로 교체: 승인된 GTK3 디자인과 구현 메모를 버려야 한다.
- GTK4/gtkmm-4.0: Ubuntu 22.04 기본 저장소와 승인된 GTK3 디자인(GtkTreeView 1만 행 등)에 맞지 않는다.

## 결과
- 기존 C 구현은 C++로 다시 구현한다. R0-001(빌드 하네스), R1-T01(core), R1-T02(job), R1-T05(nav)는 VERIFY에서 내려와 R0-002와 각 "C++ 포팅" 작업으로 다시 한다. 수용 조건은 같다.
- R1-T03(fs), R1-T04(mon), R1-T07(dash)의 C 시도는 중단하고 버린다. 계약은 C++ 판(docs/DESIGN.md)으로 새로 시작한다.
- 새 개발 패키지 `libgtkmm-3.0-dev`가 필요하다(사용자 설치).
