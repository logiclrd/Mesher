#include "mesher/native_save_dialog.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <spawn.h>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern "C" char** environ;

namespace mesher {
namespace {

bool executableOnPath(const std::string& command) {
    const char* environment=std::getenv("PATH");
    if(!environment)return false;
    std::string_view path(environment);
    while(true) {
        const auto colon=path.find(':');
        const auto directory=path.substr(0,colon);
        const auto candidate=
            std::filesystem::path(directory.empty()?".":std::string(directory))/
            command;
        if(access(candidate.c_str(),X_OK)==0)return true;
        if(colon==std::string_view::npos)break;
        path.remove_prefix(colon+1);
    }
    return false;
}

std::optional<std::filesystem::path> runPicker(
    const std::vector<std::string>& command) {
    int fds[2]{};
    if(pipe(fds)<0)throw std::runtime_error(
        std::string("Cannot open native dialog output pipe: ")+std::strerror(errno));
    // The sculpt and checkpoint workers are live. posix_spawnp, unlike
    // fork followed by allocations in the child, does not expose the
    // multithreaded parent to unsafe post-fork C++ operations.
    posix_spawn_file_actions_t actions{};
    const int init=posix_spawn_file_actions_init(&actions);
    if(init!=0) {
        close(fds[0]);close(fds[1]);
        throw std::runtime_error("Cannot initialize Save As process actions");
    }
    const int dup=posix_spawn_file_actions_adddup2(&actions,fds[1],STDOUT_FILENO);
    const int readClose=posix_spawn_file_actions_addclose(&actions,fds[0]);
    const int writeClose=posix_spawn_file_actions_addclose(&actions,fds[1]);
    if(dup!=0||readClose!=0||writeClose!=0) {
        posix_spawn_file_actions_destroy(&actions);
        close(fds[0]);close(fds[1]);
        throw std::runtime_error("Cannot redirect native dialog output");
    }
    std::vector<char*> argv;
    for(const auto& word:command)
        argv.push_back(const_cast<char*>(word.c_str()));
    argv.push_back(nullptr);
    pid_t pid=-1;
    const int launched=posix_spawnp(&pid,argv[0],&actions,nullptr,
                                   argv.data(),environ);
    posix_spawn_file_actions_destroy(&actions);
    close(fds[1]);
    if(launched!=0) {
        close(fds[0]);
        throw std::runtime_error("Cannot launch native save dialog: "+
                                 std::string(std::strerror(launched)));
    }
    std::string result;
    char buffer[4096];
    while(true) {
        const auto n=read(fds[0],buffer,sizeof(buffer));
        if(n>0)result.append(buffer,static_cast<std::size_t>(n));
        else if(n==0)break;
        else if(errno!=EINTR)break;
    }
    close(fds[0]);
    int status=0;
    while(waitpid(pid,&status,0)<0) {
        if(errno!=EINTR)throw std::runtime_error("Cannot collect save dialog result");
    }
    if(!WIFEXITED(status))
        throw std::runtime_error("Native Save As dialog terminated abnormally");
    const int code=WEXITSTATUS(status);
    if(code==1)return std::nullopt; // User cancelled or closed the dialog.
    if(code!=0)throw std::runtime_error(
        "Native Save As dialog failed (exit "+std::to_string(code)+")");
    if(!result.empty()&&result.back()=='\n')result.pop_back();
    if(result.empty())throw std::runtime_error("Native dialog returned no filename");
    return std::filesystem::path(result);
}

} // namespace

std::optional<std::filesystem::path> nativeSaveCheckpointDialog(
    GLFWwindow*,const std::filesystem::path& suggestion) {
    // GUI launchers may start Mesher in a read-only bundle directory.
    // For a bare filename, use the user's Documents (or home) folder;
    // an explicit --checkpoint directory and a previously chosen path win.
    std::filesystem::path initialPath=suggestion;
    if(suggestion.parent_path().empty()) {
        if(const char* home=std::getenv("HOME");home&&*home) {
            const std::filesystem::path homePath(home);
            const auto documents=homePath/"Documents";
            initialPath=(std::filesystem::is_directory(documents)?
                         documents:homePath)/suggestion.filename();
        }
    }
    const auto initial=std::filesystem::absolute(initialPath).string();
    const char* desktop=std::getenv("XDG_CURRENT_DESKTOP");
    const std::string_view environment=desktop?desktop:"";
    const bool kde=environment.find("KDE")!=std::string_view::npos;
    const bool haveKdialog=executableOnPath("kdialog");
    const bool haveZenity=executableOnPath("zenity");
    // kdialog is native on Plasma, Zenity gives a native GTK chooser on
    // other desktops. No shell expansion or interpolation of filenames.
    if(kde&&haveKdialog)
        return runPicker({"kdialog","--title","Save Mesher checkpoint",
                          "--getsavefilename",initial,
                          "Mesher checkpoints (*.mshr)"});
    if(haveZenity)
        return runPicker({"zenity","--file-selection","--save",
                          "--confirm-overwrite","--title=Save Mesher checkpoint",
                          "--filename="+initial,
                          "--file-filter=Mesher checkpoints | *.mshr"});
    if(haveKdialog)
        return runPicker({"kdialog","--title","Save Mesher checkpoint",
                          "--getsavefilename",initial,
                          "Mesher checkpoints (*.mshr)"});
    throw std::runtime_error(
        "No desktop Save As provider installed (install kdialog or zenity)");
}
} // namespace mesher
