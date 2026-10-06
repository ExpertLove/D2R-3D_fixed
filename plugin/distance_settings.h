#pragma once
#include <windows.h>
#include <cstdio>
#include <string>
namespace distance_control {
inline bool LoadSetting(const std::wstring& path) {
    if(path.empty() || GetPrivateProfileIntW(L"renderdistance",L"version",1,path.c_str())!=1) return false;
    return GetPrivateProfileIntW(L"renderdistance",L"enabled",0,path.c_str())==1;
}
inline bool SaveSetting(const std::wstring& path,bool enabled) {
    if(path.empty()) return false;
    const auto temporary=path+L".tmp";
    FILE* file=nullptr;
    if(_wfopen_s(&file,temporary.c_str(),L"wb") || !file) return false;
    bool ok=std::fprintf(file,"[renderdistance]\r\nversion=1\r\nenabled=%u\r\n",unsigned(enabled))>0;
    ok=(std::fclose(file)==0)&&ok;
    if(ok) ok=MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
    if(!ok) DeleteFileW(temporary.c_str());
    return ok;
}
inline std::wstring SettingsPath(HMODULE module) {
    wchar_t path[32768]{};
    const auto n=GetModuleFileNameW(module,path,32768);
    if(!n || n>=32768) return {};
    std::wstring out=path;const auto slash=out.find_last_of(L"\\/");
    if(slash==std::wstring::npos) return {};
    out.resize(slash+1);return out+L"renderdistance-settings.ini";
}
}
