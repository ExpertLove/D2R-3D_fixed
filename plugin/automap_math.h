#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace third_person::automap {
struct Point { float x=0,y=0; };
struct Rect { float x=0,y=0,w=0,h=0; };
struct Affine {
    float a=1,b=0,c=0,d=1,tx=0,ty=0;
    Point apply(Point p) const { return {a*p.x+b*p.y+tx,c*p.x+d*p.y+ty}; }
    bool finite() const {
        return std::isfinite(a)&&std::isfinite(b)&&std::isfinite(c)&&std::isfinite(d)&&
            std::isfinite(tx)&&std::isfinite(ty);
    }
};
inline bool HeadingUp(float fx,float fz,float scaleX,float scaleY,Point player,Point center,Affine& out) {
    if (!std::isfinite(fx)||!std::isfinite(fz)||!std::isfinite(scaleX)||!std::isfinite(scaleY)||
        scaleX<0.05f||scaleY<0.05f||scaleX>32||scaleY>32) return false;
    // Native path screen coords: 16*(tileX-tileY),8*(tileX+tileY).
    // Native automap divides by10; render X/Z = 2*tile coordinates.
    const float x=0.8f*scaleX*(fx-fz),y=0.4f*scaleY*(fx+fz);
    const float len=std::hypot(x,y);
    if (!std::isfinite(len)||len<1e-5f) return false;
    const float cosine=-y/len,sine=-x/len;
    out={cosine,-sine,sine,cosine,0,0};
    const auto pivot=out.apply(player);
    out.tx=center.x-pivot.x; out.ty=center.y-pivot.y;
    return out.finite();
}
inline Affine UprightAt(const Affine& map,Point anchor) {
    const auto p=map.apply(anchor);
    return {1,0,0,1,p.x-anchor.x,p.y-anchor.y};
}
inline bool InverseBounds(const Affine& m,Rect rect,Rect& out) {
    const float determinant=m.a*m.d-m.b*m.c;
    if (!m.finite()||!std::isfinite(determinant)||std::abs(determinant)<0.001f||
        !std::isfinite(rect.x)||!std::isfinite(rect.y)||!std::isfinite(rect.w)||!std::isfinite(rect.h)||
        rect.w<=0||rect.h<=0) return false;
    Affine inv{m.d/determinant,-m.b/determinant,-m.c/determinant,m.a/determinant,0,0};
    inv.tx=-(inv.a*m.tx+inv.b*m.ty); inv.ty=-(inv.c*m.tx+inv.d*m.ty);
    const Point corners[]={{rect.x,rect.y},{rect.x+rect.w,rect.y},
        {rect.x,rect.y+rect.h},{rect.x+rect.w,rect.y+rect.h}};
    auto p=inv.apply(corners[0]); float left=p.x,right=p.x,top=p.y,bottom=p.y;
    for (auto corner:corners) {
        p=inv.apply(corner);
        left=std::min(left,p.x); right=std::max(right,p.x);
        top=std::min(top,p.y); bottom=std::max(bottom,p.y);
    }
    out={left,top,right-left,bottom-top};
    return std::isfinite(out.x)&&std::isfinite(out.y)&&std::isfinite(out.w)&&std::isfinite(out.h);
}
struct Vertex { float x,y,z,u,v; };
static_assert(sizeof(Vertex)==20);
// Native f8ce10 uses x=width/2-screenX, y=height/2+screenY. Transform a COPY,
// not the game's vertex array or UVs. Upload is synchronous and copies these bytes.
inline bool TransformQuad(Vertex (&v)[4],const Affine& m,uint32_t width,uint32_t height) {
    if (!m.finite()||width<64||height<64||width>16384||height>16384) return false;
    Vertex result[4];
    for (int i=0;i<4;++i) {
        result[i]=v[i];
        if (!std::isfinite(v[i].x)||!std::isfinite(v[i].y)||!std::isfinite(v[i].z)||
            !std::isfinite(v[i].u)||!std::isfinite(v[i].v)) return false;
        auto p=m.apply({float(width)*0.5f-v[i].x,v[i].y-float(height)*0.5f});
        if (!std::isfinite(p.x)||!std::isfinite(p.y)||std::abs(p.x)>1000000||std::abs(p.y)>1000000) return false;
        result[i].x=float(width)*0.5f-p.x; result[i].y=float(height)*0.5f+p.y;
    }
    for (int i=0;i<4;++i) v[i]=result[i];
    return true;
}
} // namespace third_person::automap
