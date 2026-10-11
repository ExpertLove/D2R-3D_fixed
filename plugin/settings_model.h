#pragma once
#include <array>
#include <algorithm>
namespace camera_settings {
enum Action { Next,Previous,Elite,Center,Look,Movement,Camera,View,Settings,ActionCount };
inline constexpr const char* Labels[]={"NEXT TARGET","PREVIOUS TARGET","ELITE TARGET","CENTER TARGET","MOUSE LOOK","RELATIVE WASD","3D CAMERA","VIEW PRESET","OPEN SETTINGS"};
static_assert(sizeof(Labels)/sizeof(Labels[0])==ActionCount);
// Key value 0 = unbound. Shoulder: 0 centered, 1 right, 2 left.
inline constexpr unsigned Unbound=0, MinFov=45, MaxFov=90, ShoulderModes=3;
struct Config {
    std::array<unsigned,ActionCount> keys{0x21,0x22,0x2d,0x2f,0x44,0x43,0x58,0x57,Unbound};
    unsigned sensitivity=100,radius=9,fov=60,shoulder=0;
    bool seal=true,targeting=false,mapFollow=true,invertY=false;
};
// Physical single keys; modifiers, game UI safety gates and movement reserved.
inline bool Allowed(unsigned scan) {
    if(scan<2 || scan>0x58) return false;
    for(unsigned reserved:{0x0eu,0x0fu,0x10u,0x11u,0x12u,0x14u,0x17u,0x19u,0x1cu,0x1du,0x1eu,0x1fu,0x20u,0x23u,0x29u,0x2au,0x2eu,0x36u,0x38u,0x3au,0x45u,0x46u,0x54u,0x55u,0x56u})
        if(scan==reserved) return false;
    // Reject keypad/navigation to avoid NumLock-dependent aliases.
    return !(scan>=0x47 && scan<=0x53);
}
inline bool ValidKey(unsigned scan) { return scan==Unbound || Allowed(scan); }
inline bool Valid(const Config& c) {
    if(c.sensitivity<25 || c.sensitivity>250 || c.radius<3 || c.radius>18 ||
       c.fov<MinFov || c.fov>MaxFov || c.shoulder>=ShoulderModes) return false;
    for(unsigned i=0;i<ActionCount;++i) {
        if(!ValidKey(c.keys[i])) return false;
        for(unsigned j=0;j<i;++j) if(c.keys[i]!=Unbound && c.keys[j]==c.keys[i]) return false;
    }
    return true;
}
// Repair a hand-edited/older INI field by field instead of discarding every
// setting. Returns false when anything had to be replaced.
inline bool Sanitize(Config& c) {
    const Config defaults;
    bool clean=true;
    const auto range=[&](unsigned& v,unsigned low,unsigned high,unsigned fallback) {
        if(v<low || v>high) { v=fallback; clean=false; }
    };
    range(c.sensitivity,25,250,defaults.sensitivity);
    range(c.radius,3,18,defaults.radius);
    range(c.fov,MinFov,MaxFov,defaults.fov);
    range(c.shoulder,0,ShoulderModes-1,defaults.shoulder);
    for(unsigned i=0;i<ActionCount;++i) {
        bool ok=ValidKey(c.keys[i]);
        for(unsigned j=0;ok && j<i;++j) if(c.keys[i]!=Unbound && c.keys[j]==c.keys[i]) ok=false;
        if(ok) continue;
        clean=false;
        // Prefer the default; if that is taken by an earlier action, leave unbound.
        c.keys[i]=defaults.keys[i];
        for(unsigned j=0;j<i;++j) if(c.keys[i]!=Unbound && c.keys[j]==c.keys[i]) c.keys[i]=Unbound;
    }
    return clean;
}
inline bool Bind(Config& c,unsigned action,unsigned scan) {
    if(action>=ActionCount || !Allowed(scan)) return false;
    for(unsigned i=0;i<ActionCount;++i) if(i!=action && c.keys[i]==scan) return false;
    c.keys[action]=scan; return true;
}
inline bool Unbind(Config& c,unsigned action) {
    if(action>=ActionCount) return false;
    c.keys[action]=Unbound; return true;
}
inline int ActionForKey(const Config& c,unsigned scan) {
    if(scan==Unbound) return -1;
    for(int i=0;i<ActionCount;++i) if(c.keys[i]==scan) return i;
    return -1;
}
inline const char* ShoulderName(unsigned mode) { return mode==1?"RIGHT":mode==2?"LEFT":"CENTER"; }
// Hit codes >=100 are buttons; 0..ActionCount-1 are key rows.
enum Hit { HitClose=100,HitTargeting,HitMap,HitSeal,HitSensDown,HitSensUp,HitRadiusDown,HitRadiusUp,
    HitDefaults,HitDistance,HitInvert,HitShoulder,HitFovDown,HitFovUp };
struct Rect { float x,y,w,h; bool contains(float px,float py) const { return px>=x && py>=y && px<x+w && py<y+h; } };
// Panel coordinates are in a 1280x720 reference space, scaled uniformly.
inline constexpr float PanelW=540,PanelH=600,RowY=58,RowStep=30,ToggleY=336,ToggleStep=36,SliderY=448,SliderStep=34,DefaultsY=556;
struct Layout {
    float scale=1,x=0,y=0; Rect gear{};
    Layout(unsigned width,unsigned height) {
        scale=std::min(float(width)/1280,float(height)/720);
        x=(width-PanelW*scale)/2; y=(height-PanelH*scale)/2;
        gear={width-52*scale,height*0.38f,30*scale,30*scale};
    }
    Rect rect(float px,float py,float w,float h) const { return {x+px*scale,y+py*scale,w*scale,h*scale}; }
    int hit(float px,float py) const {
        if(rect(490,10,36,28).contains(px,py)) return HitClose;
        for(int i=0;i<ActionCount;++i) if(rect(330,RowY+i*RowStep,184,25).contains(px,py)) return i;
        const int toggles[3][2]={{HitTargeting,HitMap},{HitSeal,HitDistance},{HitInvert,HitShoulder}};
        for(int r=0;r<3;++r) {
            if(rect(18,ToggleY+r*ToggleStep,240,28).contains(px,py)) return toggles[r][0];
            if(rect(280,ToggleY+r*ToggleStep,235,28).contains(px,py)) return toggles[r][1];
        }
        const int sliders[3][2]={{HitSensDown,HitSensUp},{HitRadiusDown,HitRadiusUp},{HitFovDown,HitFovUp}};
        for(int r=0;r<3;++r) {
            if(rect(335,SliderY+r*SliderStep,38,26).contains(px,py)) return sliders[r][0];
            if(rect(478,SliderY+r*SliderStep,38,26).contains(px,py)) return sliders[r][1];
        }
        if(rect(18,DefaultsY,160,26).contains(px,py)) return HitDefaults;
        return -1;
    }
};
}
