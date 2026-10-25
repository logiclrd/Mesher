#include "mesher/repair_inspection.hpp"
#include "mesher/versioned_sculpt.hpp"
#include "mesher/repair_case.hpp"
#include "mesher/repair_step_trace.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace mesher {
namespace {
RegionTopologyIndex::Edge canonical(VertexId a,VertexId b) {
    if(a>b)std::swap(a,b);
    return {a,b};
}
PartitionRenderVertex marker(Vec3 p,float material) {
    return {float(p.x),float(p.y),float(p.z),0,0,1,material};
}
void triangle(std::vector<PartitionRenderVertex>& out,
              Vec3 a,Vec3 b,Vec3 c,float material) {
    out.push_back(marker(a,material));
    out.push_back(marker(b,material));
    out.push_back(marker(c,material));
}
void dot(std::vector<PartitionRenderVertex>& out,Vec3 c,double r,float material=2.0f) {
    const std::array<Vec3,6> points{{
        c+Vec3{r,0,0},c+Vec3{-r,0,0},
        c+Vec3{0,r,0},c+Vec3{0,-r,0},
        c+Vec3{0,0,r},c+Vec3{0,0,-r}
    }};
    for(unsigned i=0;i<4;++i) {
        const unsigned j=(i+1)%4;
        const auto a=points[i==0?0:i==1?2:i==2?1:3];
        const auto b=points[j==0?0:j==1?2:j==2?1:3];
        triangle(out,points[4],a,b,material);
        triangle(out,points[5],b,a,material);
    }
}
void rod(std::vector<PartitionRenderVertex>& out,Vec3 a,Vec3 b,double radius,float material=3.0f) {
    const auto delta=b-a;
    if(length(delta)<=1e-15)return;
    const auto axis=normalized(delta);
    const auto ref=std::abs(axis.z)<0.9?Vec3{0,0,1}:Vec3{0,1,0};
    const auto u=normalized(cross(axis,ref))*radius;
    const auto v=normalized(cross(axis,u))*radius;
    const std::array<Vec3,4> ring{{u,v,u*-1.0,v*-1.0}};
    for(unsigned i=0;i<4;++i) {
        const auto j=(i+1)%4;
        triangle(out,a+ring[i],a+ring[j],b+ring[i],material);
        triangle(out,b+ring[i],a+ring[j],b+ring[j],material);
    }
}
}


std::string classifierChoiceName(const AutoClosurePlan& plan) {
    if(!plan.ready())return "UNAVAILABLE";
    const auto caps=plan.cappedContours.size();
    const auto zippers=plan.zipperPairs.size();
    if(caps&&zippers)return "MIXED";
    if(zippers)return "ZIPPER";
    if(caps)return "CAPS";
    return "NO RIMS";
}
bool sameClassifierChoice(const AutoClosurePlan& lhs,
                          const AutoClosurePlan& rhs) {
    if(!lhs.ready()||!rhs.ready()||
       lhs.cappedContours!=rhs.cappedContours||
       lhs.zipperPairs.size()!=rhs.zipperPairs.size())
        return false;
    const auto normalized=[](const AutoClosurePlan& p) {
        std::vector<std::pair<std::size_t,std::size_t>> pairs;
        pairs.reserve(p.zipperPairs.size());
        for(const auto& link:p.zipperPairs)
            pairs.emplace_back(std::min(link.first,link.second),
                               std::max(link.first,link.second));
        std::sort(pairs.begin(),pairs.end());
        return pairs;
    };
    return normalized(lhs)==normalized(rhs);
}
void compareClassifierPlans(RepairInspectionSnapshot& snapshot,
                            AutoClosureClassifier active) {
    snapshot.activeClassifier=active;
    if(!snapshot.privateCut||!snapshot.autoPlan)return;
    const auto other=active==AutoClosureClassifier::Area?
        AutoClosureClassifier::Proximity:AutoClosureClassifier::Area;
    const auto otherPlan=planPrivateAutomaticClosure(
        *snapshot.candidate,*snapshot.privateCut,snapshot.excision,
        snapshot.contacts,AutoClosurePlanSettings{.classifier=other});
    const auto& area=active==AutoClosureClassifier::Area?
        *snapshot.autoPlan:otherPlan;
    const auto& proximity=active==AutoClosureClassifier::Proximity?
        *snapshot.autoPlan:otherPlan;
    snapshot.classifierChoices={{classifierChoiceName(area),
                                 classifierChoiceName(proximity)}};
    snapshot.classifierComparable=area.ready()&&proximity.ready();
    snapshot.classifiersAgree=snapshot.classifierComparable&&
        sameClassifierChoice(area,proximity);
}
void runAutomaticClosure(RepairInspectionSnapshot& snapshot,
                          bool symmetricSource,AutoClosureClassifier classifier,
                          bool warnQuality,
                          const std::shared_ptr<RepairStepTrace>& trace={},
                          bool scaleAware=false,bool optimizeFront=false,
                          bool optimizeCurvatureFront=false,bool guideCurvaturePoisson=false) {
    // The opt-in producer may be paused on ANY initial cap/zipper operation.
    // Completion, refusal or intentional cancellation must release consumers,
    // without changing the ordinary synchronous replay or inspector path.
    struct FinishOnExit {
        std::shared_ptr<RepairStepTrace> value;
        ~FinishOnExit() {if(value)value->finish();}
    } finishOnExit{trace};
    snapshot.warnQualityEnabled=warnQuality;
    snapshot.activeClassifier=classifier;
    auto* result=&snapshot;
        if(!result->privateCut) {
            result->autoDiagnostic="R06 requires successful complete R03 excision: "+
                result->excisionDiagnostic;
            if(trace)trace->recordTerminal({
                MixedClosureTerminalOutcome::Refused,"excision",
                result->autoDiagnostic,false});
        }else {
            try {
                result->autoPlan=planPrivateAutomaticClosure(
                    *result->candidate,*result->privateCut,
                    result->excision,result->contacts,
                    AutoClosurePlanSettings{.classifier=classifier});
                // The alternate M03 classifier is never allowed to affect
                // the selected M04 plan. Show both before even attempting
                // geometry, so refusals still display the disagreement.
                try {
                    compareClassifierPlans(*result,classifier);
                }catch(const std::exception& e) {
                    std::cerr<<"[repair-r06] classifier comparison unavailable: "
                             <<e.what()<<"\n";
                }
                if(trace)trace->setClassifier({
                    result->classifierChoices,result->classifierComparable,
                    result->classifiersAgree,
                    classifier==AutoClosureClassifier::Proximity?1:0});
                if(!result->autoPlan->ready()) {
                    result->autoDiagnostic="R06 automatic closure plan refused: "+
                        result->autoPlan->diagnostic;
                    if(trace)trace->recordTerminal({
                        MixedClosureTerminalOutcome::Refused,"planning",
                        result->autoDiagnostic,false});
                }else {
                    CertifiedPrivateAutoLimits policy;
                    policy.closure.requireReflectedGeometry=symmetricSource;
                    policy.closure.scaleAwareRefinement=scaleAware;
                    policy.closure.staging.optimizeSpatialFront=optimizeFront;
                    policy.closure.staging.optimizeCurvatureFront=optimizeCurvatureFront;
                    policy.closure.staging.guideCurvaturePoisson=guideCurvaturePoisson;
                    policy.planner.classifier=classifier;
                    if(trace)trace->attach(policy.closure,trace);
                    result->privateAuto=applyCertifiedPrivateAutomaticClosure(
                        result->candidate,*result->privateCut,
                        result->excision,*result->autoPlan,result->contacts,
                        policy);
                    if(trace)trace->recordTerminal({
                        MixedClosureTerminalOutcome::Completed,"certified_auto",
                        "R06 M04C enclosure and cavity policy passed: PRIVATE ONLY",
                        false});
                }
            }catch(const RepairStepCancelled&) {
                // This is a user request to stop, never a geometry refusal
                // or permission to launch another warning-preview attempt.
                throw;
            }catch(const RepairStepObserverFailure&) {
                // Failed visualization must not be presented as geometric
                // closure refusal, nor provoke a warning-only rerun.
                throw;
            }catch(const std::exception& e) {
                if(trace)trace->recordTerminal({
                    MixedClosureTerminalOutcome::Refused,"certified_auto",
                    e.what(),false});
                result->autoDiagnostic=std::string(
                    "R06 private automatic closure refused: ")+e.what();
            }
        }
    // Never let a warning-only preview become certified automatic closure.
    // A structurally invalid result still refuses in the M04 staging audit.
    if(warnQuality&&!result->privateAuto&&result->privateCut&&
       result->autoPlan&&result->autoPlan->ready()) {
        const auto& error=result->autoDiagnostic;
        const bool qualityFailure=
            error.find("skinny")!=std::string::npos||
            error.find("quality")!=std::string::npos||
            error.find("refinement")!=std::string::npos||
            error.find("split orbit exceeds finite budget")!=std::string::npos||
            error.find("edge-length deficit")!=std::string::npos||
            error.find("boundary density")!=std::string::npos;
        if(qualityFailure) {
            try {
                MixedPrivateClosureLimits limits;
                limits.requireReflectedGeometry=symmetricSource;
                limits.inspectionWarnQuality=true;
                limits.scaleAwareRefinement=scaleAware;
                limits.staging.optimizeSpatialFront=optimizeFront;
                limits.staging.optimizeCurvatureFront=optimizeCurvatureFront;
                limits.staging.guideCurvaturePoisson=guideCurvaturePoisson;
                if(trace)trace->attach(limits,trace,true);
                auto preview=applyPrivateMixedClosure(
                    *result->privateCut,result->excision,*result->autoPlan,limits);
                if(preview.qualityWarningFaces.empty()&&
                   preview.qualityWarningReason.empty())
                    result->warningPreviewDiagnostic=
                        "Warning-only staging produced no quality violation";
                else result->warningPreview=std::move(preview);
            }catch(const RepairStepCancelled&) {
                throw;
            }catch(const RepairStepObserverFailure&) {
                throw;
            }catch(const std::exception& e) {
                result->warningPreviewDiagnostic=std::string(
                    "Untrusted warning preview unavailable (other safety check): ")+e.what();
            }
        }
    }
}

void buildSeamAuditIfRequested(RepairInspectionSnapshot& snapshot,
                               bool requested) {
    if(!requested)return;
    const auto* good=snapshot.privateAuto?
        snapshot.privateAuto->index.get():nullptr;
    const auto* preview=snapshot.warningPreview?
        snapshot.warningPreview->index.get():nullptr;
    const auto* mesh=good?good:preview;
    if(!mesh) {
        snapshot.seamAuditDiagnostic=
            "No closed-index geometry available; R03 cut cannot have a cap/survivor seam";
        return;
    }
    std::vector<FaceId> ids;
    if(good) {
        ids=snapshot.privateAuto->greenFaces;
        const auto& blue=snapshot.privateAuto->blueFaces;
        ids.insert(ids.end(),blue.begin(),blue.end());
    }else {
        ids=snapshot.warningPreview->greenFaces;
        const auto& blue=snapshot.warningPreview->blueFaces;
        ids.insert(ids.end(),blue.begin(),blue.end());
    }
    try {
        snapshot.seamAudit=auditPrivateRepairSeam(*mesh,ids);
        if(snapshot.privateCut&&snapshot.privateCut->index) {
            // Certified AUTO exposes only a count of added vertices;
            // provenance also resolves source/new IDs from both indexes.
            const auto added=good?std::vector<VertexId>{}:
                snapshot.warningPreview->insertedVertices;
            const auto& ancestry=good?
                snapshot.privateAuto->splitEvents:
                snapshot.warningPreview->splitEvents;
            snapshot.seamProvenance=explainRepairSeamWitnesses(
                *snapshot.privateCut->index,*mesh,*snapshot.seamAudit,
                added,ancestry);
        }
    }catch(const std::exception& e) {
        snapshot.seamAuditDiagnostic=e.what();
    }
}

std::string RepairInspectionSession::inspect(const ProposedSculptStep& proposed) {
    if(frozen())return "R01 diagnostic view is frozen; relaunch to sculpt again";
    if(!proposed.candidate||!proposed.before)
        return "R01 diagnostic requires valid immutable before/candidate roots";
    if(proposed.candidate==proposed.before)return {};
    auto result=std::make_shared<RepairInspectionSnapshot>();
    result->candidate=proposed.candidate;
    const std::vector<VertexId> vertices(
        proposed.changedVertices.begin(),proposed.changedVertices.end());
    const std::vector<FaceId> faces(
        proposed.changedFaces.begin(),proposed.changedFaces.end());
    // Finite speculative work, NEVER a partial collision list.
    RegionTopologyIndex::CollisionQueryLimits limits;
    limits.maxSeedFaces=16384;
    // Each changed face performs its OWN traversal of the persistent
    // partition directory. A normal 5000-vertex symmetric sphere stamp
    // can seed ~900 triangles, exceeding 131072 *directory* visits
    // even when only a few thousand pairs reach narrow phase.
    // Keep a hard limit, but size it for the actual two-level indexed
    // query rather than confusing directory work with contact volume.
    // R01 is an explicitly opt-in debug inspector, not a real-time
    // collision solver. Candidate and baseline both use these limits.
    limits.maxBoxTests=4194304;
    limits.maxPartitionTests=1048576;
    limits.maxCandidateTriangles=524288;
    limits.maxNarrowPhaseTests=262144;
    limits.maxReportedContacts=512; // diagnostic overlay memory is deliberately finite
    if(vertices.size()>65536||faces.size()>65536)
        result->diagnostic="R01 changed-ID input budget exhausted";
    else {
        try {
            result->contacts=proposed.candidate->triangleIntersections(
                vertices,faces,&result->work,limits);
        }catch(const std::length_error& e) {
            result->diagnostic=std::string("R01 collision query budget exhausted: ")+e.what();
        }
    }
    // R01 should freeze on NEW changed-neighborhood evidence, not on a
    // historically imperfect but still editable mesh. Compare the same
    // supported finite query on the previous immutable root. Restrict
    // sources to IDs that actually exist there: new refinement vertices
    // and faces have no historical counterpart. This is face-pair
    // provenance only; R07 will introduce geometric worsening metrics.
    if(!result->contacts.empty()&&result->diagnostic.empty()) {
        std::vector<VertexId> oldVertices;
        std::vector<FaceId> oldFaces;
        oldVertices.reserve(vertices.size());
        oldFaces.reserve(faces.size());
        for(const auto id:vertices)
            if(proposed.before->maybeVertex(id))oldVertices.push_back(id);
        for(const auto id:faces)
            if(proposed.before->maybeFace(id))oldFaces.push_back(id);
        try {
            const auto previous=proposed.before->triangleIntersections(
                oldVertices,oldFaces,&result->baselineWork,limits);
            std::set<std::pair<FaceId,FaceId>> existing;
            for(const auto& p:previous)
                if(!p.ambiguous)existing.emplace(p.first,p.second);
            std::erase_if(result->contacts,[&](const auto& p) {
                if(existing.contains({p.first,p.second})) {
                    ++result->preexistingPairs;
                    return true;
                }
                return false;
            });
        }catch(const std::length_error& e) {
            result->diagnostic=std::string("R01 baseline collision query budget exhausted: ")+e.what();
            // No partial result may be presented as newly introduced:
            // the query's evidential comparison did not complete.
            result->contacts.clear();
        }
    }
    if(result->contacts.empty()&&result->diagnostic.empty())return {};
    std::set<FaceId> markedFaces;
    std::set<VertexId> markedVertices;
    std::set<RegionTopologyIndex::Edge> markedEdges;
    for(const auto& contact:result->contacts) {
        markedFaces.insert(contact.first);
        markedFaces.insert(contact.second);
    }
    for(const auto face:markedFaces) {
        const auto f=proposed.candidate->maybeFace(face);
        if(!f)throw std::logic_error("R01 contact references missing candidate FaceId");
        for(unsigned i=0;i<3;++i) {
            markedVertices.insert(f->vertices[i]);
            markedEdges.insert(canonical(f->vertices[i],f->vertices[(i+1)%3]));
        }
    }
    result->affectedFaces.assign(markedFaces.begin(),markedFaces.end());
    result->incidentVertices.assign(markedVertices.begin(),markedVertices.end());
    result->incidentEdges.assign(markedEdges.begin(),markedEdges.end());
    result->previewEnabled=previewEnabled_;
    result->excisionEnabled=excisionEnabled_;
    result->zipperEnabled=zipperEnabled_;
    result->capEnabled=capEnabled_;
    result->autoEnabled=autoEnabled_;
    if(previewEnabled_&&!result->contacts.empty()) {
        try {
            result->excision=previewApproximateExcision(
                *proposed.candidate,result->contacts);
        }catch(const std::exception& e) {
            result->excision.complete=false;
            result->excision.diagnostic=std::string("R02 preview could not complete: ")+e.what();
        }
    }
    if(excisionEnabled_) {
        if(!result->diagnostic.empty()||!result->excision.complete) {
            // Never lose the actual cause in a generic refusal banner.
            const auto reason=!result->diagnostic.empty()?result->diagnostic:
                (!result->excision.diagnostic.empty()?result->excision.diagnostic:
                 "R02 cut plan was not certified complete");
            result->excisionDiagnostic="R03 refuses incomplete contact or R02 cut plan: "+reason;
        }
        else {
            try {
                result->privateCut=applyPrivateExcision(
                    proposed.candidate,result->excision);
            }catch(const std::exception& e) {
                result->excisionDiagnostic=std::string("R03 private cut refused: ")+e.what();
            }
        }
    }
    if(zipperEnabled_) {
        if(!result->privateCut) {
            result->zipperDiagnostic="R04 requires a successful R03 private excision: "+
                result->excisionDiagnostic;
        }else {
            try {
                result->privateZipper=applyForcedPrivateZipper(
                    *result->privateCut,result->excision);
            }catch(const std::exception& e) {
                result->zipperDiagnostic=std::string("R04 forced zipper refused: ")+e.what();
            }
        }
    }
    if(capEnabled_) {
        if(!result->privateCut)
            result->capDiagnostic="R05 requires complete R03 excision: "+
                result->excisionDiagnostic;
        else {
            try {
                result->privateCap=applyForcedPrivateCap(
                    *result->privateCut,result->excision);
            }catch(const std::exception& e) {
                result->capDiagnostic=std::string("R05 forced cap refused: ")+e.what();
            }
        }
    }
    if(autoEnabled_)runAutomaticClosure(*result,symmetricSource_,classifier_,
                                        warnQuality_,{},scaleAware_,optimizeFront_,
                                        optimizeCurvatureFront_,guideCurvaturePoisson_);
    result->exactOverlay=exactOverlay_;
    buildSeamAuditIfRequested(*result,auditSeam_||exactOverlay_);
    {
        std::lock_guard lock(mutex_);
        if(frozen_)return "R01 diagnostic view is frozen; relaunch to sculpt again";
        snapshot_=std::move(result);
        frozen_.store(true,std::memory_order_release);
    }
    return "R01 diagnostic intersection detected; private candidate retained for read-only inspection";
}

std::shared_ptr<const RepairInspectionSnapshot>
RepairInspectionSession::frozenSnapshot() const {
    if(!frozen())return {};
    std::lock_guard lock(mutex_);
    return snapshot_;
}


std::shared_ptr<const RepairInspectionSnapshot> replayPrivateAutomaticRepair(
    const RepairCase& capture,AutoClosureClassifier classifier,bool warnQuality,
    bool auditSeam,bool exactOverlay,
    std::shared_ptr<RepairStepTrace> stepTrace,
    bool scaleAware,bool optimizeFront,bool optimizeCurvatureFront,
    bool guideCurvaturePoisson) {
    if(!capture.candidate.index||capture.contacts.empty())
        throw std::invalid_argument("R06 replay needs a valid candidate and saved contacts");
    auto result=std::make_shared<RepairInspectionSnapshot>();
    result->candidate=capture.candidate.index;
    result->contacts=capture.contacts;
    result->previewEnabled=true;
    result->excisionEnabled=true;
    result->autoEnabled=true;
    std::set<FaceId> touchedFaces;
    std::set<VertexId> touchedVertices;
    std::set<RegionTopologyIndex::Edge> touchedEdges;
    for(const auto& contact:result->contacts) {
        touchedFaces.insert(contact.first);
        touchedFaces.insert(contact.second);
    }
    for(const auto id:touchedFaces) {
        const auto& face=result->candidate->face(id).vertices;
        for(unsigned j=0;j<3;++j) {
            touchedVertices.insert(face[j]);
            touchedEdges.insert(canonical(face[j],face[(j+1)%3]));
        }
    }
    result->affectedFaces.assign(touchedFaces.begin(),touchedFaces.end());
    result->incidentVertices.assign(touchedVertices.begin(),touchedVertices.end());
    result->incidentEdges.assign(touchedEdges.begin(),touchedEdges.end());
    try {
        result->excision=previewApproximateExcision(
            *result->candidate,result->contacts);
    }catch(const std::exception& error) {
        result->excision.complete=false;
        result->excision.diagnostic=std::string("R02 replay refused: ")+error.what();
    }
    if(!result->excision.complete) {
        result->excisionDiagnostic="R03 refuses incomplete replay cut plan: "+
            result->excision.diagnostic;
    }else {
        try {
            result->privateCut=applyPrivateExcision(
                result->candidate,result->excision);
        }catch(const std::exception& error) {
            result->excisionDiagnostic=std::string("R03 private cut refused: ")+error.what();
        }
    }
    runAutomaticClosure(*result,capture.symmetricSource,classifier,warnQuality,
                        stepTrace,scaleAware,optimizeFront,optimizeCurvatureFront,
                        guideCurvaturePoisson);
    result->exactOverlay=exactOverlay;
    buildSeamAuditIfRequested(*result,auditSeam||exactOverlay);
    return result;
}

std::vector<PartitionRenderVertex> buildRepairInspectionOverlay(
    const RepairInspectionSnapshot& snapshot) {
    std::vector<PartitionRenderVertex> overlay;
    if(!snapshot.candidate)return overlay;
    // Shade real private cap/zipper triangles with physical-pixel edges,
    // even when ordinary model wireframe is toggled off. Warnings mark
    // ALL deficient faces and their three edges, including any affected
    // surviving face; they do not certify or publish this private mesh.
    const auto drawClosure=[&](
        const RegionTopologyIndex& mesh,
        const std::vector<FaceId>& caps,const std::vector<FaceId>& zippers,
        const std::vector<FaceId>& bad) {
        const auto drawFaces=[&](const std::vector<FaceId>& ids,
                                  float material,bool warned) {
            for(const auto id:ids) {
                const auto& v=mesh.face(id).vertices;
                const auto a=mesh.vertex(v[0]).position;
                const auto b=mesh.vertex(v[1]).position;
                const auto c=mesh.vertex(v[2]).position;
                const auto n=cross(b-a,c-a);
                const auto norm=length(n);
                // Keep red diagnostic edges visible even for degenerate
                // faces whose filled triangles occupy zero area.
                if(norm>1e-18) {
                    const auto distance=.003*std::min({
                        length(a-b),length(b-c),length(c-a)});
                    const auto offset=snapshot.exactOverlay?Vec3{}:
                        n/norm*(warned?distance*1.5:distance);
                    triangle(overlay,a+offset,b+offset,c+offset,material);
                    triangle(overlay,c-offset,b-offset,a-offset,material);
                }
                // Non-rasterizable zero-area warning faces retain tiny red
                // fallback edge rods; normal edges are shader-barycentric.
                if(norm<=1e-18&&warned)
                    for(unsigned k=0;k<3;++k)
                        rod(overlay,mesh.vertex(v[k]).position,
                            mesh.vertex(v[(k+1)%3]).position,
                            .00015,11.0f);
            }
        };
        drawFaces(caps,9.0f,false);
        drawFaces(zippers,8.0f,false);
        drawFaces(bad,10.0f,true); // tinted light-red rejected face
        // Exact-position yellow seam markers remain separate from the
        // displaced (or undisplaced) colored face overlay. These use
        // ORIGINAL shared edge endpoint IDs, never matching XYZ guesses.
        if(snapshot.exactOverlay&&snapshot.seamAudit)
            for(const auto& e:snapshot.seamAudit->interfaceEdges)
                rod(overlay,mesh.vertex(e.a).position,
                    mesh.vertex(e.b).position,.00030,13.0f);
    };
    if(snapshot.autoEnabled&&snapshot.privateAuto) {
        const auto& result=*snapshot.privateAuto;
        drawClosure(*result.index,result.greenFaces,result.blueFaces,{});
        return overlay;
    }
    if(snapshot.autoEnabled&&snapshot.warningPreview) {
        const auto& result=*snapshot.warningPreview;
        drawClosure(*result.index,result.greenFaces,result.blueFaces,
                    result.qualityWarningFaces);
        return overlay;
    }
    if(snapshot.capEnabled&&snapshot.privateCap) {
        const auto& result=*snapshot.privateCap;
        drawClosure(*result.index,result.greenFaces,{}, {});
        return overlay;
    }
    if(snapshot.zipperEnabled&&snapshot.privateZipper) {
        const auto& result=*snapshot.privateZipper;
        drawClosure(*result.index,{},result.blueFaces,{});
        return overlay;
    }
    if(snapshot.excisionEnabled&&snapshot.privateCut) {
        // Only real SURVIVING boundary rims are shown on the privately
        // cut mesh. Removed faces/vertices must not reappear as red/orange
        // "ghosts", which would obscure the actual holes.
        for(const auto& loop:snapshot.excision.contours) {
            for(std::size_t i=0;i<loop.vertexIds.size();++i) {
                const auto a=loop.vertexIds[i];
                const auto b=loop.vertexIds[(i+1)%loop.vertexIds.size()];
                rod(overlay,snapshot.privateCut->index->vertex(a).position,
                    snapshot.privateCut->index->vertex(b).position,.0025,6.0f);
            }
        }
        return overlay;
    }
    // Real incident edge lengths keep markers visible on both coarse
    // fixture geometry and ordinary higher-density sphere meshes.
    std::map<VertexId,double> radius;
    for(const auto vertex:snapshot.incidentVertices)
        radius.emplace(vertex,std::numeric_limits<double>::infinity());
    for(const auto edge:snapshot.incidentEdges) {
        const auto a=snapshot.candidate->vertex(edge.a).position;
        const auto b=snapshot.candidate->vertex(edge.b).position;
        const double distance=length(b-a);
        if(auto it=radius.find(edge.a);it!=radius.end())
            it->second=std::min(it->second,distance);
        if(auto it=radius.find(edge.b);it!=radius.end())
            it->second=std::min(it->second,distance);
    }
    const auto markerRadius=[&](VertexId id) {
        const auto it=radius.find(id);
        if(it==radius.end()||!std::isfinite(it->second))return .005;
        return std::clamp(it->second*.10,.004,.045);
    };
    overlay.reserve(snapshot.incidentVertices.size()*24+
                    snapshot.incidentEdges.size()*24);
    for(const auto edge:snapshot.incidentEdges)
        rod(overlay,snapshot.candidate->vertex(edge.a).position,
            snapshot.candidate->vertex(edge.b).position,
            std::min(markerRadius(edge.a),markerRadius(edge.b))*.34);
    for(const auto id:snapshot.incidentVertices)
        dot(overlay,snapshot.candidate->vertex(id).position,markerRadius(id));
    if(snapshot.previewEnabled&&!snapshot.excisionEnabled) {
        // The face inset exposes underlying wireframe and makes the
        // diagnostic region distinguishable from the red R01 contact halo.
        const auto highlight=[&](FaceId id,float material) {
            const auto& f=snapshot.candidate->face(id).vertices;
            const auto a=snapshot.candidate->vertex(f[0]).position;
            const auto b=snapshot.candidate->vertex(f[1]).position;
            const auto c=snapshot.candidate->vertex(f[2]).position;
            const auto center=(a+b+c)/3.0;
            const auto normal=cross(b-a,c-a);
            const auto scale=length(normal);
            const auto offset=scale>0?
                normal/scale*(.003*std::min({length(a-b),length(b-c),length(c-a)})):
                Vec3{};
            triangle(overlay,center+(a-center)*.89+offset,
                             center+(b-center)*.89+offset,
                             center+(c-center)*.89+offset,material);
        };
        for(const auto id:snapshot.excision.proposedFaces)highlight(id,4.0f);
        for(const auto id:snapshot.excision.uncertainFaces)highlight(id,5.0f);
        // Cyan dots mark vertices absorbed to repair branching cut rims.
        for(const auto id:snapshot.excision.expandedBoundaryVertices)
            dot(overlay,snapshot.candidate->vertex(id).position,.008,7.0f);
        for(const auto& loop:snapshot.excision.contours) {
            for(std::size_t i=0;i<loop.vertexIds.size();++i) {
                const auto a=loop.vertexIds[i];
                const auto b=loop.vertexIds[(i+1)%loop.vertexIds.size()];
                rod(overlay,snapshot.candidate->vertex(a).position,
                    snapshot.candidate->vertex(b).position,
                    std::max(.0025,std::min(markerRadius(a),markerRadius(b))*.53),6.0f);
            }
        }
    }
    return overlay;
}

} // namespace mesher
