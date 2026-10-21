#pragma once

#include "mesher/mesh.hpp"

namespace mesher {

// V18-M07: deliberately named, *source-independent* OFF-mode desktop
// GPU acceptance geometry. NOT a V17 Merge source/certificate.
// Two separate near sheets and one remote harmless editing target.
// Stable coordinate equality never welds these three independent faces.
inline Mesh desktopOffModeNearSheetsFixture() {
    Mesh m;
    m.vertices={
        {0,0,0},{2,0,0},{0,2,0},
        {0,0,.55},{0,2,.55},{2,0,.55},
        {3.5,0,0},{5.5,0,0},{3.5,2,0}
    };
    m.triangles={{0,1,2},{3,4,5},{6,7,8}};
    return m;
}

} // namespace mesher
