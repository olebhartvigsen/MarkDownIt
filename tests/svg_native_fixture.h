#pragma once

// Native paint probes use the same SVG on Mac and Windows. Samples stay away
// from antialiased edges; rounded corners and open paths include clear probes.
namespace svg_native_fixture {
constexpr int kWidth = 240;
constexpr int kHeight = 140;
constexpr char kSvg[] = R"svg(<svg width="240" height="140">
<rect x="10" y="10" width="40" height="30" rx="10" fill="#40a0e0" stroke="#000" stroke-width="4"/>
<ellipse cx="90" cy="25" rx="20" ry="12" fill="#40a0e0" stroke="#000" stroke-width="4"/>
<line x1="130" y1="15" x2="170" y2="15" stroke="#000" stroke-width="4"/>
<rect x="190" y="10" width="40" height="30" fill="none" stroke="#000" stroke-width="4"/>
<path d="M10,60 L50,60 L30,90 Z" fill="#40a0e0" stroke="#000" stroke-width="4"/>
<path d="M70,60 L70,95 L110,95" fill="none" stroke="#000" stroke-width="4"/>
<path d="M130,60 L130,95 L170,95 Z" fill="none" stroke="#000" stroke-width="4"/>
<polygon points="190,60 230,60 230,100 190,100 190,60 230,60 230,100 190,100" fill="#40a0e0"/>
</svg>)svg";

struct Probe {
    const char* name;
    int x;
    int y;
    int r;
    int g;
    int b;
};
constexpr Probe kProbes[] = {
    {"rounded fill", 30, 25, 64, 160, 224},
    {"rounded stroke", 10, 25, 0, 0, 0},
    {"rounded corner", 10, 10, 255, 255, 255},
    {"ellipse fill", 90, 25, 64, 160, 224},
    {"ellipse stroke", 71, 25, 0, 0, 0},
    {"ellipse corner", 70, 13, 255, 255, 255},
    {"line stroke", 150, 15, 0, 0, 0},
    {"line exterior", 150, 23, 255, 255, 255},
    {"rect stroke", 191, 25, 0, 0, 0},
    {"rect interior", 210, 25, 255, 255, 255},
    {"polygon fill", 30, 70, 64, 160, 224},
    {"polygon stroke", 30, 60, 0, 0, 0},
    {"polygon exterior", 15, 85, 255, 255, 255},
    {"open path vertical", 70, 80, 0, 0, 0},
    {"open path horizontal", 90, 95, 0, 0, 0},
    {"open path gap", 90, 77, 255, 255, 255},
    {"closed path diagonal", 150, 77, 0, 0, 0},
    {"nonzero winding fill", 210, 80, 64, 160, 224},
};
}  // namespace svg_native_fixture
