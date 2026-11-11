#pragma once
#include <string>

namespace perlin::ui {
    // swaps plain .btn boxes (rim/face/shade/edge) for java button sprites (normal/highlighted/disabled,
    // nine-sliced: packs restyle) and slider knob, for every menu document
    std::string WithJavaWidgets(const std::string& rml);
}
