# MarkDownIt

A native Windows markdown viewer. No Electron, no .NET runtime, no external
dependencies shipped.

**Status:** early development.

## Build

CMake + Visual Studio 2022 Build Tools, Windows 10 SDK.

```
cmake -B build -S .
cmake --build build --config Release
```

## Usage

```
MarkDownIt.exe path\to\file.md
```

## License

MIT. See [LICENSE](LICENSE).
