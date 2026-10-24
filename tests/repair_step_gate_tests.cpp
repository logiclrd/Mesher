#include "mesher/repair_step_gate.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace mesher;
namespace {
using namespace std::chrono_literals;
struct Packet {std::string operation;std::vector<int> faceIds;};
void check(bool value,const char* reason) {
    if(!value)throw std::runtime_error(reason);
}
void boundedTwoFrameNavigationAndOneStepPermit() {
    RepairStepGate<Packet> gate;
    std::atomic<int> completed{0};
    std::thread producer([&] {
        gate.publishAndWait({"ensureFace 1,2,3",{1}});
        gate.publishAndWait({"splitEdge 2,3",{1,2}});
        completed.store(2);
        gate.finish();
    });
    try {
        check(gate.waitForSequence(1,2s),"first operation never reached consumer");
        const auto first=gate.view();
        check(first.waiting&&first.sequence==1&&
              first.current->faceIds==std::vector<int>{1},
              "first immutable frame was not published before pause");
        check(!gate.showPrevious(),"no previous frame exists on first step");
        check(gate.next(),"Next did not release pending operation");
        check(!gate.next(),"Next accumulated a second permit");
        check(gate.waitForSequence(2,2s),"second operation never arrived");
        check(gate.view().waiting&&completed.load()==0,
              "second operation must block independently");
        check(gate.showPrevious(),"old state must remain inspectable");
        auto before=gate.view();
        check(before.showingPrevious&&before.displayed->faceIds.size()==1&&
              before.current->faceIds.size()==2&&completed.load()==0,
              "Previous must switch view without undoing or advancing worker");
        gate.showCurrent();
        check(!gate.view().showingPrevious&&gate.view().displayed->faceIds.size()==2,
              "Current must restore the latest immutable state");
        check(gate.next(),"second Next failed");
        producer.join();
        check(gate.view().finished&&completed.load()==2,
              "worker must finish after exactly two Next commands");
    }catch(...) {gate.cancel();producer.join();throw;}
}
void continueBypassesFutureWaits() {
    RepairStepGate<Packet> gate;
    std::thread producer([&] {
        for(int i=0;i<30;++i)
            gate.publishAndWait({"ensureFace",{i}});
        gate.finish();
    });
    try {
        check(gate.waitForSequence(1,2s),"Continue fixture did not start");
        gate.continueRunning();
        producer.join();
        check(gate.view().sequence==30&&gate.view().finished&&
              gate.view().running&&!gate.next()&&
              gate.view().previous->faceIds.front()==28,
              "Continue must stop blocking but retain ONLY the last two packets");
    }catch(...) {gate.cancel();producer.join();throw;}
}
void cancellationWakesBlockedWorker() {
    RepairStepGate<Packet> gate;
    std::atomic<bool> observed{false};
    std::thread producer([&] {
        try {gate.publishAndWait({"splitEdge",{42}});}
        catch(const RepairStepCancelled&) {observed=true;}
    });
    try {
        check(gate.waitForSequence(1,2s),"Cancel fixture did not start");
        gate.cancel();
        producer.join();
        check(observed&&gate.view().cancelled&&!gate.next(),
              "Cancel must wake producer without accidentally accepting the split");
    }catch(...) {gate.cancel();producer.join();throw;}
}
void rejectsPublishingAfterFinish() {
    RepairStepGate<Packet> gate;
    gate.finish();
    bool refused=false;
    try {gate.publishAndWait({"invalid",{0}});}
    catch(const std::logic_error&) {refused=true;}
    check(refused&&gate.view().sequence==0,"finished gate must refuse new frames");
}
}
int main() {
    try {
        boundedTwoFrameNavigationAndOneStepPermit();
        continueBypassesFutureWaits();
        cancellationWakesBlockedWorker();
        rejectsPublishingAfterFinish();
        std::cout<<"[r06-m16c1] bounded previous/current, single-step, continue, cancellation PASS\n";
    }catch(const std::exception& e) {
        std::cerr<<"[r06-m16c1] FAIL: "<<e.what()<<'\n';
        return 1;
    }
}
