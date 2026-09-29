#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <iostream>
// PRODUCTION_HELPERS
int main() {
    for (double bad : {std::numeric_limits<double>::quiet_NaN(),
                       std::numeric_limits<double>::infinity(),
                       -std::numeric_limits<double>::infinity(), 1e100}) {
        assert(!SafeGlowBounds(bad,0,40,40));
        assert(!SafeGlowBounds(0,bad,40,40));
        assert(!SafeGlowBounds(0,0,bad,40));
        assert(!SafeGlowBounds(0,0,40,bad));
        assert(InsetGlowRadius(bad,1,38,38)==0);
        assert(InsetGlowRadius(13,bad,38,38)==0);
    }
    for (double bad : {0.0,-1.0,0.5}) {
        assert(!SafeGlowBounds(0,0,bad,40));
        assert(InsetGlowRadius(13,1,bad,40)==0);
    }
    assert(SafeGlowBounds(-2,-2,44,44));
    assert(InsetGlowRadius(13,1,24,24)==12); // circular native 26px
    assert(InsetGlowRadius(0,1,38,38)==0); // square theme
    assert(InsetGlowRadius(4,1,38,38)==3);
    assert(InsetGlowRadius(999,1,38,38)==19); // oversized native radius
    assert(InsetGlowRadius(1,-2,12,6)==3); // expanded thin pill
    assert(InsetGlowRadius(-1,1,38,38)==0);
    std::cout << "PASS: contour bounds and radius safety\n";
}
