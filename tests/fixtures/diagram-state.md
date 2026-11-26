# State diagram

```mermaid
stateDiagram-v2
    [*] --> Ready
    Ready --> Drawing : paint
    Drawing --> Ready : done
    Drawing --> [*] : close
```
