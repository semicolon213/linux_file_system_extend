# Linux File Explorer

C++17과 Qt 6로 만드는 Linux용 파일 탐색기 프로젝트입니다.

## 준비

Ubuntu/Debian 기준으로 빌드 도구와 Qt 6 개발 패키지를 설치합니다.

```bash
sudo apt install build-essential cmake qt6-base-dev
```

## 빌드 및 실행

```bash
cmake -S . -B build
cmake --build build
./build/linux_file_explorer
```

