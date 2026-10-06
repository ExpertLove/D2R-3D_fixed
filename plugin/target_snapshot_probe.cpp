// Diagnostic EXE: VM_READ only. Uses the SAME provider as the DLL; no native
// calls, injection or writes. Run tools/check_target_layout.py first.
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include "target_provider.h"
struct RemoteReader {
    HANDLE process;
    bool operator()(uintptr_t p,void* out,size_t size) {
        SIZE_T n=0;
        return p>=0x10000 && size<=65536 && ReadProcessMemory(process,
            reinterpret_cast<const void*>(p),out,size,&n) && n==size;
    }
};
int main(int argc,char** argv) {
    if (argc!=2) { fprintf(stderr,"Usage: target_snapshot_probe.exe PID (run layout check first)\n"); return 2; }
    const DWORD pid=DWORD(strtoul(argv[1],nullptr,10));
    HANDLE process=OpenProcess(PROCESS_VM_READ,FALSE,pid);
    if (!process) { fprintf(stderr,"OpenProcess VM_READ failed: %lu\n",GetLastError()); return 1; }
    RemoteReader read{process};
    constexpr uintptr_t base=0x140000000;
    uint32_t slot=0,id=0;
    third_person::TargetProvider provider;
    third_person::TargetFrame frame;
    bool ok=read(base+0x2a23704,&slot,4) && slot<8 && read(base+0x2a238f0+slot*4,&id,4) &&
        provider.sample(read,base,1,id,GetTickCount64(),frame);
    CloseHandle(process);
    printf("{\"diagnostic_only\":true,\"pid\":%lu,\"complete\":%s,\"player\":%u,\"playerX\":%.4f,\"playerZ\":%.4f,\"pets\":%u,\"rejected\":%u,\"units\":[\n",
        pid,ok?"true":"false",id,frame.playerX,frame.playerZ,frame.pets,frame.rejected);
    for (uint32_t i=0;i<frame.count;++i) {
        const auto& u=frame.units[i];
        const char* relation=u.relation==third_person::Relation::Hostile?"hostile":
            u.relation==third_person::Relation::OwnedPet?"pet":u.relation==third_person::Relation::Friendly?"friendly":"unknown";
        const char* sight=u.sight==third_person::Sight::Clear?"clear":u.sight==third_person::Sight::Blocked?"blocked":"unknown";
        printf("{\"id\":%u,\"class\":%u,\"x\":%.4f,\"z\":%.4f,\"relation\":\"%s\",\"terrainLOS\":\"%s\"}%s\n",
            u.key.id,u.classId,u.x,u.z,relation,sight,i+1<frame.count?",":"");
    }
    printf("]}\n");
    return ok?0:1;
}
