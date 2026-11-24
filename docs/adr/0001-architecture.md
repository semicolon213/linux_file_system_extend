# ADR-0001 아키텍처
단일 프로세스 modular monolith, event-driven 관측, 계층별 의존성. MSA는 배포·IPC 비용 때문에 제외. GTK3는 기존 학습 환경 유지.
상태: 승인 (2026-11-24, 사용자). 툴킷은 GTK3 / C17로 확정. 저장소의 C++17 / Qt 6 스캐폴드(`CMakeLists.txt`, `src/`)는 R0에서 교체한다.
