// Standalone PPM preview of the chronos vector font's octant rasterizer.
// Renders the SAME mask + coverage-AA logic font.hpp uses, but to an RGB
// image (each terminal cell = 2x4 sub-pixels drawn as a 2x4 block of image
// pixels scaled up), so we can eyeball glyph quality without a TTY.
//
//   c++ -std=c++20 -O2 tools/fontpreview.cpp -o build/fontpreview
//   ./build/fontpreview > /tmp/clock.ppm

#include "../src/font.hpp"
#include <cstdio>
#include <vector>

using chronos::gfx::Col;

// A fake Painter-compatible sink: records (cx,cy,glyph,fg,bg) and renders each
// cell as a 2-wide x 4-tall sub-pixel block by re-deriving the octant mask.
struct CellRec { int cx, cy; char32_t g; Col fg, bg; };

// We can't use the real Painter (needs maya). Re-implement draw_text's loop
// here against a CellRec sink so the math is identical.
#include <cmath>
#include <array>
#include <string_view>

namespace fp {
using namespace chronos::font;
using chronos::gfx::smoothstep;
using chronos::gfx::mix;

// octant bit r*2+c -> which 2x4 sub-pixels are lit (for image expansion)
inline bool bit(char32_t, int){ return false; }

template <class Sink>
float draw(int cols, int rows, float px, float py, float em,
           std::string_view s, Col fg, float weight, Sink&& sink) {
    const float SX=2.f, SY=4.f;
    const float half = std::max(0.85f, weight*em*0.5f);
    struct Placed{const Glyph*g;float ox;};
    std::vector<Placed> placed; float pen=px*SX;
    for(size_t i=0;i<s.size();){char32_t cp;i+=chronos::gfx::utf8_decode(s,i,cp);
        const Glyph&g=glyph(cp);placed.push_back({&g,pen});pen+=(g.adv+0.08f)*em;}
    float sy0=py*SY;
    int cx0=std::max(0,int(std::floor(px))-1), cx1=std::min(cols-1,int(std::ceil(pen/SX))+1);
    int cy0=std::max(0,int(std::floor(py))-1), cy1=std::min(rows-1,int(std::ceil((sy0+em)/SY))+1);
    auto dist=[&](float sx,float sy){float best=1e9f;for(auto&pl:placed){
        float gx=(sx-pl.ox)/em,gy=(sy-sy0)/em;
        if(gx<-0.3f||gx>pl.g->adv+0.3f||gy<-0.3f||gy>1.3f)continue;
        for(auto&st:pl.g->strokes){if(st.size()==1){float dx=(gx-st[0].x)*em,dy=(gy-st[0].y)*em;best=std::min(best,std::sqrt(dx*dx+dy*dy));continue;}
            for(size_t k=0;k+1<st.size();++k)best=std::min(best,seg_dist(gx,gy,st[k],st[k+1])*em);}}return best;};
    auto sub_cov=[&](float sx,float sy){float a=0;for(int sj=0;sj<3;++sj)for(int si=0;si<3;++si){
        float d=dist(sx+(si+0.5f)/3.f,sy+(sj+0.5f)/3.f);a+=smoothstep(half+0.6f,half-0.6f,d);}return a/9.f;};
    for(int cy=cy0;cy<=cy1;++cy)for(int cx=cx0;cx<=cx1;++cx){
        float bx=cx*SX,by=cy*SY;float covs[8];float mx=0;
        for(int r=0;r<4;++r)for(int col=0;col<2;++col){float cov=sub_cov(bx+col,by+r);covs[r*2+col]=cov;mx=std::max(mx,cov);}
        int mask=0;for(int i=0;i<8;++i)if(covs[i]>=0.5f)mask|=1<<i;
        if(!mask)continue;
        float a=smoothstep(0.50f,0.78f,mx);
        sink(cx,cy,mask,covs,fg,a);
    }
    return pen/SX;
}
}

int main(){
    const int COLS=64, ROWS=14;
    const int PXW=2, PXH=4;                 // image px per sub-pixel
    const int IW=COLS*PXW*2, IH=ROWS*PXH*1; // wait: cell=2x4 subpx
    // image: each cell -> 2 wide x 4 tall sub-pixels, each sub-pixel scaled SxS
    const int S=6;                          // upscale per sub-pixel
    const int W=COLS*2*S, H=ROWS*4*S;
    (void)PXW;(void)PXH;(void)IW;(void)IH;
    std::vector<Col> img(W*H, Col{0.05f,0.06f,0.11f});  // sky-ish bg

    Col sky{0.05f,0.06f,0.11f};
    auto blit=[&](int cx,int cy,int mask,const float covs[8],Col fg,float a){
        for(int r=0;r<4;++r)for(int col=0;col<2;++col){
            bool lit = mask & (1<<(r*2+col));
            // per-sub-pixel: blend by its own coverage * cell alpha for smoothness
            float cov = covs[r*2+col];
            float aa = (0.55f+0.45f*a) * (lit?1.f:cov*0.5f);
            aa = std::clamp(aa,0.f,1.f);
            Col ink = mix(sky,fg,aa);
            int sx=(cx*2+col)*S, sy=(cy*4+r)*S;
            for(int yy=0;yy<S;++yy)for(int xx=0;xx<S;++xx){
                int X=sx+xx,Y=sy+yy; if(X<0||X>=W||Y<0||Y>=H)continue;
                img[Y*W+X]=ink;
            }
        }
    };

    float em = 9.0f*4*0.72f;   // ~ rows*4*0.72
    Col accent{0.78f,0.86f,1.0f};
    Col contour{0.02f,0.03f,0.07f};
    Col glow = mix(Col{0,0,0}, accent, 0.40f);
    Col top_ink{1.0f,1.0f,1.0f};
    Col bot_ink = accent;
    // gradient blit: lerp fg top->bottom by cell row within the em-box
    auto blit_grad=[&](float py0,float emh,Col ftop,Col fbot){
        return [&,py0,emh,ftop,fbot](int cx,int cy,int mask,const float covs[8],Col,float a){
            float vt=std::clamp((cy*4.f - py0*4.f)/emh,0.f,1.f);
            Col fg=mix(ftop,fbot,vt);
            for(int r=0;r<4;++r)for(int col=0;col<2;++col){
                bool lit=mask&(1<<(r*2+col)); float cov=covs[r*2+col];
                float aa=(0.55f+0.45f*a)*(lit?1.f:cov*0.5f); aa=std::clamp(aa,0.f,1.f);
                Col ink=mix(sky,fg,aa);
                int sx=(cx*2+col)*S, sy=(cy*4+r)*S;
                for(int yy=0;yy<S;++yy)for(int xx=0;xx<S;++xx){int X=sx+xx,Y=sy+yy;if(X<0||X>=W||Y<0||Y>=H)continue;img[Y*W+X]=ink;}
            }
        };
    };
    fp::draw(COLS,ROWS,1,1,em,"22:50",glow,0.30f,blit);
    fp::draw(COLS,ROWS,1,1,em,"22:50",contour,0.20f,blit);
    fp::draw(COLS,ROWS,1,1,em,"22:50",top_ink,0.135f,blit_grad(1,em,top_ink,bot_ink));

    // seconds after the minutes, smaller, baseline-style
    float endx = 1.f + chronos::font::measure_em("22:50")*em/2.f;
    float sq = em*0.40f;
    float sy2 = 1.f + (em - sq)/4.f;
    fp::draw(COLS,ROWS,endx+1.0f,sy2,sq,"35",contour,0.22f,blit);
    fp::draw(COLS,ROWS,endx+1.0f,sy2,sq,"35",top_ink,0.135f,blit_grad(sy2,sq,top_ink,bot_ink));

    bool ascii = getenv("ASCII")!=nullptr;
    if(ascii){
        const char* ramp=" .:-=+*#%@";
        for(int sy=0; sy<ROWS*4; ++sy){
            for(int sx=0; sx<COLS*2; ++sx){
                Col c=img[(sy*S)*W + (sx*S)];
                float b=(c.r+c.g+c.b)/3.f;
                int idx=std::clamp(int(b*9.999f),0,9);
                std::putchar(ramp[idx]);
            }
            std::putchar('\n');
        }
        return 0;
    }
    std::printf("P6\n%d %d\n255\n",W,H);
    for(int i=0;i<W*H;++i){
        auto cl=[&](float v){return (unsigned char)std::clamp(int(v*255+0.5f),0,255);};
        unsigned char px[3]={cl(img[i].r),cl(img[i].g),cl(img[i].b)};
        std::fwrite(px,1,3,stdout);
    }
    return 0;
}
