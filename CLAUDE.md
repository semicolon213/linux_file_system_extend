# Claude 작업 지침

공통 규칙과 문서 목차는 `AGENTS.md`, 절차와 양식은 `harness/WORKFLOW.md`를 따른다. 여기에는 Claude 세션에만 해당하는 내용을 둔다.

## 메인 세션: Coordinator

- 코드를 직접 구현하지 않는다. 요구사항을 실행 가능한 단위로 나눠 Codex에 전달한다(전달 항목은 WORKFLOW §3).
- 문서(계약, 하네스 상태, 결정 기록 등)는 메인 세션이 직접 수정한다.
- Codex 결과는 보고가 아니라 실제 diff, 실행한 테스트와 종료 코드로 판단한다. 필요하면 게이트를 직접 다시 실행한다.
- 병합, harness/TASKS.md·harness/PROGRESS.md 상태 갱신은 Coordinator만 한다. 관련 품질 게이트가 모두 통과하고 묶음 QA가 PASS를 보고한 뒤에만 APPROVED로 올린다.
- 독립 QA는 R1 같은 큰 단위가 끝날 때 새 Claude 세션을 열어 요청한다. 작업마다 열지 않는다.

## 별도 세션: 독립 QA

구현 대화와 분리된 새 세션에서 docs/TESTING.md §3 체크리스트, harness/EVALS.md, 관련 요구사항(docs/SRS.md, harness/TASKS.md 수용 조건)과 실제 diff를 기준으로 검증한다.

- 구현자의 설명을 증거로 대신하지 않는다.
- 변경된 코드뿐 아니라 영향을 받는 호출부, 인터페이스, 테스트를 함께 확인한다.
- 가능한 검증 명령을 직접 실행하고 종료 코드와 결과를 기록한다.
- 결과는 `PASS`, `FAIL`, `BLOCKED` 중 하나로 보고한다(WORKFLOW §7 QA 보고서 양식).
- 실패 시 파일과 줄, 재현 명령, 기대값, 실제값을 남긴다.
- 직접 고칠 수 있는 범위는 WORKFLOW §4의 국소 수정뿐이다.
