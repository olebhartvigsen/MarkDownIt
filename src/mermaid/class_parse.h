// Class diagram: parse `classDiagram` sources.
//
// Grammar (mermaid-compatible subset):
//   classDiagram
//   direction LR|TB|BT|RL
//   class Name
//   class Name { member* method* annotation* }
//   member: [+|-|#|~]text[$*]?        ($ static, * abstract suffix stripped)
//   method: [+|-|#|~]name(args) [ret][$*]?
//   annotation: <<Interface>> etc.
//   A ["card"] <arrow> ["card"] B [: label]
//     arrows: <|-- --|> <|.. ..|> *-- --* o-- --o ..> <.. --> <-- .. --
//   %% comments skipped
//
// Member/method text mirrors mermaid's member parse: text keeps the
// visibility char, trailing $ (static) or * (abstract) classifier suffix is
// stripped, and `name(args) ret` methods gain the ` : ret` separator.
#ifndef MERMAID_CLASS_PARSE_H
#define MERMAID_CLASS_PARSE_H

#include <string>
#include <vector>

namespace mermaid {

enum class RelType {
    None,          // plain link (-- / ..)
    Extension,     // <|-- / --|> hollow triangle
    Composition,   // *-- filled diamond
    Aggregation,   // o-- hollow diamond
    Dependency,    // ..> / --> open arrow
};

enum class RelEnd { None, Start, End };

struct ClassRelation {
    std::string from;
    std::string to;
    RelType type = RelType::None;      // marker shape
    RelEnd marker_end = RelEnd::End;   // which side carries the marker
    bool dashed = false;
    std::string label;        // `: label` text
    std::string start_label;  // leading quoted cardinality
    std::string end_label;    // trailing quoted cardinality
};

struct ClassItem {
    std::string text;      // rendered text (+String name, +eat(), <<Interface>>)
    bool is_method = false;
    bool is_annotation = false;
};

struct ClassBox {
    std::string id;
    std::vector<std::string> annotations;  // inner text (interface), no << >>
    std::vector<std::string> members;
    std::vector<std::string> methods;
};

struct ClassDiagram {
    std::vector<ClassBox> classes;
    std::vector<ClassRelation> relations;
    std::string direction = "TB";
    std::string error;
};

// Parses `classDiagram ...` source (without code fences). On error, error is
// set and the returned diagram is empty.
ClassDiagram ParseClassDiagram(std::string_view src);

// Parse one member line into (text, is_method). Exposed for tests.
bool ClassParseMember(const std::string& raw, std::string& text,
                      bool& is_method);

}  // namespace mermaid

#endif  // MERMAID_CLASS_PARSE_H
