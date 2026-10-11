#pragma once
// Short hotkey feedback ("MOUSE LOOK ON", "VIEW: CLOSE"...) drawn with the same
// verified native solid UI packets as the settings panel. Any thread may Show();
// the render thread copies the text under a tiny lock. No game memory access.
#include <windows.h>
#include <cctype>
#include <cstring>
#include "settings_ui.h"

namespace hud_toast {
inline constexpr unsigned MaxText=40;
inline constexpr ULONGLONG DurationMs=1600, FadeMs=450;
inline SRWLOCK lock=SRWLOCK_INIT;
inline char text[MaxText+1]{};
inline ULONGLONG shownAt=0;

inline void Show(const char* message,ULONGLONG now=GetTickCount64()) {
    if(!message) return;
    char copy[MaxText+1]{};
    unsigned n=0;
    for(;message[n] && n<MaxText;++n) copy[n]=char(std::toupper(static_cast<unsigned char>(message[n])));
    AcquireSRWLockExclusive(&lock);
    std::memcpy(text,copy,sizeof text); shownAt=now?now:1;
    ReleaseSRWLockExclusive(&lock);
}
inline void Clear() { AcquireSRWLockExclusive(&lock); text[0]=0; shownAt=0; ReleaseSRWLockExclusive(&lock); }
// Opacity envelope: full, then a linear fade over the last FadeMs.
inline float Alpha(ULONGLONG age) {
    if(age>=DurationMs) return 0.0f;
    if(age+FadeMs<=DurationMs) return 1.0f;
    return float(DurationMs-age)/float(FadeMs);
}
// Pure drawing: centred near the top, above gameplay, below nothing native.
inline bool DrawText(target_ring::Batch& b,const char* message,float alpha) {
    if(!message || !*message || !(alpha>0) || b.width<640 || b.height<360) return false;
    const float s=std::min(float(b.width)/1280,float(b.height)/720);
    const float glyph=2.0f*s, advance=6.0f*glyph;
    const auto length=float(std::strlen(message));
    const float textW=length*advance-glyph, padX=12*s, padY=8*s;
    const float boxW=textW+2*padX, boxH=7*glyph+2*padY;
    const float x=(float(b.width)-boxW)*0.5f, y=float(b.height)*0.13f;
    const unsigned before=b.count;
    camera_settings::Rect(b,x-2*s,y-2*s,boxW+4*s,boxH+4*s,0.6f,0.43f,0.2f,0.85f*alpha);
    camera_settings::Rect(b,x,y,boxW,boxH,0.045f,0.035f,0.03f,0.82f*alpha);
    const unsigned textStart=b.count;
    camera_settings::Text(b,x+padX,y+padY,glyph,message,1.0f,0.86f,0.55f);
    for(unsigned i=textStart;i<b.count;++i) b.quads[i].packet.opacity=alpha;
    return b.count>before;
}
inline bool Draw(target_ring::Batch& b,ULONGLONG now=GetTickCount64()) {
    char copy[MaxText+1]{}; ULONGLONG at=0;
    AcquireSRWLockShared(&lock);
    std::memcpy(copy,text,sizeof copy); at=shownAt;
    ReleaseSRWLockShared(&lock);
    if(!at || !copy[0] || now<at) return false;
    return DrawText(b,copy,Alpha(now-at));
}
} // namespace hud_toast
