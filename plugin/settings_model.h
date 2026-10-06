#pragma once
#include <array>
#include <algorithm>
namespace camera_settings {
enum Action { Next,Previous,Elite,Center,Look,Movement,Camera,ActionCount };
inline constexpr const char* Labels[]={"NEXT TARGET","PREVIOUS TARGET","ELITE TARGET","CENTER TARGET","MOUSE LOOK","RELATIVE WASD","3D CAMERA"};
struct Config {
    std::array<unsigned,ActionCount> keys{0x21,0x22,0x2d,0x2f,0x44,0x43,0x58};
    unsigned sensitivity=100,radius=9;
    bool seal=true,targeting=false,mapFollow=true;
};
// Physical single keys; modifiers, game UI safety gates and movement reserved.
inline bool Allowed(unsigned scan) {
    if(scan<2 || scan>0x58) return false;
    for(unsigned reserved:{0x0eu,0x0fu,0x10u,0x11u,0x12u,0x14u,0x17u,0x19u,0x1cu,0x1du,0x1eu,0x1fu,0x20u,0x23u,0x29u,0x2au,0x2eu,0x36u,0x38u,0x3au,0x45u,0x46u,0x54u,0x55u,0x56u})
        if(scan==reserved) return false;
    // Reject keypad/navigation to avoid NumLock-dependent aliases.
    return !(scan>=0x47 && scan<=0x53);
}
inline bool Valid(const Config& c) {
    if(c.sensitivity<25 || c.sensitivity>250 || c.radius<3 || c.radius>18) return false;
    for(unsigned i=0;i<ActionCount;++i) {
        if(!Allowed(c.keys[i])) return false;
        for(unsigned j=0;j<i;++j) if(c.keys[j]==c.keys[i]) return false;
    }
    return true;
}
inline bool Bind(Config& c,unsigned action,unsigned scan) {
    if(action>=ActionCount || !Allowed(scan)) return false;
    for(unsigned i=0;i<ActionCount;++i) if(i!=action && c.keys[i]==scan) return false;
    c.keys[action]=scan; return true;
}
struct Rect { float x,y,w,h; bool contains(float px,float py) const { return px>=x && py>=y && px<x+w && py<y+h; } };
struct Layout {
    float scale=1,x=0,y=0; Rect gear{};
    Layout(unsigned width,unsigned height) {
        scale=std::min(float(width)/1280,float(height)/720);
        x=(width-540*scale)/2; y=(height-470*scale)/2;
        gear={width-52*scale,height*0.38f,30*scale,30*scale};
    }
    Rect rect(float px,float py,float w,float h) const { return {x+px*scale,y+py*scale,w*scale,h*scale}; }
    int hit(float px,float py) const {
        if(rect(490,10,36,28).contains(px,py)) return 100; // close
        for(int i=0;i<ActionCount;++i) if(rect(330,float(58+i*30),184,25).contains(px,py)) return i;
        if(rect(18,280,240,28).contains(px,py)) return 101; // targeting
        if(rect(280,280,235,28).contains(px,py)) return 102; // map
        if(rect(18,317,240,28).contains(px,py)) return 103; // seal
        if(rect(335,355,38,26).contains(px,py)) return 104;
        if(rect(478,355,38,26).contains(px,py)) return 105;
        if(rect(335,389,38,26).contains(px,py)) return 106;
        if(rect(478,389,38,26).contains(px,py)) return 107;
        if(rect(18,429,160,26).contains(px,py)) return 108; // defaults
        return -1;
    }
};
}
