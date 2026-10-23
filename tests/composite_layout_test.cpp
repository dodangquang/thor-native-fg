#include "composite_layout.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
int main() {
    using thor::CompositeLayout;
    const CompositeLayout full{1920,1080,0,0,1920,1080};
    auto a=thor::sourceComposite(full,1280,720);
    assert(a && a->width==1280 && a->height==720 && a->right==1280 && a->bottom==720);
    a=thor::sourceComposite(full,1024,576);
    assert(a && a->width==1024 && a->height==576);
    a=thor::sourceComposite({1920,1080,240,0,1680,1080},640,480);
    assert(a && a->width==853 && a->height==480 && a->left==106 && a->right==746);
    a=thor::sourceComposite({1080,1920,0,240,1080,1680},480,640);
    assert(a && a->width==480 && a->height==853 && a->top==106 && a->bottom==746);
    assert(!thor::sourceComposite(full,1920,1080));
    assert(!thor::sourceComposite(full,3840,2160));
    assert(!thor::sourceComposite({1280,720,0,90,1280,630},1280,540));
    assert(!thor::sourceComposite({1920,1080,0,0,1921,1080},1280,720));
    assert(!thor::sourceComposite({1920,1080,100,0,100,1080},1280,720));
    assert(!thor::sourceComposite(full,0,720));
    assert(!thor::sourceComposite(full,8192,720));
    assert(!thor::sourceComposite({UINT32_MAX,1080,0,0,1920,1080},1280,720));
    // Check independent geometry invariants over portrait/landscape, asymmetric
    // bars and non-integral scales; no allocations or coordinate overflow.
    unsigned cases=0;
    for(uint32_t width: {320u,853u,1280u,1920u,4096u})
    for(uint32_t height: {240u,479u,720u,1080u,4096u})
    for(uint32_t source: {32u,127u,640u,1024u,2048u}) {
        auto p=thor::sourceComposite({width,height,5,7,width-11,height-13},source,source/2);
        if(p){assert(p->width<width && p->height<height && p->right<=p->width && p->bottom<=p->height);
            assert(p->right-p->left<=source+1 && p->bottom-p->top<=source/2+1);}
        ++cases;
    }
    std::printf("12 explicit layout cases and %u geometry invariant cases PASS\n",cases);
}
