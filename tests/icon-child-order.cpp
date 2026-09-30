#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>
enum class GlowStyle { Full, Frame, LeftBar };
// PRODUCTION_HELPERS
int main() {
    unsigned cases=0;
    // All native orders, host positions, styles, and thin-pill/Styler variants.
    for (bool plate : {false,true}) {
        std::vector<int> order{0,1,2,3,4,5};
        do {
            for (auto style : {GlowStyle::Full,GlowStyle::Frame,GlowStyle::LeftBar}) {
                std::vector<GlowOrderChild> input;
                for (int id : order) {
                    // background, glyph, badge, running indicator, progress, other
                    bool above=id==2 || id==4 || (id==3 && !plate) || (id==1 && (style==GlowStyle::LeftBar || style==GlowStyle::Full));
                    input.push_back({above});
                }
                auto target=GlowHostInsertionIndex(input);
                assert(target<=order.size());
                {
                    for(size_t i=0;i<input.size();++i) if(input[i].aboveHost) assert(target<=i);
                    assert(target==input.size() || input[target].aboveHost);
                }
                for(size_t old=0;old<=order.size();++old) {
                    auto list=order;
                    list.insert(list.begin()+old,99);
                    if(old!=target) {
                        list.erase(list.begin()+old);
                        list.insert(list.begin()+target,99);
                    }
                    assert(list[target]==99);
                    list.erase(list.begin()+target);
                    assert(list==order); // native identity/order survives placement and clear
                    assert(GlowHostInsertionIndex(input)==target); // no repeat move
                    ++cases;
                }
            }
        } while(std::next_permutation(order.begin(),order.end()));
    }
    for(auto style:{GlowStyle::Full,GlowStyle::Frame,GlowStyle::LeftBar}) {
        assert(GlowHostInsertionIndex({})==0);
        std::vector<GlowOrderChild> noAnchors(3,{false});
        assert(GlowHostInsertionIndex(noAnchors)==3u);
    }
    std::cout << "PASS: " << cases << " host placements; native order preserved, idempotence and missing anchors\n";
}
