# Mermaid Diagram Test Document

## Flowchart Top-Down

```mermaid
flowchart TD
    A[Start] --> B{Decision}
    B -->|Yes| C[Action 1]
    B -->|No| D[Action 2]
    C --> E((End))
    D --> E
```

## Flowchart Left-Right

```mermaid
graph LR
    A --> B --> C
    A --> D
    C --> E[Result]
```

## Sequence Diagram

```mermaid
sequenceDiagram
    participant User
    participant API
    participant Database
    User->>API: GET /data
    API->>Database: SELECT * FROM table
    Database-->>API: rows
    API-->>User: JSON response
```

## Dotted and Thick Edges

```mermaid
graph TD
    A -.->|optional| B
    A ==>|important| C
```

## Circle and Diamond Shapes

```mermaid
graph TD
    Start((Start)) --> Check{Valid?}
    Check -->|Yes| Process[Process data]
    Check -->|No| Error([Error])
    Process --> End((End))
```

## Invalid Diagram (Falls Back to Code)

```mermaid
gantt
    title A gantt chart
    dateFormat  YYYY-MM-DD
    section Section
    Task 1 :a1, 2024-01-01, 30d
```
