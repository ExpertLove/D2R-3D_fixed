#pragma once
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <string>
#include "settings_model.h"
#include "loader_gear_pixels.h"
#include "distance_control.h"
#include "target_ring.h"
namespace camera_settings {
inline SRWLOCK configLock=SRWLOCK_INIT;
inline Config config;
inline std::atomic<bool> open{false};
inline std::atomic<int> waiting{-1},status{0}; // 1 invalid/conflict,2 saved,3 save failed
inline std::atomic<unsigned> width{0},height{0};
inline std::atomic<ULONGLONG> frameTick{0};
inline std::wstring filename;
inline Config Copy() { AcquireSRWLockShared(&configLock); const auto c=config; ReleaseSRWLockShared(&configLock); return c; }
inline void Set(const Config& c) { AcquireSRWLockExclusive(&configLock); config=c; ReleaseSRWLockExclusive(&configLock); }
inline void Close() { open.store(false); waiting.store(-1); }
// Live camera state persisted alongside the config (tenths, signed).
inline std::atomic<int> zoomTenths{450},pitchTenths{220};
inline int ReadInt(const wchar_t* key,int fallback,const std::wstring& path) {
    wchar_t value[32]{};
    GetPrivateProfileStringW(L"3dcam",key,L"",value,32,path.c_str());
    if(!value[0]) return fallback;
    wchar_t* end=nullptr; const long parsed=wcstol(value,&end,10);
    return end && end!=value && !*end && parsed>=-1000000 && parsed<=1000000 ? int(parsed) : fallback;
}
inline void LoadFile(const std::wstring& path) {
    filename=path;
    Config c;
    const auto version=ReadInt(L"version",1,filename);
    for(unsigned i=0;i<ActionCount;++i) {
        wchar_t key[24]; swprintf_s(key,L"key%u",i);
        const int value=ReadInt(key,int(c.keys[i]),filename);
        c.keys[i]=value<0?~0u:unsigned(value);
    }
    const auto positive=[&](const wchar_t* key,unsigned fallback) { const int v=ReadInt(key,int(fallback),filename); return v<0?~0u:unsigned(v); };
    c.sensitivity=positive(L"sensitivity",c.sensitivity);
    c.radius=positive(L"radius",c.radius);
    c.fov=positive(L"fov",c.fov);
    c.shoulder=positive(L"shoulder",c.shoulder);
    c.seal=ReadInt(L"seal",1,filename)!=0;
    c.targeting=ReadInt(L"targeting",0,filename)!=0;
    c.mapFollow=ReadInt(L"mapFollow",1,filename)!=0;
    c.invertY=ReadInt(L"invertY",0,filename)!=0;
    zoomTenths.store(ReadInt(L"zoom",450,filename));
    pitchTenths.store(ReadInt(L"pitch",220,filename));
    // Older .23 files lack the new keys: defaults fill in, nothing is discarded.
    // Only individually invalid/conflicting fields are repaired.
    const bool clean=version==1 && Sanitize(c);
    Set(c); status.store(clean?0:1);
}
inline void Load(HMODULE module) {
    wchar_t path[32768]{};
    const auto n=GetModuleFileNameW(module,path,32768);
    if(!n || n>=32768) return;
    std::wstring file=path; const auto slash=file.find_last_of(L"\\/");
    if(slash==std::wstring::npos) { filename.clear(); return; }
    file.resize(slash+1); file+=L"3dcam-settings.ini";
    LoadFile(file);
}
inline bool Save() {
    if(filename.empty()) { status.store(3); return false; }
    const auto c=Copy(); const auto temp=filename+L".tmp";
    FILE* file=nullptr;
    if(_wfopen_s(&file,temp.c_str(),L"wb") || !file) { status.store(3); return false; }
    bool ok=std::fprintf(file,"[3dcam]\r\nversion=1\r\nsensitivity=%u\r\nradius=%u\r\nfov=%u\r\nshoulder=%u\r\nseal=%u\r\ntargeting=%u\r\nmapFollow=%u\r\ninvertY=%u\r\nzoom=%d\r\npitch=%d\r\n",
        c.sensitivity,c.radius,c.fov,c.shoulder,unsigned(c.seal),unsigned(c.targeting),unsigned(c.mapFollow),unsigned(c.invertY),
        zoomTenths.load(),pitchTenths.load())>0;
    for(unsigned i=0;i<ActionCount;++i) ok=(std::fprintf(file,"key%u=%u\r\n",i,c.keys[i])>0)&&ok;
    ok=(std::fclose(file)==0)&&ok;
    if(ok) ok=MoveFileExW(temp.c_str(),filename.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
    if(!ok) DeleteFileW(temp.c_str());
    status.store(ok?2:3); return ok;
}
// Render thread publishes the live UI size every active frame, even when
// nothing of ours is drawn (V selection and click hit-testing depend on it).
inline void Observe(unsigned w,unsigned h) {
    if(w<640 || h<360) return;
    width.store(w);height.store(h);frameTick.store(GetTickCount64());
}
inline bool Fresh() { const auto at=frameTick.load(),now=GetTickCount64(); return at && now>=at && now-at<250 && width.load()>=640 && height.load()>=360; }
inline void Rect(target_ring::Batch& b,float x,float y,float w,float h,float r,float g,float blue,float alpha=1) {
    if(b.count>=target_ring::MaxQuads || w<=0 || h<=0) return;
    target_ring::Quad q{};
    // Match native f8ce10 vertex order: right/top, left/top, right/bottom,
    // left/bottom. Left-first reverses triangle winding and is culled by GPU.
    q.points[0]={x+w,y};q.points[1]={x,y};q.points[2]={x+w,y+h};q.points[3]={x,y+h};
    q.packet.rect[0]=x;q.packet.rect[1]=y;q.packet.rect[2]=x+w;q.packet.rect[3]=y+h;
    q.packet.color[0]=r;q.packet.color[1]=g;q.packet.color[2]=blue;q.packet.opacity=alpha;
    b.quads[b.count++]=q;
}
inline const unsigned char* Glyph(char ch) {
    static constexpr unsigned char glyphs[][7]={
        {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
        {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
        {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},
        {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
        {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
        {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
        {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
        {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
        {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
        {14,17,17,15,1,1,14},{0,0,4,31,4,0,0},{0,0,0,31,0,0,0},
        {17,2,4,8,16,17,0},{0,4,0,0,4,0,0},{0,0,0,0,0,0,0}
    };
    if(ch>='A' && ch<='Z') return glyphs[ch-'A'];
    if(ch>='0' && ch<='9') return glyphs[26+ch-'0'];
    return glyphs[ch=='+'?36:ch=='-'?37:ch=='%'?38:ch==':'?39:40];
}
inline void Text(target_ring::Batch& b,float x,float y,float scale,const char* text,float r=0.9f,float g=0.85f,float blue=0.7f) {
    for(;*text;++text,x+=6*scale) {
        const auto* rows=Glyph(*text);
        // Row runs merged vertically with identical runs below: same pixels,
        // roughly 40% fewer native quads for the panel text.
        unsigned used[7]{};
        for(unsigned row=0;row<7;++row) for(unsigned col=0;col<5;) {
            if(!(rows[row]&(16>>col))) { ++col; continue; }
            const auto start=col;
            while(col<5 && (rows[row]&(16>>col))) ++col;
            const unsigned run=((1u<<(col-start))-1)<<(5-col); // run bits in 5-bit row
            const unsigned edges=(start?16u>>(start-1):0)|(col<5?16u>>col:0);
            if(used[row]&run) continue;
            unsigned bottom=row+1;
            while(bottom<7 && (rows[bottom]&run)==run && !(rows[bottom]&edges) && !(used[bottom]&run)) ++bottom;
            for(unsigned k=row;k<bottom;++k) used[k]|=run;
            Rect(b,x+start*scale,y+row*scale,(col-start)*scale,(bottom-row)*scale,r,g,blue);
        }
    }
}
inline std::string KeyName(unsigned scan) {
    if(scan==Unbound) return "NONE";
    if(scan>=0x3b && scan<=0x44) return "F"+std::to_string(scan-0x3b+1);
    if(scan==0x57 || scan==0x58) return scan==0x57?"F11":"F12";
    // Stable US physical names, independent of the active keyboard language.
    static constexpr char row1[]="1234567890-",row2[]="QWERTYUIOP",row3[]="ASDFGHJKL",row4[]="ZXCVBNM";
    if(scan>=2 && scan<=12) return std::string(1,row1[scan-2]);
    if(scan>=0x10 && scan<=0x19) return std::string(1,row2[scan-0x10]);
    if(scan>=0x1e && scan<=0x26) return std::string(1,row3[scan-0x1e]);
    if(scan>=0x2c && scan<=0x32) return std::string(1,row4[scan-0x2c]);
    if(scan==0x39) return "SPACE";
    return "SCAN "+std::to_string(scan);
}
// Original loader bitmap, cropped to its red face. Run-length row quads use the
// already guarded native solid renderer (no guessed texture ABI).
inline void DrawGear(target_ring::Batch& b) {
    const Layout l(b.width,b.height);
    const auto gear=l.gear;
    const float pixelW=gear.w/GearSize,pixelH=gear.h/GearSize;
    for(unsigned y=0;y<GearSize;++y) for(unsigned x=0;x<GearSize;) {
        const auto color=GearPixels[y*GearSize+x];const auto start=x++;
        while(x<GearSize && GearPixels[y*GearSize+x]==color) ++x;
        Rect(b,gear.x+start*pixelW,gear.y+y*pixelH,(x-start)*pixelW,pixelH,
            float((color>>16)&255)/255,float((color>>8)&255)/255,float(color&255)/255);
    }
}
// gear=false while mouse look owns the cursor: it cannot be clicked then, and
// its ~1000 quads used to be uploaded every single frame for nothing.
inline void Draw(target_ring::Batch& b,bool targeting,bool map,std::uint32_t distanceFlags=0,bool gear=true) {
    if(b.width<640 || b.height<360) return;
    Observe(b.width,b.height);
    if(gear || open.load()) DrawGear(b);
    if(!open.load()) return;
    const Layout l(b.width,b.height);const float s=l.scale;
    const auto c=Copy();
    const auto box=[&](float x,float y,float w,float h) { Rect(b,l.x+x*s,l.y+y*s,w*s,h*s,0.20f,0.16f,0.12f); };
    const auto text=[&](float x,float y,const char* str) { Text(b,l.x+x*s,l.y+y*s,2*s,str); };
    Rect(b,l.x-2*s,l.y-2*s,(PanelW+4)*s,(PanelH+4)*s,0.6f,0.43f,0.2f);
    Rect(b,l.x,l.y,PanelW*s,PanelH*s,0.045f,0.035f,0.03f);
    text(18,18,"3D CAMERA SETTINGS");box(490,10,36,28);text(502,16,"X");
    const int wait=waiting.load();
    for(int i=0;i<ActionCount;++i) {
        const float y=RowY+i*RowStep;
        text(18,y+7,Labels[i]);box(330,y,184,25);
        const auto key=wait==i?std::string("PRESS KEY"):KeyName(c.keys[i]);
        text(341,y+6,key.c_str());
    }
    const auto toggle=[&](int row,int column,const std::string& label) {
        const float x=column?280.0f:18.0f,w=column?235.0f:240.0f,y=ToggleY+row*ToggleStep;
        box(x,y,w,28);text(x+(column?10:9),y+7,label.c_str());
    };
    toggle(0,0,targeting?"TARGETING: ON":"TARGETING: OFF");
    toggle(0,1,map?"MAP FOLLOW: ON":"MAP FOLLOW: OFF");
    toggle(1,0,c.seal?"TARGET SEAL: ON":"TARGET SEAL: OFF");
    if(distanceFlags&distance_control::Present) toggle(1,1,distance_control::Label(distanceFlags));
    toggle(2,0,c.invertY?"INVERT Y: ON":"INVERT Y: OFF");
    toggle(2,1,std::string("SHOULDER: ")+ShoulderName(c.shoulder));
    const char* sliders[]={"MOUSE SENSITIVITY","CENTER RADIUS","FIELD OF VIEW"};
    const std::string values[]={std::to_string(c.sensitivity)+"%",std::to_string(c.radius)+"%",std::to_string(c.fov)};
    for(int r=0;r<3;++r) {
        const float y=SliderY+r*SliderStep;
        text(18,y+7,sliders[r]);
        box(335,y,38,26);box(478,y,38,26);text(349,y+6,"-");text(491,y+6,"+");
        text(385+(values[r].size()<4?6.0f*(4-float(values[r].size())):0.0f),y+7,values[r].c_str());
    }
    box(18,DefaultsY,160,26);text(27,DefaultsY+6,"DEFAULTS");
    const int st=status.load();
    const char* hint=wait>=0?"BACKSPACE CLEARS - ESC CANCELS":
        st==1?"RESERVED KEY OR CONFLICT":st==3?"SAVE FAILED - CHECK LOG":st==2?"SAVED - GAME NOT PAUSED":"GAME BINDS NOT CHECKED";
    Text(b,l.x+190*s,l.y+(DefaultsY+5)*s,s,hint);
}
}
