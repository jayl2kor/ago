# /status — 프로젝트 현황 개요

Agl 프로젝트의 현재 상태를 빠르게 파악합니다.

## 사용법
`/status` — 전체 프로젝트 현황 표시

## 절차

1. **소스 파일**: `src/` 디렉토리의 모든 `.c`/`.h` 파일 목록과 각 파일의 역할 (파일 첫 주석 또는 파일명 기반 추론)
2. **테스트 상태**: `tests/` 디렉토리의 테스트 파일 목록. `make test` 가능하면 실행하여 통과/실패 현황.
3. **예제**: `examples/` 디렉토리의 `.agl` 파일 목록.
4. **Git 상태**: 현재 브랜치, 커밋되지 않은 변경사항, 최근 커밋 3개.
5. **구현 상태**: 메모리의 `implementation-status.md`를 읽어서 현재 Phase와 모듈별 상태 표시.
6. **다음 작업 제안**: 로드맵 기준으로 다음에 할 작업 제안.

## 출력 형식
```
═══ Agl Project Status ═══

📁 Source:     N files (lexer.c, parser.c, ...)
🧪 Tests:     M tests (K passing, J failing)
📝 Examples:  L files
🔀 Git:       branch main, clean/N uncommitted changes
🚀 Phase:     X — [phase name]
👉 Next:      [suggested next task]
```

## 주의사항
- 빠른 개요 목적이므로 상세 분석은 하지 않음
- 파일이나 디렉토리가 아직 없으면 "아직 생성되지 않음"으로 표시
