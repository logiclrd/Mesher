#include "mesher/native_save_dialog.hpp"

#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <stdexcept>
#include <string>

// The accessory button is a real parser-selection control, and its choice
// also updates Cocoa's visible-file filter as it changes.
@interface MesherOpenFilterController : NSObject {
@public
    NSOpenPanel* panel;
    UTType* checkpointType;
    UTType* objType;
    UTType* plyType;
}
- (void)change:(id)sender;
@end
@implementation MesherOpenFilterController
- (void)change:(id)sender {
    const NSInteger index=[(NSPopUpButton*)sender indexOfSelectedItem];
    if(index==0)panel.allowedContentTypes=checkpointType?@[checkpointType]:@[];
    else if(index==1)panel.allowedContentTypes=objType?@[objType]:@[];
    else if(index==2)panel.allowedContentTypes=plyType?@[plyType]:@[];
    else panel.allowedContentTypes=@[];
}
@end

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
        // Finder launches may have an unwritable process working directory.
        // Do not force that directory for a bare default filename; let
        // NSSavePanel use the user's normal Documents/last-used location.
        const std::string filename=suggestion.filename().string();
        dialog.nameFieldStringValue=[NSString stringWithUTF8String:filename.c_str()];
        if(!suggestion.parent_path().empty()) {
            const auto initial=std::filesystem::absolute(suggestion);
            const std::string directory=initial.parent_path().string();
            dialog.directoryURL=[NSURL fileURLWithPath:
                [NSString stringWithUTF8String:directory.c_str()] isDirectory:YES];
        }
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
std::optional<NativeOpenSelection> nativeOpenMeshDialog(
    GLFWwindow*,const std::filesystem::path& suggestion) {
    @autoreleasepool {
        NSOpenPanel* panel=[NSOpenPanel openPanel];
        panel.title=@"Open Mesher mesh";
        panel.prompt=@"Open";
        panel.canChooseFiles=YES;
        panel.canChooseDirectories=NO;
        panel.allowsMultipleSelection=NO;
        panel.allowsOtherFileTypes=NO;
        // Accessory records the user's selection. The Open filter is not
        // inferred from the returned URL or its extension.
        NSPopUpButton* fileType=[[NSPopUpButton alloc] initWithFrame:
            NSMakeRect(0,0,280,28) pullsDown:NO];
        [fileType addItemsWithTitles:@[
            @"Mesher mesh files (*.mshr)",@"Wavefront OBJ (*.obj)",
            @"Stanford PLY (*.ply)",@"All Files (*)"]];
        UTType* mshr=[UTType typeWithFilenameExtension:@"mshr"
                                     conformingToType:UTTypeData];
        UTType* obj=[UTType typeWithFilenameExtension:@"obj"
                                    conformingToType:UTTypeData];
        UTType* ply=[UTType typeWithFilenameExtension:@"ply"
                                    conformingToType:UTTypeData];
        // Cocoa exposes the selected item, and the accessory controller
        // updates the actual visible filter without using the filename
        // extension as a parser hint. All Files clears the restriction.
        MesherOpenFilterController* controller=
            [[MesherOpenFilterController alloc] init];
        controller->panel=panel;
        controller->checkpointType=mshr;
        controller->objType=obj;
        controller->plyType=ply;
        [fileType setTarget:controller];
        [fileType setAction:@selector(change:)];
        panel.accessoryView=fileType;
        [controller change:fileType];
        if(!suggestion.parent_path().empty()) {
            const auto absolute=std::filesystem::absolute(suggestion);
            const auto directory=absolute.parent_path().string();
            panel.directoryURL=[NSURL fileURLWithPath:
                [NSString stringWithUTF8String:directory.c_str()]
                isDirectory:YES];
        }
        const NSModalResponse response=[panel runModal];
        [controller release];
        if(response==NSModalResponseCancel)return std::nullopt;
        if(response!=NSModalResponseOK)
            throw std::runtime_error("macOS Open did not complete");
        NSURL* selected=panel.URL;
        if(!selected||!selected.isFileURL)
            throw std::runtime_error("macOS Open returned a non-file URL");
        const char* path=selected.fileSystemRepresentation;
        if(!path)throw std::runtime_error("macOS Open path unavailable");
        const auto index=fileType.indexOfSelectedItem;
        const auto filter=index==0?MeshOpenFilter::MesherCheckpoint:
                          index==1?MeshOpenFilter::WavefrontObj:
                          index==2?MeshOpenFilter::StanfordPly:MeshOpenFilter::AllFiles;
        return NativeOpenSelection{std::filesystem::path(path),filter};
    }
}
void nativeOpenErrorMessage(GLFWwindow*,const std::string& message) {
    @autoreleasepool {
        NSAlert* alert=[[NSAlert alloc] init];
        alert.alertStyle=NSAlertStyleCritical;
        alert.messageText=@"Unable to open mesh";
        NSString* detail=[NSString stringWithUTF8String:message.c_str()];
        alert.informativeText=detail?detail:@"The selected file could not be read.";
        [alert addButtonWithTitle:@"OK"];
        [alert runModal];
    }
}
} // namespace mesher
