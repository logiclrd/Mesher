#include "mesher/native_save_dialog.hpp"
#include "mesher/native_dialog_path.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <shobjidl.h>
#include <shlobj_core.h>
#include <memory>
#include <array>
#include <vector>
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
    require(dialog->SetFileName(suggestion.filename().c_str()),"SetFileName");
    // Force this dialog to the current process directory, even if Shell
    // remembers another location or the last checkpoint was elsewhere.
    const auto cwd=nativeDialogInitialDirectory();
    IShellItem* rawFolder=nullptr;
    require(SHCreateItemFromParsingName(
        cwd.c_str(),nullptr,IID_PPV_ARGS(&rawFolder)),"Save CWD folder");
    ComPtr<IShellItem> folder(rawFolder);
    require(dialog->SetFolder(folder.get()),"SetFolder Save CWD");
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
    return acceptNativeDialogFilePath(destination);
}
std::optional<NativeMeshExportSelection> nativeSaveMeshExportDialog(
    GLFWwindow* owner,const std::filesystem::path& suggestion) {
    ComApartment apartment;
    IFileSaveDialog* raw=nullptr;
    require(CoCreateInstance(CLSID_FileSaveDialog,nullptr,CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&raw)),"CoCreateInstance Export");
    ComPtr<IFileSaveDialog> dialog(raw);
    DWORD flags=0;
    require(dialog->GetOptions(&flags),"GetOptions Export");
    require(dialog->SetOptions(flags|FOS_FORCEFILESYSTEM|FOS_OVERWRITEPROMPT|
                               FOS_STRICTFILETYPES|FOS_PATHMUSTEXIST),
            "SetOptions Export");
    // The filter strings must remain alive until the modal COM dialog ends.
    std::vector<std::wstring> titles,patterns;
    titles.reserve(meshExportFormats.size());
    patterns.reserve(meshExportFormats.size());
    std::array<COMDLG_FILTERSPEC,meshExportFormats.size()> filters{};
    for(std::size_t i=0;i<meshExportFormats.size();++i) {
        const auto& entry=meshExportFormats[i];
        titles.emplace_back(entry.label.begin(),entry.label.end());
        patterns.emplace_back(L"*."+std::wstring(
            entry.extension.begin(),entry.extension.end()));
        filters[i]={titles.back().c_str(),patterns.back().c_str()};
    }
    require(dialog->SetFileTypes(static_cast<UINT>(filters.size()),
                                  filters.data()),"SetFileTypes Export");
    require(dialog->SetFileTypeIndex(
        static_cast<UINT>(MeshExportPreset::PlyLittleMeshAlpha)+1u),
        "SetFileTypeIndex Export");
    if(!suggestion.filename().empty())
        require(dialog->SetFileName(suggestion.filename().c_str()),
                "SetFileName Export");
    const auto cwd=nativeDialogInitialDirectory();
    IShellItem* folderRaw=nullptr;
    require(SHCreateItemFromParsingName(
        cwd.c_str(),nullptr,IID_PPV_ARGS(&folderRaw)),
        "Export CWD folder");
    ComPtr<IShellItem> folder(folderRaw);
    require(dialog->SetFolder(folder.get()),"SetFolder Export CWD");
    const HRESULT shown=dialog->Show(
        owner?glfwGetWin32Window(owner):nullptr);
    if(shown==HRESULT_FROM_WIN32(ERROR_CANCELLED))return std::nullopt;
    require(shown,"Show Export");
    UINT selectedIndex=0;
    require(dialog->GetFileTypeIndex(&selectedIndex),
            "GetFileTypeIndex Export");
    if(selectedIndex==0||selectedIndex>meshExportFormats.size())
        throw std::runtime_error("Windows Export selected an unknown file type");
    IShellItem* resultRaw=nullptr;
    require(dialog->GetResult(&resultRaw),"GetResult Export");
    ComPtr<IShellItem> result(resultRaw);
    PWSTR pathname=nullptr;
    require(result->GetDisplayName(SIGDN_FILESYSPATH,&pathname),
            "GetDisplayName Export");
    const std::filesystem::path chosen(pathname);
    CoTaskMemFree(pathname);
    const auto preset=static_cast<MeshExportPreset>(selectedIndex-1u);
    return NativeMeshExportSelection{
        acceptNativeDialogFilePath(validateExportFilename(chosen,preset)),preset};
}
bool nativeConfirmMeshExportDisclosure(GLFWwindow* owner,
                                       std::string_view disclosure) {
    const std::string utf8(disclosure);
    const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,
        utf8.c_str(),-1,nullptr,0);
    if(count<=0)throw std::runtime_error("Cannot display export warning");
    std::wstring message(static_cast<std::size_t>(count),L'\0');
    (void)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8.c_str(),
                              -1,message.data(),count);
    message.resize(static_cast<std::size_t>(count-1));
    message+=L"\n\nContinue with this export?";
    const auto choice=MessageBoxW(owner?glfwGetWin32Window(owner):nullptr,
        message.c_str(),L"Confirm mesh export format",
        MB_YESNOCANCEL|MB_ICONWARNING|MB_TASKMODAL|MB_DEFBUTTON2);
    if(choice==IDYES)return true;
    if(choice==IDCANCEL||choice==IDNO)return false;
    throw std::runtime_error("Windows mesh export confirmation failed");
}
void nativeMeshExportErrorMessage(GLFWwindow* owner,const std::string& message) {
    const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,
        message.c_str(),-1,nullptr,0);
    std::wstring text;
    if(count>0) {
        text.resize(static_cast<std::size_t>(count));
        (void)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,
            message.c_str(),-1,text.data(),count);
    }else text=L"Unable to export the selected mesh.";
    (void)MessageBoxW(owner?glfwGetWin32Window(owner):nullptr,text.c_str(),
        L"Mesher - Export failed",MB_OK|MB_ICONERROR|MB_TASKMODAL);
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
        {L"All supported files (*.mshr, *.obj, *.ply, *.stl)",
         L"*.mshr;*.obj;*.ply;*.stl"},
        {L"Mesher mesh files (*.mshr)",L"*.mshr"},
        {L"Wavefront OBJ (*.obj)",L"*.obj"},
        {L"Stanford PLY (*.ply)",L"*.ply"},
        {L"STL surface (*.stl)",L"*.stl"},
        {L"All Files (*.*)",L"*.*"}};
    require(dialog->SetFileTypes(6,filters),"SetFileTypes Open");
    require(dialog->SetFileTypeIndex(1),"SetFileTypeIndex Open");
    (void)suggestion; // The last filename never chooses the starting folder.
    const auto cwd=nativeDialogInitialDirectory();
    IShellItem* folderRaw=nullptr;
    require(SHCreateItemFromParsingName(
        cwd.c_str(),nullptr,IID_PPV_ARGS(&folderRaw)),"Open CWD folder");
    ComPtr<IShellItem> folder(folderRaw);
    require(dialog->SetFolder(folder.get()),"SetFolder Open CWD");
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
    if(index<1||index>6)
        throw std::runtime_error("Open dialog selected unknown file type");
    // Both the default "All supported files" and unrestricted "All Files"
    // inspect the file's CONTENT, not its extension.
    const auto filter=index==2?MeshOpenFilter::MesherCheckpoint:
                      index==3?MeshOpenFilter::WavefrontObj:
                      index==4?MeshOpenFilter::StanfordPly:
                      index==5?MeshOpenFilter::StereolithographyStl:
                               MeshOpenFilter::AllFiles;
    return NativeOpenSelection{acceptNativeDialogFilePath(selected),filter};
}
std::optional<bool> nativePlyAlphaOpacityDialog(
    GLFWwindow* owner,float minimum,float maximum) {
    const auto low=static_cast<int>(minimum*255.f+0.5f);
    const auto high=static_cast<int>(maximum*255.f+0.5f);
    const std::wstring text=L"PLY vertex alpha ranges from "+
        std::to_wstring(low)+L" to "+std::to_wstring(high)+
        L" / 255. These values might be opacity or unrelated data.\n\n"
        L"Yes: import as Mesher alpha.\n"
        L"No: display opaque RGB; preserve source alpha for export.\n"
        L"Cancel: leave the existing mesh unchanged.";
    const int result=MessageBoxW(owner?glfwGetWin32Window(owner):nullptr,
        text.c_str(),L"Interpret PLY alpha?",
        MB_YESNOCANCEL|MB_ICONQUESTION|MB_TASKMODAL|MB_DEFBUTTON2);
    if(result==IDYES)return true;
    if(result==IDNO)return false;
    if(result==IDCANCEL)return std::nullopt;
    throw std::runtime_error("Windows PLY alpha prompt failed");
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
