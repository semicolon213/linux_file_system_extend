# 메모리 계약
할당/해제 쌍 일치: malloc/free, g_malloc/g_free, GObject ref/unref. public API에 transfer full/none 명시. 큐는 참조 소유권을 인수하고 실패 시 호출자 유지. 오류 경로에서 fd, DIR*, statement, source 해제. 크기 계산 overflow 검사. ASan/UBSan 및 Valgrind 검증.
