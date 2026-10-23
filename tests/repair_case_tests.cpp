#include "mesher/repair_case.hpp"
#include "mesher/repair_inspection.hpp"
#include "mesher/mesh_partitioning.hpp"
#include "mesher/sculpt_fixtures.hpp"
#include "mesher/repair_preview.hpp"
#include "mesher/repair_excision.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace mesher;
namespace {
void require(bool yes,const char* why) {
    if(!yes)throw std::runtime_error(why);
}
struct Fixture {
    std::shared_ptr<const RegionTopologyIndex> index;
    FlatMeshIndex ids;
};
Fixture fixture() {
    auto imported=partitionFlatMesh(generateTorusFixture(5000,1,5),64);
    return {RegionTopologyIndex::build(imported.revision),imported.flatIndex};
}
FaceId near(const Fixture& f,Vec3 p) {
    FaceId nearest=0;
    double best=1e100;
    for(const auto id:f.ids.faceIds) {
        const auto tri=f.index->face(id).vertices;
        const auto center=(f.index->vertex(tri[0]).position+
            f.index->vertex(tri[1]).position+
            f.index->vertex(tri[2]).position)/3;
        if(const auto d=length(center-p);d<best) {
            best=d;nearest=id;
        }
    }
    return nearest;
}
std::vector<RegionTopologyIndex::CollisionPair> contacts(const Fixture& f) {
    const auto a=near(f,{.12,0,0}),b=near(f,{-.12,0,0});
    return {{std::min(a,b),std::max(a,b),false}};
}
void assertSameGeometry(const RegionTopologyIndex& a,
                        const RegionTopologyIndex& b) {
    const auto aa=a.revision().identityWatermarks();
    const auto bb=b.revision().identityWatermarks();
    require(aa.nextVertex==bb.nextVertex&&aa.nextFace==bb.nextFace&&
            aa.nextOverflow==bb.nextOverflow,
            "R06 case lost immutable ID allocator watermarks");
    require(a.retiredFaceIds()==b.retiredFaceIds()&&
            a.retiredVertexIds()==b.retiredVertexIds(),
            "R06 case lost tombstone provenance");
    std::size_t faces=0,vertices=0;
    a.revision().forEachPartition([&](PartitionId id,const MeshPartition& p) {
        const auto rhs=b.revision().partition(id);
        require(rhs&&rhs->faces.size()==p.faces.size()&&
                rhs->vertices.size()==p.vertices.size(),
                "R06 case changed partition ownership or incidence counts");
        for(std::size_t i=0;i<p.vertices.size();++i) {
            const auto& v=p.vertices[i],w=rhs->vertices[i];
            require(v.id==w.id&&v.position.x==w.position.x&&
                    v.position.y==w.position.y&&v.position.z==w.position.z,
                    "R06 case modified a vertex stable ID or XYZ");
            ++vertices;
        }
        for(std::size_t i=0;i<p.faces.size();++i) {
            const auto& f=p.faces[i],g=rhs->faces[i];
            require(f.id==g.id&&f.vertices==g.vertices,
                    "R06 case changed a face stable ID or winding");
            ++faces;
        }
    });
    require(vertices>1000&&faces>1000,
            "R06 case test unexpectedly lost the large real fixture mesh");
}
void roundtrip(const Fixture& f) {
    const auto output=std::filesystem::current_path();
    const auto goodFile=output/"r06-sample-success.r06case";
    const auto badFile=output/"r06-sample-refused.r06case";
    const auto ps=contacts(f);
    RepairCase good;
    good.candidate.index=f.index;
    good.candidate.initialVertexCount=5000;
    good.contacts=ps;
    good.symmetricSource=false;
    good.expectedClosure=true;
    good.intendedSelection=RepairCaseIntent::PreferCaps;
    good.originalDiagnostic="original physical fixture expected complete";
    // Assert the actual current algorithm, not merely the saved booleans.
    const auto initial=replayPrivateAutomaticRepair(good);
    require(initial->privateAuto.has_value(),
            "R06 case positive torus must have a true private closure");
    std::filesystem::remove(goodFile);
    writeRepairCase(goodFile,good);
    const auto restored=readRepairCase(goodFile);
    assertSameGeometry(*f.index,*restored.candidate.index);
    require(restored.contacts.size()==1&&
            restored.contacts.front().first==ps.front().first&&
            restored.contacts.front().second==ps.front().second&&
            !restored.symmetricSource&&restored.expectedClosure&&
            restored.intendedSelection==RepairCaseIntent::PreferCaps&&
            restored.originalDiagnostic==good.originalDiagnostic,
            "R06 case lost source evidence, flags or original diagnostic");
    const auto repeated=replayPrivateAutomaticRepair(restored);
    require(repeated->privateAuto.has_value()&&
            repeated->privateAuto->greenFaces.size()==initial->privateAuto->greenFaces.size()&&
            repeated->privateAuto->postRetirementFaces==initial->privateAuto->postRetirementFaces,
            "R06 case accepted replay not deterministic");
    // M09: replay computes BOTH planning policies on the same frozen
    // candidate; the alternative is comparison-only and never builds M04.
    require(initial->classifierComparable &&
            initial->classifierChoices[0]!="UNAVAILABLE"&&
            initial->classifierChoices[1]!="UNAVAILABLE"&&
            initial->activeClassifier==AutoClosureClassifier::Area,
            "R06 frozen case did not retain both read-only M03 plans");
    const auto proximity=replayPrivateAutomaticRepair(
        restored,AutoClosureClassifier::Proximity);
    require(proximity->autoPlan&&
            proximity->activeClassifier==AutoClosureClassifier::Proximity&&
            proximity->classifierChoices==initial->classifierChoices&&
            proximity->classifiersAgree==initial->classifiersAgree,
            "M09 alternative M03 replay must compare the identical source/cut");
    require(proximity->autoDiagnostic.find("stale or forged M03 closure choice")==
                std::string::npos,
            "M09 independently regenerated M04C must use selected proximity policy");
    require(f.index->retiredFaceCount()==0,
            "R06 replay mutated source tombstones");
    // The deliberately invalid source symmetry flag MUST refuse rather
    // than return a geometrically unsymmetric closure as symmetric.
    RepairCase refused=good;
    refused.symmetricSource=true;
    refused.expectedClosure=false;
    refused.intendedSelection=RepairCaseIntent::Unspecified;
    refused.originalDiagnostic="original mirror topology incompatible";
    const auto negative=replayPrivateAutomaticRepair(refused);
    require(!negative->privateAuto.has_value()&&
            !negative->autoDiagnostic.empty()&&negative->privateCut,
            "R06 replay did not refuse incompatible intrinsic symmetry");
    std::filesystem::remove(badFile);
    writeRepairCase(badFile,refused);
    const auto refusedRead=readRepairCase(badFile);
    const auto repeatedRefusal=replayPrivateAutomaticRepair(refusedRead);
    require(!repeatedRefusal->privateAuto.has_value()&&
            repeatedRefusal->autoDiagnostic==negative->autoDiagnostic,
            "R06 saved refusal must reproduce its exact diagnostic");
    // A semantically bad yet geometrically CLOSED case is intentionally
    // capturable. The headless CLI must report CHOICE_MISMATCH with exit 3
    // even while expected CLOSED still matches current CLOSED.
    RepairCase wrongChoice=good;
    wrongChoice.intendedSelection=RepairCaseIntent::PreferZipper;
    const auto wrongFile=output/"r06-sample-prefer-zipper.r06case";
    std::filesystem::remove(wrongFile);
    writeRepairCase(wrongFile,wrongChoice);
    const auto wrongRead=readRepairCase(wrongFile);
    require(wrongRead.expectedClosure&&
            wrongRead.intendedSelection==RepairCaseIntent::PreferZipper&&
            replayPrivateAutomaticRepair(wrongRead)->privateAuto,
            "R06 case must save a COMPLETE but semantically misclassified result");
    RepairCase invalidTag=good;
    invalidTag.intendedSelection=static_cast<RepairCaseIntent>(9);
    bool invalidIntentRefused=false;
    try{writeRepairCase(output/"r06-invalid-intent.r06case",invalidTag);}
    catch(const std::invalid_argument&){invalidIntentRefused=true;}
    require(invalidIntentRefused,
            "R06 writer must reject invalid human classifier annotation");
    require(!std::filesystem::exists(output/"r06-invalid-intent.r06case"),
            "R06 invalid-tag write must not create file");
    auto corrupt=output/"r06-corrupt.r06case";
    std::filesystem::copy_file(goodFile,corrupt,
        std::filesystem::copy_options::overwrite_existing);
    {
        std::fstream file(corrupt,std::ios::binary|std::ios::in|std::ios::out);
        require(static_cast<bool>(file),"Cannot open R06 case corruption fixture");
        file.seekp(25);
        file.put(static_cast<char>(0x7f));
    }
    bool rejected=false;
    try{(void)readRepairCase(corrupt);}
    catch(const std::exception&){rejected=true;}
    require(rejected,"R06 case corruption must fail checksum");
    std::filesystem::remove(corrupt);
    corrupt=output/"r06-truncated.r06case";
    {
        std::ofstream out(corrupt,std::ios::binary|std::ios::trunc);
        out<<"MSHRCAS1";
    }
    rejected=false;
    try{(void)readRepairCase(corrupt);}
    catch(const std::exception&){rejected=true;}
    require(rejected,"R06 case truncated header must be refused");
    std::filesystem::remove(corrupt);
    rejected=false;
    try{writeRepairCase(goodFile,good);}
    catch(const std::exception&){rejected=true;}
    require(rejected,"R06 case capture must never silently overwrite a previous case");
    // Physical capture should never overwrite an earlier independent
    // refusal when the operator saves more than one case in a session.
    const auto uniqueDirectory=output/"r06-case-path-test";
    const auto first=uniqueRepairCasePath(uniqueDirectory);
    require(!std::filesystem::exists(first),"R06 unique first path already exists");
    {
        std::ofstream out(first,std::ios::binary);
        out<<"existing physical capture";
    }
    const auto second=uniqueRepairCasePath(uniqueDirectory);
    const auto wrongClassification=uniqueRepairCasePath(
        uniqueDirectory,"classification-zipper");
    require(wrongClassification.filename().string().find("classification-zipper-")==0,
            "R06 desired-zipper captures need distinguishable unique filenames");
    require(second!=first&&!std::filesystem::exists(second),
            "R06 repeated capture must choose a distinct unused filename");
    std::filesystem::remove(first);
    std::filesystem::remove(uniqueDirectory);
    const auto proposed=previewApproximateExcision(*f.index,ps);
    require(proposed.complete,"R06 case tombstone setup failed R02");
    const auto cut=applyPrivateExcision(f.index,proposed);
    RepairCase withTombstones=good;
    withTombstones.candidate.index=cut.index;
    // The contact pair is not part of the R03 cut anymore, so swap to
    // surviving faces just to validate the archive, not run R02 on it.
    FaceId a=0,b=0;
    cut.index->revision().forEachPartition([&](PartitionId,const MeshPartition& p) {
        for(const auto& f:p.faces) {
            if(!a)a=f.id;
            else if(!b&&f.id!=a)b=f.id;
        }
    });
    require(a&&b,"R06 tombstone fixture lacks surviving faces");
    withTombstones.contacts={{std::min(a,b),std::max(a,b),false}};
    auto tombstoneFile=output/"r06-tombstone-test.r06case";
    std::filesystem::remove(tombstoneFile);
    writeRepairCase(tombstoneFile,withTombstones);
    const auto loaded=readRepairCase(tombstoneFile);
    assertSameGeometry(*cut.index,*loaded.candidate.index);
    require(loaded.candidate.index->retiredFaceCount()>0,
            "R06 case must preserve real retired stable identities");
    std::filesystem::remove(tombstoneFile);
    std::cout<<"[r06-case] V2 complete root, desired classifier, trusted contacts, positive/refusal,"
                " tombstones, watermarks, corruption and overwrite guards PASS\n";
}
}
int main() {
    try{roundtrip(fixture());return 0;}
    catch(const std::exception& error) {
        std::cerr<<"R06 repair-case regression FAILED: "<<error.what()<<"\n";
        return 1;
    }
}
