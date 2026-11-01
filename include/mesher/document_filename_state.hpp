#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace mesher {

enum class DocumentSourceFormat {NativeMshr,WavefrontObj,StanfordPly,Stl};
enum class ExportFileFamily {Obj,Ply,Stl};

// Variant is the zero-based format choice *within* that family. Defaults
// match OBJ inline RGB, PLY binary LE editable RGBA, STL binary uncoloured.
// Full Ctrl+E UX07E later maps this to MeshExportPreset explicitly.
struct PreferredExportFormat {
    ExportFileFamily family=ExportFileFamily::Ply;
    unsigned variant=6;
    bool operator==(const PreferredExportFormat&) const=default;
};

enum class FilenameProvenance {OpenedNative,OpenedForeign,SavedNative,SeededByExport};
struct SaveAsSuggestion {
    std::filesystem::path filename;
    DocumentSourceFormat initialFormat=DocumentSourceFormat::NativeMshr;
};

// Pure UI policy. No writes, history edits, worker calls or filesystem
// existence tests: only identity tracking and filename selection.
// Completion callbacks must carry the document generation captured when the
// asynchronous request was accepted, preventing a stale export/save from
// changing the filename of a document opened or created in the meantime.
class DocumentFilenameState {
public:
    std::uint64_t generation() const noexcept {return generation_;}
    const std::optional<std::filesystem::path>& filenameHint() const noexcept {
        return hint_;
    }
    const std::optional<std::filesystem::path>& nativeSaveDestination() const noexcept {
        return nativeSave_;
    }
    bool canDirectSave() const noexcept {return nativeSave_.has_value();}
    const std::optional<FilenameProvenance>& filenameProvenance() const noexcept {
        return provenance_;
    }
    PreferredExportFormat preferredExport() const noexcept {return exportFormat_;}

    void opened(const std::filesystem::path& path,DocumentSourceFormat kind,
                std::optional<PreferredExportFormat> detectedEncoding=std::nullopt);
    void newDocument();
    // Return false for a stale completion (no state is touched).
    bool completedExport(const std::filesystem::path& destination,
                         PreferredExportFormat encoding,
                         std::uint64_t acceptedGeneration);
    bool completedNativeSave(const std::filesystem::path& destination,
                             std::uint64_t acceptedGeneration);

    // The caller supplies the randomly generated basename only if no
    // remembered filename is available; it is otherwise ignored.
    SaveAsSuggestion suggestSaveAs(std::string_view randomStem) const;
    // The hint's original file type is independent of the last export codec.
    std::filesystem::path suggestExportFilename(
        std::string_view randomStem,std::string_view selectedExtension) const;

private:
    std::uint64_t generation_=1;
    std::optional<std::filesystem::path> hint_;
    std::optional<std::filesystem::path> nativeSave_;
    std::optional<FilenameProvenance> provenance_;
    std::optional<DocumentSourceFormat> importedFormat_;
    std::string hintExtension_;
    PreferredExportFormat exportFormat_{};
};
} // namespace mesher
