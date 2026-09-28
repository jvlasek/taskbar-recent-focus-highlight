#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdint>
#include <iostream>
#include <vector>
enum class GlowStyle { Full, BottomBar, Frame, LeftBar };
// PRODUCTION_HELPERS
int main() {
    unsigned cases=0;
    // Equal native values, reversed values, negative/custom values and limits.
    for (int pattern=0;pattern<5;++pattern) {
        std::vector<int> permutation{0,1,2,3,4,5};
        do {
            for (auto style:{GlowStyle::Full,GlowStyle::BottomBar,GlowStyle::Frame,GlowStyle::LeftBar}) {
                std::vector<IconZOrderInput> input;
                for(auto id:permutation) {
                    int z=pattern==0 ? 0 : pattern==1 ? id : pattern==2 ? -id : pattern==3 ? (id%2 ? INT_MAX : INT_MIN) : (id%3-1)*7;
                    // 0 background; 1 glyph; 2 badge; 3 running pill; 4 Styler; 5 progress.
                    bool above=id==2 || id==3 || id==5 || (style==GlowStyle::LeftBar && id==1);
                    input.push_back({z,above,id==3});
                }
                auto plan=PlanIconZOrder(input,style);
                assert(plan.nativeZ.size()==input.size());
                for(size_t i=0;i<input.size();++i) {
                    assert(plan.nativeZ[i]!=plan.hostZ);
                    for(size_t j=i+1;j<input.size();++j) {
                        bool originallyAbove=input[i].z>input[j].z;
                        assert((plan.nativeZ[i]>plan.nativeZ[j])==originallyAbove);
                    }
                    if(style==GlowStyle::Full)assert(plan.hostZ<plan.nativeZ[i]);
                    if((style==GlowStyle::Frame || style==GlowStyle::LeftBar) && input[i].aboveHost)
                        assert(plan.hostZ<plan.nativeZ[i]);
                    if(style==GlowStyle::BottomBar && input[i].runningIndicator) {
                        assert(plan.hostZ==plan.nativeZ[i]+1);
                    }
                }
                auto repeated=PlanIconZOrder(input,style);
                assert(repeated.hostZ==plan.hostZ && repeated.nativeZ==plan.nativeZ);
                ++cases;
            }
        } while(std::next_permutation(permutation.begin(),permutation.end()));
    }
    for(auto style:{GlowStyle::Full,GlowStyle::BottomBar,GlowStyle::Frame,GlowStyle::LeftBar}) {
        assert(PlanIconZOrder({},style).nativeZ.empty());
        std::vector<IconZOrderInput> base{{0,false,false},{0,false,true},{0,style==GlowStyle::LeftBar,false},{0,false,false}};
        for(int cycle=0;cycle<10;++cycle) {
            auto before=PlanIconZOrder(base,style);
            auto badge=base;badge.insert(badge.begin()+3,{0,true,false}); // Windows creates badge ahead of DefaultIcon.
            auto withBadge=PlanIconZOrder(badge,style);
            assert(withBadge.nativeZ[3]>withBadge.nativeZ[2]);
            assert(PlanIconZOrder(base,style).nativeZ==before.nativeZ);
        }
    }
    std::cout<<"PASS: "<<cases<<" ZIndex plans, native draw-order preservation, int limits, and repeated badge cycles\n";
}
