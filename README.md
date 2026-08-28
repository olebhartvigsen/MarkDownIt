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

## Mermaid Diagram Support

MarkDownIt renders fenced code blocks with the `mermaid` language tag as
diagrams instead of raw text.

### Supported

- **flowchart** and **graph** with directions TD, TB, LR, RL, BT
- **sequenceDiagram** with participants and messages

Node shapes: rectangle `[]`, rounded `()`, diamond `{}`, circle `(())`,
stadium `([])`.

Edge styles: solid `-->`, dotted `-.->`, thick `==>`, with labels
`-->|text|`.

### Not Supported

Unsupported diagram types fall back to plain code block rendering:

- gantt, class, state, ER, pie, journey, C4, mindmap
- subgraph blocks
- style/class directives (e.g. `style A fill:#f9f`)
- clickable links in diagrams
- Diagram themes

### Edit Mode

In edit mode, the diagram source is shown as plain text when the caret
is inside the block, so you can edit the diagram. The rendered diagram
reappears when the caret moves outside the block.

## License

MIT. See [LICENSE](LICENSE).
