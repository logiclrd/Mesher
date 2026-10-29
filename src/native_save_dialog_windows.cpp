#include "mesher/native_save_dialog.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <shobjidl.h>
#include <shlobj_core.h>
#include <memory>
#include <stdexcept>
#include <string>

namespace mesher {
namespace {
template<class T>
struct ComRelease {
    void operator()(T* pointer) const noexcept {
        if(pointer)pointer->Release();
    }
};
template<class T>
using ComPtr=std::unique_ptr<T,ComRelease<T>>;

void require(HRESULT result,const char* operation) {
    if(FAILED(result))
        throw std::runtime_error(std::string("Windows Save As: ")+operation+
                                 " failed (HRESULT "+std::to_string(result)+")");
}

struct ComApartment {
    bool initialized=false;
    ComApartment() {
        require(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED),
                "CoInitializeEx");
        initialized=true;
    }
    ~ComApartment() {if(initialized)CoUninitialize();}
};
} // namespace

std::optional<std::filesystem::path> nativeSaveCheckpointDialog(
    GLFWwindow* owner,const std::filesystem::path& suggestion) {
    ComApartment apartment;
    IFileSaveDialog* raw=nullptr;
    require(CoCreateInstance(CLSID_FileSaveDialog,nullptr,CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&raw)),"CoCreateInstance");
    ComPtr<IFileSaveDialog> dialog(raw);
    DWORD options=0;
    require(dialog->GetOptions(&options),"GetOptions");
    require(dialog->SetOptions(options|FOS_FORCEFILESYSTEM|FOS_OVERWRITEPROMPT|
                              FOS_STRICTFILETYPES),"SetOptions");
    const COMDLG_FILTERSPEC filter{L"Mesher checkpoints (*.mshr)",L"*.mshr"};
    require(dialog->SetFileTypes(1,&filter),"SetFileTypes");
    require(dialog->SetFileTypeIndex(1),"SetFileTypeIndex");
    require(dialog->SetDefaultExtension(L"mshr"),"SetDefaultExtension");
    const auto absolute=std::filesystem::absolute(suggestion);
    require(dialog->SetFileName(absolute.filename().c_str()),"SetFileName");
    // An unqualified default filename should use the Shell's preferred
    // location (typically Documents), not the packaged app's working dir.
    if(!suggestion.parent_path().empty()) {
        IShellItem* rawFolder=nullptr;
        const HRESULT folderResult=SHCreateItemFromParsingName(
            absolute.parent_path().c_str(),nullptr,IID_PPV_ARGS(&rawFolder));
        if(SUCCEEDED(folderResult)) {
            ComPtr<IShellItem> folder(rawFolder);
            (void)dialog->SetFolder(folder.get());
        }
    }
    const HRESULT shown=dialog->Show(glfwGetWin32Window(owner));
    if(shown==HRESULT_FROM_WIN32(ERROR_CANCELLED))return std::nullopt;
    require(shown,"Show");
    IShellItem* rawResult=nullptr;
    require(dialog->GetResult(&rawResult),"GetResult");
    ComPtr<IShellItem> selection(rawResult);
    PWSTR chosen=nullptr;
    require(selection->GetDisplayName(SIGDN_FILESYSPATH,&chosen),"GetDisplayName");
    const std::filesystem::path destination(chosen);
    CoTaskMemFree(chosen);
    return destination;
}
} // namespace mesher
