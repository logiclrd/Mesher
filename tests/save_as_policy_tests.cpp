#include "mesher/save_as_policy.hpp"
#include "mesher/native_dialog_path.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace {
void check(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
void testDialogCwdSelectionPolicy() {
    namespace fs=std::filesystem;
    const auto original=fs::current_path();
    // macOS may expose /var as a symlink to /private/var. current_path()
    // reports the physical path, so use that representation for the
    // temporary test root before comparing any accepted CWDs.
    const auto scratch=fs::canonical(fs::temp_directory_path())/
        ("mesher-dialog-cwd-"+std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Restore {
        fs::path cwd,root;
        ~Restore() {
            std::error_code ec;
            fs::current_path(cwd,ec);
            fs::remove_all(root,ec);
        }
    } restore{original,scratch};
    const auto first=scratch/"one";
    const auto second=scratch/"two";
    fs::create_directories(first);
    fs::create_directories(second);
    fs::current_path(first);
    check(mesher::nativeDialogInitialDirectory()==first,
          "Dialog starts in CWD, not the suggested filename's directory");

    // Relative selection is made absolute BEFORE chdir. This must work
    // for non-existent Save As targets, as well as existing Open files.
    const auto chosen=mesher::acceptNativeDialogFilePath("mesh.mshr");
    check(chosen==first/"mesh.mshr"&&chosen.is_absolute()&&
          fs::current_path()==first,
          "Accepted relative Save As path is absolute and retains its parent");
    const auto other=mesher::acceptNativeDialogFilePath(
        second/"test-file.obj");
    check(other==second/"test-file.obj"&&
          fs::current_path()==second&&
          mesher::nativeDialogInitialDirectory()==second,
          "Accepted selection changes CWD for the next Open or Save As");
    const auto normalized=mesher::acceptNativeDialogFilePath(
        first/".."/"one"/"named.ply");
    check(normalized==first/"named.ply"&&
          fs::current_path()==first,
          "Path normalization precedes the global CWD transition");

    // Cancellation never invokes acceptance and therefore cannot move CWD.
    const std::optional<fs::path> cancelled=std::nullopt;
    if(cancelled)(void)mesher::acceptNativeDialogFilePath(*cancelled);
    check(fs::current_path()==first,
          "Cancelled dialog must not change CWD");
    bool refused=false;
    try {(void)mesher::acceptNativeDialogFilePath({});}
    catch(const std::invalid_argument&) {refused=true;}
    check(refused&&fs::current_path()==first,
          "Empty accepted path is refused without changing CWD");
    refused=false;
    try {(void)mesher::acceptNativeDialogFilePath(
        scratch/"missing"/"file.obj");}
    catch(const fs::filesystem_error&) {refused=true;}
    check(refused&&fs::current_path()==first,
          "Failure to enter selected parent leaves process CWD unchanged");
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
        testDialogCwdSelectionPolicy();
        testNoWriteOnCancelledDialog();
        testOnlyMshrPathsReachCheckpointWorker();
        testQueueFailureDoesNotBecomeASuccess();
        std::cout<<"Native Save As checkpoint selection policies: PASS\n";
    }catch(const std::exception& error) {
        std::cerr<<"Save As regression: "<<error.what()<<'\n';
        return 1;
    }
}
