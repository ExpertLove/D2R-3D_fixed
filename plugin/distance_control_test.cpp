#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "distance_client.h"
#include "distance_settings.h"
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1); } } while(0)
using namespace distance_control;
namespace {
State provider;
unsigned acquired=0,released=0;
bool installed=false,validTable=true;
std::uint32_t __cdecl readState() noexcept { return provider.flags(); }
std::uint32_t __cdecl setState(std::uint32_t value) noexcept { return value<=1 && provider.request(value!=0); }
Api table{sizeof(Api),Version,&readState,&setState};
D2RL::PluginCommunication::Result __cdecl acquire(const D2RL::PluginContext*,const D2RL::PluginCommunication::AcquireServiceRequest* req,D2RL::PluginCommunication::AcquiredService* out) noexcept {
    using namespace D2RL::PluginCommunication;
    CHECK(std::strcmp(req->providerPluginId,Provider)==0 && std::strcmp(req->name,ServiceName)==0);
    CHECK(req->minimumVersion==Version && req->minimumTableSize==sizeof(Api));
    if(!installed) return Result::NotFound;
    ++acquired;
    table.version=validTable?Version:99;
    *out={sizeof(AcquiredService),0,1,&table,Version,sizeof(Api)};return Result::Success;
}
D2RL::PluginCommunication::Result __cdecl release(const D2RL::PluginContext*,D2RL::PluginCommunication::ServiceHandle handle) noexcept {
    CHECK(handle==1);++released;return D2RL::PluginCommunication::Result::Success;
}
D2RL::PluginCommunicationService communication{sizeof(D2RL::PluginCommunicationService),1,nullptr,&acquire,&release,nullptr,nullptr,nullptr};
D2RL::ServiceQueryResult __cdecl query(const D2RL::PluginContext*,D2RL::ServiceId id,std::uint32_t,const void** out) noexcept {
    CHECK(id==D2RL::ServiceId::PluginCommunication);*out=&communication;return D2RL::ServiceQueryResult::Success;
}
}
int main() {
    State state;CHECK(!(state.flags()&Enabled));CHECK(!state.request(true));
    state.support(true);CHECK(state.request(true));
    auto t=state.ticket();CHECK(state.pending(t));CHECK(!(state.flags()&Enabled));
    // A native update in progress must not lose a newer UI intent.
    CHECK(state.request(false));state.complete(t,true);
    CHECK(state.flags()&Enabled);CHECK(state.flags()&Pending);CHECK(!(state.flags()&Requested));
    t=state.ticket();state.complete(t,true);CHECK(!(state.flags()&(Enabled|Pending)));
    CHECK(state.request(true));state.complete(state.ticket(),false);CHECK(state.flags()&SaveFailed);
    state.fault();CHECK(!state.request(true));CHECK(!(state.flags()&(Supported|Enabled)));
    CHECK(std::strcmp(Label(0),"")==0);
    // Atomic persistence with default-off, both directions and invalid-version fallback.
    wchar_t dir[MAX_PATH],path[MAX_PATH];CHECK(GetTempPathW(MAX_PATH,dir));CHECK(GetTempFileNameW(dir,L"dst",0,path));
    CHECK(!LoadSetting(path));CHECK(SaveSetting(path,true));CHECK(LoadSetting(path));
    State restored;restored.support(true);CHECK(restored.request(LoadSetting(path)));CHECK(restored.flags()&Pending);CHECK(!(restored.flags()&Enabled));
    restored.complete(restored.ticket(),true);CHECK(restored.flags()&Enabled);
    CHECK(SaveSetting(path,false));CHECK(!LoadSetting(path));
    CHECK(WritePrivateProfileStringW(L"renderdistance",L"version",L"99",path));CHECK(!LoadSetting(path));
    CHECK(DeleteFileW(path));CHECK(!SaveSetting(L"",true));
    // Optional provider loads after camera; no phantom switch in Standard.
    D2RL::PluginApi api{};api.apiSize=sizeof(api);api.queryService=&query;
    D2RL::PluginContext ctx{};ctx.contextSize=sizeof(ctx);ctx.api=&api;
    Client client;client.poll(&ctx);CHECK(client.flags==0 && acquired==0);CHECK(!client.toggle());
    installed=true;provider.support(true);client.poll(&ctx);CHECK(acquired==1 && (client.flags&Present));
    CHECK(client.toggle());CHECK(provider.flags()&Pending);CHECK(!(provider.flags()&Enabled));
    provider.complete(provider.ticket(),true);client.poll(&ctx);CHECK(client.flags&Enabled);CHECK(acquired==1);
    CHECK(client.toggle());CHECK(!(provider.flags()&Requested));
    client.close();CHECK(client.flags==0 && released==1);
    validTable=false;client.poll(&ctx);CHECK(client.flags==0 && released==2);CHECK(!client.toggle());
    client.close();CHECK(released==2);
    std::puts("distance default/persistence/queued updates/optional leased API/lifetime PASS; no native game calls");
}
