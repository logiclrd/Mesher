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
std::optional<NativeOpenSelection> nativeOpenMeshDialog(
    GLFWwindow* owner,const std::filesystem::path& suggestion) {
    ComApartment apartment;
    IFileOpenDialog* raw=nullptr;
    require(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,
                             IID_PPV_ARGS(&raw)),"CoCreateInstance Open");
    ComPtr<IFileOpenDialog> dialog(raw);
    DWORD flags=0;
    require(dialog->GetOptions(&flags),"GetOptions Open");
    require(dialog->SetOptions(flags|FOS_FORCEFILESYSTEM|FOS_FILEMUSTEXIST|
                              FOS_PATHMUSTEXIST),"SetOptions Open");
    const COMDLG_FILTERSPEC filters[]{
        {L"Mesher mesh files (*.mshr)",L"*.mshr"},
        {L"Wavefront OBJ (*.obj)",L"*.obj"},
        {L"All Files (*.*)",L"*.*"}};
    require(dialog->SetFileTypes(3,filters),"SetFileTypes Open");
    require(dialog->SetFileTypeIndex(1),"SetFileTypeIndex Open");
    if(!suggestion.parent_path().empty()) {
        const auto absolute=std::filesystem::absolute(suggestion);
        IShellItem* folderRaw=nullptr;
        if(SUCCEEDED(SHCreateItemFromParsingName(
               absolute.parent_path().c_str(),nullptr,
               IID_PPV_ARGS(&folderRaw)))) {
            ComPtr<IShellItem> folder(folderRaw);
            (void)dialog->SetFolder(folder.get());
        }
    }
    const HRESULT shown=dialog->Show(glfwGetWin32Window(owner));
    if(shown==HRESULT_FROM_WIN32(ERROR_CANCELLED))return std::nullopt;
    require(shown,"Show Open");
    IShellItem* resultRaw=nullptr;
    require(dialog->GetResult(&resultRaw),"GetResult Open");
    ComPtr<IShellItem> result(resultRaw);
    PWSTR path=nullptr;
    require(result->GetDisplayName(SIGDN_FILESYSPATH,&path),"GetDisplayName Open");
    const std::filesystem::path selected(path);
    CoTaskMemFree(path);
    UINT index=0;
    require(dialog->GetFileTypeIndex(&index),"GetFileTypeIndex Open");
    if(index<1||index>3)
        throw std::runtime_error("Open dialog selected unknown file type");
    const auto filter=index==1?MeshOpenFilter::MesherCheckpoint:
                      index==2?MeshOpenFilter::WavefrontObj:MeshOpenFilter::AllFiles;
    return NativeOpenSelection{selected,filter};
}
void nativeOpenErrorMessage(GLFWwindow* owner,const std::string& message) {
    const int chars=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,
        message.c_str(),-1,nullptr,0);
    std::wstring unicode;
    if(chars>0) {
        unicode.resize(static_cast<std::size_t>(chars));
        (void)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,
            message.c_str(),-1,unicode.data(),chars);
    }else unicode=L"Unable to open the selected mesh.";
    MessageBoxW(owner?glfwGetWin32Window(owner):nullptr,unicode.c_str(),
                L"Mesher — Open failed",MB_OK|MB_ICONERROR|MB_TASKMODAL);
}
} // namespace mesher
