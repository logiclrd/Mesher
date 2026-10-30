#include "mesher/document_filename_state.hpp"
#include "mesher/random_filename.hpp"

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>

namespace {
namespace fs=std::filesystem;
using namespace mesher;

void require(bool condition,const char* msg) {
    if(!condition)throw std::runtime_error(msg);
}
void testFilenamePolicy() {
    DocumentFilenameState state;
    require(!state.filenameHint()&&!state.nativeSaveDestination()&&
            !state.canDirectSave(),"Fresh document falsely has a Save path");
    require(state.preferredExport()==PreferredExportFormat{
                ExportFileFamily::Ply,6},
            "New editor has wrong PLY default");
    const auto initial=state.suggestSaveAs("exquisite goldfish");
    require(initial.filename==fs::path("exquisite goldfish.mshr")&&
            initial.initialFormat==DocumentSourceFormat::NativeMshr,
            "Fresh Save As did not default to generated .mshr filename");

    const auto firstGeneration=state.generation();
    const auto exportName=fs::absolute("bob.ply");
    require(state.completedExport(exportName,{ExportFileFamily::Ply,7},
                                  firstGeneration),
            "First export completion refused");
    require(!state.canDirectSave()&&state.filenameHint()==exportName&&
            state.filenameProvenance()==FilenameProvenance::SeededByExport,
            "Export-seeded hint incorrectly established native Save");
    const auto suggested=state.suggestSaveAs("");
    auto expected=exportName;
    expected.replace_extension(".mshr");
    require(suggested.filename==expected&&
            suggested.initialFormat==DocumentSourceFormat::NativeMshr,
            "Export-first Ctrl+S must preload bob.mshr");
    require(state.preferredExport()==PreferredExportFormat{
                ExportFileFamily::Ply,7},
            "Export did not remember exact PLY codec");

    require(state.completedExport(fs::absolute("other.stl"),
        {ExportFileFamily::Stl,3},firstGeneration),
        "Second export completion refused");
    require(state.filenameHint()==exportName&&
            state.preferredExport()==PreferredExportFormat{
                ExportFileFamily::Stl,3},
            "Later export should change format but preserve existing filename hint");

    require(state.completedNativeSave(expected,firstGeneration),
            "Native checkpoint Save completion refused");
    require(state.canDirectSave()&&state.nativeSaveDestination()==expected&&
            state.filenameHint()==expected,
            "Native save did not establish direct Save identity");
    require(state.preferredExport()==PreferredExportFormat{
                ExportFileFamily::Stl,3},
            "Saving .mshr unexpectedly changed Ctrl+E preference");
    state.newDocument();
    require(!state.filenameHint()&&!state.canDirectSave()&&
            state.preferredExport()==PreferredExportFormat{
                ExportFileFamily::Stl,3},
            "New must clear document identity but preserve export preference");
    require(!state.completedExport(fs::absolute("stale.obj"),
        {ExportFileFamily::Obj,1},firstGeneration)&&
            !state.completedNativeSave(expected,firstGeneration),
            "Stale writer completion modified newer document");
    require(!state.filenameHint()&&!state.canDirectSave()&&
            state.preferredExport()==PreferredExportFormat{
                ExportFileFamily::Stl,3},
            "Ignoring stale output mutated document state");
}
void testOpeningAndErrors() {
    DocumentFilenameState state;
    state.opened("duck.obj",DocumentSourceFormat::WavefrontObj,
                 PreferredExportFormat{ExportFileFamily::Obj,3});
    const auto remembered=fs::absolute("duck.obj");
    require(!state.canDirectSave()&&state.filenameHint()==remembered,
            "Imported OBJ must remain a filename hint, not native Save");
    auto suggestion=state.suggestSaveAs("unused");
    require(suggestion.filename==remembered&&
            suggestion.initialFormat==DocumentSourceFormat::WavefrontObj,
            "Import Save As must preload detected source format");
    require(state.preferredExport()==PreferredExportFormat{
                ExportFileFamily::Obj,3},
            "Import did not preserve recognized OBJ codec");

    // Opening .mshr changes document identity, not the session-wide export
    // codec remembered from foreign-format imports.
    state.opened("revision.mshr",DocumentSourceFormat::NativeMshr);
    require(state.canDirectSave()&&state.nativeSaveDestination()==
            fs::absolute("revision.mshr")&&
            state.preferredExport()==PreferredExportFormat{
                ExportFileFamily::Obj,3},
            "Opening native checkpoint reset Ctrl+E or lost Save eligibility");
    suggestion=state.suggestSaveAs("unused");
    require(suggestion.initialFormat==DocumentSourceFormat::NativeMshr,
            "Opening checkpoint did not select native Save As");

    // Content may identify a native .mshr file that has a misleading
    // extension. Do not write checkpoint bytes directly to the foreign path.
    state.opened("weird.obj",DocumentSourceFormat::NativeMshr);
    require(!state.canDirectSave()&&state.filenameHint()==
            fs::absolute("weird.obj"),
            "Misleading native path unexpectedly permitted direct Save");
    const auto before=state.filenameHint();
    const auto generation=state.generation();
    bool refused=false;
    try {
        state.opened("invalid.ply",DocumentSourceFormat::StanfordPly,
                     PreferredExportFormat{ExportFileFamily::Obj,1});
    }catch(const std::invalid_argument&) {refused=true;}
    require(refused&&state.generation()==generation&&
            state.filenameHint()==before,
            "Invalid import codec partially changed document identity");
    refused=false;
    try {
        (void)state.completedNativeSave("broken.obj",generation);
    }catch(const std::invalid_argument&) {refused=true;}
    require(refused&&!state.canDirectSave()&&state.filenameHint()==before,
            "Invalid native Save path partially changed state");
    refused=false;
    try {
        (void)state.completedExport("bogus.ply",
                                   {ExportFileFamily::Ply,12},generation);
    }catch(const std::invalid_argument&) {refused=true;}
    require(refused&&state.filenameHint()==before,
            "Invalid export encoding partially changed filename hint");
}

void testEmbeddedWords() {
    const auto counts=embeddedFilenameWordCounts();
    require(counts.first>100&&counts.second>100,
            "Curated embedded vocabulary is unexpectedly empty");
    const auto oldCwd=fs::current_path();
    const auto temp=fs::temp_directory_path()/
        ("mesher-ux09-names-"+std::to_string(std::random_device{}()));
    fs::create_directory(temp);
    try {
        fs::current_path(temp);
        // This deliberately runs with no words/ directory and no adjoining
        // .txt files: executable must contain all vocabulary data.
        std::mt19937_64 a(9876),b(9876);
        for(unsigned index=0;index<500;++index) {
            const auto generated=randomFilenameStem(a);
            require(generated==randomFilenameStem(b),
                    "Seeded filename generation is non-deterministic");
            require(generated.size()>2&&generated.size()<=97,
                    "Generated basename length is outside portable limits");
            const auto space=generated.find(' ');
            require(space!=std::string::npos&&space>0&&
                    space+1<generated.size()&&
                    generated.find(' ',space+1)==std::string::npos,
                    "Filename must have exactly one adjective and one noun");
            for(const unsigned char c:generated)
                require((c>='a'&&c<='z')||c=='-'||c==' ',
                        "Generated filename contains a non-portable character");
            const auto suggestion=DocumentFilenameState{}.suggestSaveAs(generated);
            require(suggestion.filename.extension()==".mshr",
                    "Generated basename must be compatible with native Save As");
        }
        require(!randomFilenameStem().empty(),
                "System-seeded filename generator failed without word files");
    }catch(...) {
        fs::current_path(oldCwd);
        std::error_code ignored;fs::remove_all(temp,ignored);
        throw;
    }
    fs::current_path(oldCwd);
    fs::remove_all(temp);
}
} // namespace

int main() {
    try {
        testFilenamePolicy();
        testOpeningAndErrors();
        testEmbeddedWords();
        std::cout<<"UX09A filename provenance and embedded word tests passed\n";
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"UX09A test failed: "<<e.what()<<'\n';
        return 1;
    }
}
