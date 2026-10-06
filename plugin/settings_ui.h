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
inline void Load(HMODULE module) {
    wchar_t path[32768]{};
    const auto n=GetModuleFileNameW(module,path,32768);
    if(!n || n>=32768) return;
    filename=path; const auto slash=filename.find_last_of(L"\\/");
    if(slash==std::wstring::npos) { filename.clear(); return; }
    filename.resize(slash+1); filename+=L"3dcam-settings.ini";
    Config c;
    const auto version=GetPrivateProfileIntW(L"3dcam",L"version",1,filename.c_str());
    for(unsigned i=0;i<ActionCount;++i) {
        wchar_t key[24]; swprintf_s(key,L"key%u",i);
        c.keys[i]=GetPrivateProfileIntW(L"3dcam",key,c.keys[i],filename.c_str());
    }
    c.sensitivity=GetPrivateProfileIntW(L"3dcam",L"sensitivity",100,filename.c_str());
    c.radius=GetPrivateProfileIntW(L"3dcam",L"radius",9,filename.c_str());
    c.seal=GetPrivateProfileIntW(L"3dcam",L"seal",1,filename.c_str())!=0;
    c.targeting=GetPrivateProfileIntW(L"3dcam",L"targeting",0,filename.c_str())!=0;
    c.mapFollow=GetPrivateProfileIntW(L"3dcam",L"mapFollow",1,filename.c_str())!=0;
    if(version!=1 || !Valid(c)) { Set(Config{}); status.store(1); } else Set(c);
}
inline bool Save() {
    if(filename.empty()) { status.store(3); return false; }
    const auto c=Copy(); const auto temp=filename+L".tmp";
    FILE* file=nullptr;
    if(_wfopen_s(&file,temp.c_str(),L"wb") || !file) { status.store(3); return false; }
    bool ok=std::fprintf(file,"[3dcam]\r\nversion=1\r\nsensitivity=%u\r\nradius=%u\r\nseal=%u\r\ntargeting=%u\r\nmapFollow=%u\r\n",c.sensitivity,c.radius,unsigned(c.seal),unsigned(c.targeting),unsigned(c.mapFollow))>0;
    for(unsigned i=0;i<ActionCount;++i) ok=(std::fprintf(file,"key%u=%u\r\n",i,c.keys[i])>0)&&ok;
    ok=(std::fclose(file)==0)&&ok;
    if(ok) ok=MoveFileExW(temp.c_str(),filename.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
    if(!ok) DeleteFileW(temp.c_str());
    status.store(ok?2:3); return ok;
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
        for(unsigned row=0;row<7;++row) for(unsigned col=0;col<5;) {
            if(!(rows[row]&(16>>col))) { ++col; continue; }
            const auto start=col;
            while(col<5 && (rows[row]&(16>>col))) ++col;
            Rect(b,x+start*scale,y+row*scale,(col-start)*scale,scale,r,g,blue);
        }
    }
}
inline std::string KeyName(unsigned scan) {
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
inline void Draw(target_ring::Batch& b,bool targeting,bool map,std::uint32_t distanceFlags=0) {
    if(b.width<640 || b.height<360) return;
    width.store(b.width);height.store(b.height);frameTick.store(GetTickCount64());
    const Layout l(b.width,b.height);const float s=l.scale;
    const auto gear=l.gear;
    // Original loader bitmap, cropped to its red face. Run-length row quads
    // use the already guarded native solid renderer (no guessed texture ABI).
    const float pixelW=gear.w/GearSize,pixelH=gear.h/GearSize;
    for(unsigned y=0;y<GearSize;++y) for(unsigned x=0;x<GearSize;) {
        const auto color=GearPixels[y*GearSize+x];const auto start=x++;
        while(x<GearSize && GearPixels[y*GearSize+x]==color) ++x;
        Rect(b,gear.x+start*pixelW,gear.y+y*pixelH,(x-start)*pixelW,pixelH,
            float((color>>16)&255)/255,float((color>>8)&255)/255,float(color&255)/255);
    }
    if(!open.load()) return;
    const auto c=Copy();
    const auto box=[&](float x,float y,float w,float h) { Rect(b,l.x+x*s,l.y+y*s,w*s,h*s,0.20f,0.16f,0.12f); };
    const auto text=[&](float x,float y,const char* str) { Text(b,l.x+x*s,l.y+y*s,2*s,str); };
    Rect(b,l.x-2*s,l.y-2*s,544*s,474*s,0.6f,0.43f,0.2f);
    Rect(b,l.x,l.y,540*s,470*s,0.045f,0.035f,0.03f);
    text(18,18,"3D CAMERA SETTINGS");box(490,10,36,28);text(502,16,"X");
    for(int i=0;i<ActionCount;++i) {
        text(18,float(65+i*30),Labels[i]);box(330,float(58+i*30),184,25);
        const auto key=waiting.load()==i?std::string("PRESS KEY"):KeyName(c.keys[i]);
        text(341,float(64+i*30),key.c_str());
    }
    box(18,280,240,28);text(27,287,targeting?"TARGETING: ON":"TARGETING: OFF");
    box(280,280,235,28);text(290,287,map?"MAP FOLLOW: ON":"MAP FOLLOW: OFF");
    box(18,317,240,28);text(27,324,c.seal?"TARGET SEAL: ON":"TARGET SEAL: OFF");
    if(distanceFlags&distance_control::Present) { box(280,317,235,28);text(290,324,distance_control::Label(distanceFlags)); }
    text(18,362,"MOUSE SENSITIVITY"); text(18,396,"CENTER RADIUS");
    for(int y:{355,389}) { box(335,float(y),38,26);box(478,float(y),38,26);text(349,float(y+6),"-");text(491,float(y+6),"+"); }
    text(385,362,(std::to_string(c.sensitivity)+"%").c_str());text(396,396,(std::to_string(c.radius)+"%").c_str());
    box(18,429,160,26);text(27,435,"DEFAULTS");
    const int st=status.load();
    Text(b,l.x+190*s,l.y+434*s,s,st==1?"RESERVED KEY OR CONFLICT":st==3?"SAVE FAILED - CHECK LOG":st==2?"SAVED - GAME NOT PAUSED":"GAME BINDS NOT CHECKED");
}
}
