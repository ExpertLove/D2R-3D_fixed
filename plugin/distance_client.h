#pragma once
#include <D2RLPlugin/context.h>
#include <D2RLPlugin/plugin_communication.h>
#include "distance_control.h"
namespace distance_control {
// Only the camera input thread owns/calls the lease. Render thread reads flags only.
class Client {
    const D2RL::PluginCommunicationService* communication_=nullptr;
    D2RL::PluginCommunication::ServiceLease<Api> lease_;
public:
    std::atomic<std::uint32_t> flags{0};
    void poll(const D2RL::PluginContext* ctx) {
        if(!ctx) return;
        if(!communication_) {
            const D2RL::PluginCommunicationService* service=nullptr;
            if(ctx->QueryService(&service)!=D2RL::ServiceQueryResult::Success ||
               !D2RL::HasPluginCommunicationServiceField(service,D2RL::PluginCommunicationServiceRequiredSize)) return;
            communication_=service;
        }
        if(!lease_) {
            using namespace D2RL::PluginCommunication;
            if(Acquire(ctx,communication_,Provider,ServiceName,Version,sizeof(Api),&lease_)!=Result::Success) { flags=0;return; }
            const auto* table=lease_.Get();
            if(!table || lease_.Size()<sizeof(Api) || table->size<sizeof(Api) || table->version!=Version || !table->state || !table->setEnabled) {
                lease_.Reset();flags=0;return;
            }
        }
        flags=lease_->state();
    }
    bool toggle() {
        if(!lease_) return false;
        const auto before=lease_->state();
        if(!(before&Supported)) { flags=before;return false; }
        const bool result=lease_->setEnabled((before&Requested)?0:1)!=0;
        flags=lease_->state();return result;
    }
    void close() { flags=0;lease_.Reset();communication_=nullptr; }
};
inline Client client;
}
