#include "mesher/document_filename_state.hpp"
#include "mesher/mesh_export_formats.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace mesher {
namespace {
namespace fs=std::filesystem;

fs::path acceptedFile(const fs::path& path) {
    if(path.empty()||path.filename().empty()||
       path.filename()=="."||path.filename()=="..")
        throw std::invalid_argument("Document filename is empty");
    return fs::absolute(path).lexically_normal();
}
bool isNativeExtension(const fs::path& path) {
    auto extension=path.extension().string();
    std::transform(extension.begin(),extension.end(),extension.begin(),
        [](unsigned char c){return static_cast<char>(std::tolower(c));});
    return extension==".mshr";
}
ExportFileFamily family(DocumentSourceFormat kind) {
    switch(kind) {
    case DocumentSourceFormat::WavefrontObj:return ExportFileFamily::Obj;
    case DocumentSourceFormat::StanfordPly:return ExportFileFamily::Ply;
    case DocumentSourceFormat::Stl:return ExportFileFamily::Stl;
    case DocumentSourceFormat::NativeMshr:break;
    }
    throw std::invalid_argument("Native checkpoint has no import encoding");
}
PreferredExportFormat canonicalImportFormat(DocumentSourceFormat kind) {
    switch(kind) {
    case DocumentSourceFormat::WavefrontObj:return {ExportFileFamily::Obj,1};
    case DocumentSourceFormat::StanfordPly:return {ExportFileFamily::Ply,6};
    case DocumentSourceFormat::Stl:return {ExportFileFamily::Stl,1};
    case DocumentSourceFormat::NativeMshr:break;
    }
    throw std::invalid_argument("Native checkpoint has no export codec");
}
void validateExport(PreferredExportFormat preferred) {
    const auto count=preferred.family==ExportFileFamily::Obj?4u:
                     preferred.family==ExportFileFamily::Ply?12u:
                     preferred.family==ExportFileFamily::Stl?5u:0u;
    if(preferred.variant>=count)
        throw std::invalid_argument("Unknown export encoding variant");
}
} // namespace

void DocumentFilenameState::opened(
    const fs::path& path,DocumentSourceFormat kind,
    std::optional<PreferredExportFormat> detectedEncoding) {
    // Validate BEFORE changing a prior document, including provenance.
    const fs::path absolute=acceptedFile(path);
    if(kind==DocumentSourceFormat::NativeMshr) {
        if(detectedEncoding)
            throw std::invalid_argument(
                "A native checkpoint must not change export codec preference");
    }else {
        if(detectedEncoding) {
            validateExport(*detectedEncoding);
            if(detectedEncoding->family!=family(kind))
                throw std::invalid_argument(
                    "Detected import encoding conflicts with format family");
        }else detectedEncoding=canonicalImportFormat(kind);
    }
    ++generation_;
    hint_=absolute;
    importedFormat_=kind;
    hintExtension_=kind==DocumentSourceFormat::WavefrontObj?"obj":
                   kind==DocumentSourceFormat::StanfordPly?"ply":
                   kind==DocumentSourceFormat::Stl?"stl":"mshr";
    if(kind==DocumentSourceFormat::NativeMshr&&isNativeExtension(absolute)) {
        nativeSave_=absolute;
        provenance_=FilenameProvenance::OpenedNative;
    }else {
        nativeSave_.reset();
        provenance_=FilenameProvenance::OpenedForeign;
    }
    if(detectedEncoding)exportFormat_=*detectedEncoding;
}

void DocumentFilenameState::newDocument() {
    ++generation_;
    hint_.reset();
    hintExtension_.clear();
    nativeSave_.reset();
    provenance_.reset();
    importedFormat_.reset();
    // Export format is session-wide, independent of a document.
}

bool DocumentFilenameState::completedExport(
    const fs::path& destination,PreferredExportFormat encoding,
    std::uint64_t acceptedGeneration) {
    if(acceptedGeneration!=generation_)return false;
    validateExport(encoding);
    const auto absolute=acceptedFile(destination);
    exportFormat_=encoding;
    if(!hint_) {
        hint_=absolute;
        hintExtension_=encoding.family==ExportFileFamily::Obj?"obj":
                       encoding.family==ExportFileFamily::Ply?"ply":"stl";
        provenance_=FilenameProvenance::SeededByExport;
        importedFormat_.reset();
    }
    return true;
}

bool DocumentFilenameState::completedNativeSave(
    const fs::path& destination,std::uint64_t acceptedGeneration) {
    if(acceptedGeneration!=generation_)return false;
    const auto absolute=acceptedFile(destination);
    if(!isNativeExtension(absolute))
        throw std::invalid_argument(
            "A native checkpoint Save must have a .mshr extension");
    hint_=absolute;
    hintExtension_="mshr";
    nativeSave_=absolute;
    provenance_=FilenameProvenance::SavedNative;
    importedFormat_=DocumentSourceFormat::NativeMshr;
    // Saving the native document never changes Ctrl+E selection.
    return true;
}

SaveAsSuggestion DocumentFilenameState::suggestSaveAs(
    std::string_view randomStem) const {
    if(!hint_) {
        if(randomStem.empty())
            throw std::invalid_argument("New document requires a filename suggestion");
        // The random generator owns portable basename validation.
        auto name=fs::path(std::string(randomStem));
        if(name.has_parent_path()||name.filename().empty()||
           name.filename()=="."||name.filename()=="..")
            throw std::invalid_argument("Invalid generated filename basename");
        name+=".mshr";
        return {std::move(name),DocumentSourceFormat::NativeMshr};
    }
    if(provenance_==FilenameProvenance::SeededByExport) {
        auto filename=*hint_;
        filename.replace_extension(".mshr");
        return {std::move(filename),DocumentSourceFormat::NativeMshr};
    }
    if(importedFormat_&&*importedFormat_!=DocumentSourceFormat::NativeMshr)
        return {*hint_,*importedFormat_};
    return {*hint_,DocumentSourceFormat::NativeMshr};
}

std::filesystem::path DocumentFilenameState::suggestExportFilename(
    std::string_view randomStem,std::string_view selectedExtension) const {
    if(!hint_&&randomStem.empty())
        throw std::invalid_argument("New document needs an export filename stem");
    const fs::path filename=hint_?hint_->filename():fs::path(randomStem);
    if(filename.empty()||filename.has_parent_path())
        throw std::invalid_argument("Invalid export filename suggestion");
    return changeExportFilenameExtension(
        filename,hintExtension_,selectedExtension);
}

} // namespace mesher
