#include "mesher/native_save_dialog.hpp"

#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <stdexcept>
#include <string>

namespace mesher {

std::optional<std::filesystem::path> nativeSaveCheckpointDialog(
    GLFWwindow*,const std::filesystem::path& suggestion) {
    // The native application is already bootstrapped by GLFW. This modal
    // NSSavePanel runs only on the UI thread and needs no external script,
    // accessibility grants, or Homebrew helper on the recipient's Mac.
    @autoreleasepool {
        NSSavePanel* dialog=[NSSavePanel savePanel];
        dialog.title=@"Save Mesher checkpoint";
        dialog.prompt=@"Save";
        dialog.canCreateDirectories=YES;
        dialog.extensionHidden=NO;
        dialog.allowsOtherFileTypes=NO;
        UTType* type=[UTType typeWithFilenameExtension:@"mshr"
                                    conformingToType:UTTypeData];
        if(type)dialog.allowedContentTypes=@[type];
        const auto initial=std::filesystem::absolute(suggestion);
        const std::string filename=initial.filename().string();
        const std::string directory=initial.parent_path().string();
        dialog.nameFieldStringValue=[NSString stringWithUTF8String:filename.c_str()];
        dialog.directoryURL=[NSURL fileURLWithPath:
            [NSString stringWithUTF8String:directory.c_str()] isDirectory:YES];
        const NSModalResponse response=[dialog runModal];
        if(response==NSModalResponseCancel)return std::nullopt;
        if(response!=NSModalResponseOK)
            throw std::runtime_error("macOS Save As did not complete");
        NSURL* selected=dialog.URL;
        if(!selected||!selected.isFileURL)
            throw std::runtime_error("macOS Save As did not select a filesystem path");
        const char* name=selected.fileSystemRepresentation;
        if(!name)throw std::runtime_error("Cannot retrieve macOS Save As path");
        return std::filesystem::path(name);
    }
}
} // namespace mesher
