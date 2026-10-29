#include "mesher/save_as_policy.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace {
void check(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
void testNoWriteOnCancelledDialog() {
    std::size_t queued=0;
    const auto enqueue=[&](const std::filesystem::path&){++queued;};
    check(!mesher::dispatchCheckpointSaveAs(std::nullopt,enqueue),
          "Cancelling Save As must report no selection");
    check(queued==0,"Cancellation must not queue a checkpoint");
}
void testOnlyMshrPathsReachCheckpointWorker() {
    std::filesystem::path queued;
    std::size_t calls=0;
    const auto enqueue=[&](const std::filesystem::path& destination) {
        queued=destination;
        ++calls;
    };
    check(mesher::dispatchCheckpointSaveAs(
              std::filesystem::path("session"),enqueue),
          "Valid extension-free selection is accepted");
    check(queued==std::filesystem::path("session.mshr"),
          "Missing extension must be added");
    check(mesher::dispatchCheckpointSaveAs(
              std::filesystem::path("states/final.MSHR"),enqueue),
          "Case-insensitive mshr extension must be preserved");
    check(queued==std::filesystem::path("states/final.MSHR"),
          "Already selected filename must not be rewritten");
    check(mesher::dispatchCheckpointSaveAs(
              std::filesystem::path("states/important data.mshr"),enqueue),
          "Paths with whitespace must remain literal");
    check(queued==std::filesystem::path("states/important data.mshr"),
          "Save As paths must not be shell-interpreted");
    check(calls==3,"Accepted destinations queue exactly once");
    for(const auto& invalid:{"", "recovery.obj", "recovery.stl", "archive.zip"}) {
        try {
            (void)mesher::dispatchCheckpointSaveAs(
                std::filesystem::path(invalid),enqueue);
            throw std::runtime_error("Invalid extension was accepted");
        }catch(const std::invalid_argument&) {
            check(calls==3,"Invalid selection must not queue a write");
        }
    }
}
void testQueueFailureDoesNotBecomeASuccess() {
    std::filesystem::path current="first.mshr";
    const auto enqueue=[&](const std::filesystem::path& path) {
        if(path=="invalid/missing.mshr")
            throw std::runtime_error("Failed to queue");
        current=path; // Same contract used by main.cpp after queue accepted.
    };
    try {
        (void)mesher::dispatchCheckpointSaveAs(
            std::filesystem::path("invalid/missing.mshr"),enqueue);
        throw std::runtime_error("Failed queue unexpectedly accepted");
    }catch(const std::runtime_error& error) {
        check(std::string(error.what())=="Failed to queue",
              "Unexpected failure reason");
        check(current=="first.mshr","Failed queue must not change suggested path");
    }
    check(mesher::dispatchCheckpointSaveAs(
            std::filesystem::path("next-state.mshr"),enqueue),
          "Valid next Save As must be accepted");
    check(current=="next-state.mshr","Accepted path becomes next suggestion");
}
}
int main() {
    try {
        testNoWriteOnCancelledDialog();
        testOnlyMshrPathsReachCheckpointWorker();
        testQueueFailureDoesNotBecomeASuccess();
        std::cout<<"Native Save As checkpoint selection policies: PASS\n";
    }catch(const std::exception& error) {
        std::cerr<<"Save As regression: "<<error.what()<<'\n';
        return 1;
    }
}
