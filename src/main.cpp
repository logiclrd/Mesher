#include "mesher/brush.hpp"
#include "mesher/async_sculpt.hpp"
#include "mesher/brush_radius.hpp"
#include "mesher/mesh.hpp"
#include "mesher/viewport.hpp"
#include "mesher/durable_checkpoint.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <charconv>
#include <iostream>
#include <cstdlib>
#include <cmath>
#include <string>
#include <string_view>
#include <memory>
#include <iomanip>
#include <sstream>
#include <stdexcept>

int main(int argc, char** argv) {
    mesher::SphereOptions options;
    bool inflate = true, demoMode = false, verboseInput = false;
    bool allowMergeGeometry = false;
    double step = .02, strength = .012, brushRadius = .4, stampRate = 10.0, timeSpeed = .2;
    std::uint32_t segments = 128;
    mesher::CollapseSettings collapseSettings;
    bool compareRemesh = false, compareMitigations = false;
    mesher::CurvatureSettings curvatureSettings;
    mesher::RefineSettings refineSettings;
    bool compareLocalRemesh = false;
    std::filesystem::path checkpointPath="mesher-checkpoint.mshr";
    std::filesystem::path recoverPath;
    try {
        for(int i=1;i<argc;++i) {
            const std::string_view arg=argv[i];
            if(arg=="--asymmetric") options.symmetric=false;
            else if(arg=="--collapse") collapseSettings.enabled=true;
            else if(arg=="--refine") refineSettings.enabled=true;
            else if(arg=="--no-edge-flips") refineSettings.optimizeFlips=false;
            else if(arg=="--compare-local-remesh") compareLocalRemesh=true;
            else if(arg=="--compare-remesh") compareRemesh=true;
            else if(arg=="--compare-mitigations") compareMitigations=true;
            else if(arg=="--curvature-correction") curvatureSettings.enabled=true;
            else if(arg=="--no-inflate") inflate=false;
            else if(arg=="--demo") demoMode=true;
            else if(arg=="--verbose-input") verboseInput=true;
            else if(arg=="--merge-geometry")allowMergeGeometry=true;
            else if(arg=="--checkpoint"||arg=="--recover") {
                if(++i>=argc)throw std::invalid_argument("Missing checkpoint filename");
                if(arg=="--checkpoint")checkpointPath=argv[i];
                else recoverPath=argv[i];
            }
            else if(arg=="--step" || arg=="--strength" || arg=="--brush-radius" || arg=="--stamps-per-second" || arg=="--minimum-edge-ratio" || arg=="--curvature-strength" || arg=="--maximum-edge-ratio" || arg=="--time-speed") {
                if(++i>=argc) throw std::invalid_argument("Missing value after "+std::string(arg));
                const std::string value=argv[i];
                std::size_t parsed=0;
                const double v=std::stod(value,&parsed);
                if(parsed!=value.size() || !std::isfinite(v) ||
                   ((arg=="--step" || arg=="--brush-radius" || arg=="--stamps-per-second" || arg=="--minimum-edge-ratio" || arg=="--time-speed") && v<=0))
                    throw std::invalid_argument("Invalid brush parameter: "+value);
                if(arg=="--step")step=v;
                else if(arg=="--brush-radius")brushRadius=v;
                else if(arg=="--stamps-per-second")stampRate=v;
                else if(arg=="--time-speed")timeSpeed=v;
                else if(arg=="--minimum-edge-ratio")collapseSettings.minimumEdgeRatio=v;
                else if(arg=="--maximum-edge-ratio"){
                    if(v<=0)throw std::invalid_argument("Maximum edge ratio must be positive");
                    refineSettings.maximumEdgeRatio=v;
                }
                else if(arg=="--curvature-strength"){
                    if(v<0)throw std::invalid_argument("Curvature strength must be nonnegative");
                    curvatureSettings.strength=v;
                    curvatureSettings.enabled=true;
                }
                else strength=v;
            }
            else if(arg=="--max-split-pairs" || arg=="--max-flip-pairs") {
                if(++i>=argc)throw std::invalid_argument("Missing refinement operation limit");
                const std::string_view value=argv[i];
                std::uint64_t count=0;
                const auto [p,ec]=std::from_chars(value.data(),value.data()+value.size(),count);
                if(ec!=std::errc{} || p!=value.data()+value.size() || count>1000)
                    throw std::invalid_argument("Invalid refinement operation limit");
                if(arg=="--max-split-pairs")
                    refineSettings.maximumSplitPairsPerStamp=static_cast<std::uint32_t>(count);
                else
                    refineSettings.maximumFlipPairsPerStamp=static_cast<std::uint32_t>(count);
            }
            else if(arg=="--max-collapse-pairs") {
                if(++i>=argc) throw std::invalid_argument("Missing --max-collapse-pairs value");
                const std::string_view value=argv[i];
                std::uint64_t n=0;
                auto [p,ec]=std::from_chars(value.data(),value.data()+value.size(),n);
                if(ec!=std::errc{}||p!=value.data()+value.size()||n>10000)
                    throw std::invalid_argument("Invalid max-collapse-pairs");
                collapseSettings.maximumPairsPerStamp=static_cast<std::uint32_t>(n);
            }
            else if(arg=="--segments") {
                if(++i>=argc) throw std::invalid_argument("Missing --segments value");
                const std::string_view value=argv[i];
                std::uint64_t n=0;
                auto [p,ec]=std::from_chars(value.data(),value.data()+value.size(),n);
                if(ec!=std::errc{} || p!=value.data()+value.size() || n==0 || n>100000)
                    throw std::invalid_argument("Segments must be between 1 and 100000");
                segments=static_cast<std::uint32_t>(n);
            }
            else if(arg=="--symmetric")options.symmetric=true;
            else if(arg=="--vertices"||arg=="--seed"||arg=="--relax") {
                if(++i>=argc)throw std::invalid_argument("Missing value after "+std::string(arg));
                const std::string_view value=argv[i];
                std::uint64_t number=0;
                auto [p,ec]=std::from_chars(value.data(),value.data()+value.size(),number);
                if(ec!=std::errc{}||p!=value.data()+value.size())throw std::invalid_argument("Invalid integer: "+std::string(value));
                if(arg=="--vertices") {
                    if(number>1000000)throw std::invalid_argument("Too many vertices");
                    options.vertexCount=static_cast<std::uint32_t>(number);
                } else if(arg=="--seed")options.seed=number;
                else {
                    if(number>10000)throw std::invalid_argument("Too many relaxation iterations");
                    options.relaxationIterations=static_cast<std::uint32_t>(number);
                }
            } else if(arg=="--help"||arg=="-h"){
                std::cout<<"Mesher [--symmetric|--asymmetric] [--vertices N] [--seed N] [--relax N]\n"
                         <<"       [--no-inflate] [--step D] [--strength D] [--segments N] [--brush-radius R]\n"
                         <<"       [--stamps-per-second HZ] [--collapse] [--compare-remesh]\n"
                         <<"       [--minimum-edge-ratio R] [--max-collapse-pairs N]\n"
                         <<"       [--curvature-correction] [--curvature-strength GAIN]\n"
                         <<"       [--compare-mitigations] [--compare-local-remesh]\n"
                         <<"       [--refine] [--no-edge-flips] [--maximum-edge-ratio R]\n"
                         <<"       [--max-split-pairs N] [--max-flip-pairs N]\\n"
                         <<"       [--demo] [--time-speed MODEL_UNITS_PER_SECOND]\\n"
                         <<"       [--verbose-input] (interactive mouse event trace)\n"
                         <<"       [--checkpoint FILE] [--recover FILE]\n"
                         <<"       [--merge-geometry] (explicit protected opt-in; desktop M toggles mode)\n"
                         <<"Defaults: --symmetric --vertices 5000 --seed 1 --relax 65\n"
                         <<"          Mouse Inflate by default; --demo replays 128 segments at 10 stamps/sec.\n"
                         <<"Escape exits. Space pauses, Right steps (when paused), R replays.\n";
                return 0;
            } else throw std::invalid_argument("Unknown argument: "+std::string(arg));
        }
        std::cout<<"Build: "<<MESHER_BUILD_CONFIGURATION
                 <<"; hull backend: "<<mesher::sphereHullBackend()
                 <<"; OpenMP max relaxation threads: "<<mesher::sphereRelaxationThreads()
                 <<"\n";
        if(allowMergeGeometry&&(demoMode||compareLocalRemesh||
           compareRemesh||compareMitigations||!inflate))
            throw std::invalid_argument(
                "--merge-geometry only supports interactive, non-demo sculpt mode");
        if(allowMergeGeometry&&!recoverPath.empty())
            throw std::invalid_argument(
                "--merge-geometry refuses V13 disk recovery without sealed first-original M29 authority");
        mesher::Mesh mesh;
        std::optional<mesher::DurableCheckpoint> recovered;
        if(!recoverPath.empty()) {
            if(demoMode||compareLocalRemesh||compareRemesh||
               compareMitigations||!inflate)
                throw std::invalid_argument(
                    "--recover requires interactive Inflate mode");
            recovered=mesher::readCheckpoint(recoverPath);
            mesh=mesher::flattenRevisionForViewport(
                *recovered->index,mesher::FlatMeshIndex{});
            if(mesh.vertices.empty())throw std::runtime_error("Empty checkpoint mesh");
            options.vertexCount=static_cast<std::uint32_t>(mesh.vertices.size());
            options.symmetric=!recovered->mirrorPairs.empty();
            std::cout<<"Recovered "<<mesh.vertices.size()<<" vertices, "
                     <<mesh.triangles.size()<<" faces, "
                     <<recovered->index->retiredVertexCount()
                     <<" retired vertices from "<<recoverPath.string()<<"\n";
        }else {
            std::cout<<"Generating "<<options.vertexCount
                     <<(options.symmetric?" symmetric":" asymmetric")
                     <<" vertices..."<<std::flush;
            mesher::SphereGenerationTimings generation{};
            mesh=mesher::generateSphere(options,&generation);
            std::cout<<" "<<mesh.triangles.size()
                     <<" triangles; watertight manifold verified.\n"
                     <<std::fixed<<std::setprecision(3)
                     <<"Sphere timing: relaxation "<<generation.relaxationSeconds
                     <<" s; hull "<<generation.triangulationSeconds
                     <<" s; validation "<<generation.validationSeconds
                     <<" s; total "<<generation.totalSeconds<<" s\n";
        }
        if(compareLocalRemesh) {
            struct Variant {const char* name;bool refine;bool flips;bool collapse;};
            constexpr std::array<Variant,5> modes{{
                {"baseline",false,false,false},
                {"collapse",false,false,true},
                {"split",true,false,false},
                {"split_flip",true,true,false},
                {"all",true,true,true}
            }};
            std::array<mesher::Mesh,5> meshes{mesh,mesh,mesh,mesh,mesh};
            std::array<std::unique_ptr<mesher::InflateDemoAnimation>,5> runs;
            for(std::size_t i=0;i<modes.size();++i) {
                auto collapse=collapseSettings;
                collapse.enabled=modes[i].collapse;
                auto refine=refineSettings;
                refine.enabled=modes[i].refine;
                refine.optimizeFlips=modes[i].flips;
                runs[i]=std::make_unique<mesher::InflateDemoAnimation>(
                    meshes[i],step,strength,segments,brushRadius,
                    collapse,options.symmetric,curvatureSettings,refine);
            }
            std::cout<<std::setprecision(9)
                     <<"mode,stamp,vertices,faces,min_edge,min_altitude,inverted,"
                       "split_pairs,flip_pairs,added_vertices,collapsed_vertices\n";
            auto dump=[&](std::size_t i) {
                const auto& anim=*runs[i];
                const auto& d=anim.diagnostics();
                std::cout<<modes[i].name<<","<<anim.completed()<<","
                         <<meshes[i].vertices.size()<<","<<meshes[i].triangles.size()
                         <<","<<d.minimumEdgeLength<<","<<d.minimumTriangleAltitude
                         <<","<<d.invertedFaces<<","<<anim.lastRefine().splitPairs
                         <<","<<anim.lastRefine().flipPairs<<","
                         <<anim.lastRefine().newVertices<<","
                         <<anim.lastCollapse().removedVertices<<"\n";
            };
            for(std::size_t i=0;i<modes.size();++i)dump(i);
            for(std::uint32_t stamp=0;stamp<runs[0]->total();++stamp)
                for(std::size_t i=0;i<modes.size();++i) {
                    if(!runs[i]->advance())throw std::logic_error("Inconsistent stamp count");
                    dump(i);
                }
            return 0;
        }
        if(compareRemesh || compareMitigations) {
            struct Variant {const char* name;bool collapse;bool curvature;};
            constexpr std::array<Variant,4> variants{{
                {"baseline",false,false},
                {"curvature",false,true},
                {"collapse",true,false},
                {"both",true,true}
            }};
            std::array<mesher::Mesh,4> meshes{mesh,mesh,mesh,mesh};
            std::array<std::unique_ptr<mesher::InflateDemoAnimation>,4> animations;
            for(std::size_t i=0;i<variants.size();++i) {
                if(!compareMitigations && (i==1 || i==3))continue;
                mesher::CollapseSettings cs=collapseSettings;
                cs.enabled=variants[i].collapse;
                mesher::CurvatureSettings cur=curvatureSettings;
                cur.enabled=variants[i].curvature;
                animations[i]=std::make_unique<mesher::InflateDemoAnimation>(
                    meshes[i],step,strength,segments,brushRadius,cs,options.symmetric,cur);
            }
            std::cout << std::setprecision(9);
            std::cout << "mode,stamp,vertices,faces,min_edge,min_altitude,inverted,"
                         "shortening_below_limit,collapsed_pairs,removed_vertices\n";
            auto dump=[&](std::size_t i) {
                const auto& anim=*animations[i];
                const auto& d=anim.diagnostics();
                std::cout<<variants[i].name<<","<<anim.completed()
                         <<","<<meshes[i].vertices.size()
                         <<","<<meshes[i].triangles.size()<<","<<d.minimumEdgeLength
                         <<","<<d.minimumTriangleAltitude<<","<<d.invertedFaces
                         <<","<<anim.preCollapseDiagnostics().shrinkingShortEdges
                         <<","<<anim.lastCollapse().accepted
                         <<","<<anim.lastCollapse().removedVertices<<"\n";
            };
            for(std::size_t i=0;i<variants.size();++i)
                if(animations[i])dump(i);
            while(true) {
                bool advanced=false;
                for(std::size_t i=0;i<variants.size();++i) {
                    if(!animations[i])continue;
                    if(animations[i]->advance()) {
                        advanced=true;
                        dump(i);
                    }
                }
                if(!advanced)break;
            }
            return 0;
        }
        mesher::ViewportAnimation playback;
        std::unique_ptr<mesher::InflateDemoAnimation> demo;
        mesher::ViewportInteraction interaction;
        std::unique_ptr<mesher::AsyncSculptWorker> liveWorker;

        if(inflate && !demoMode) {
            const auto brushRange=mesher::BrushRadiusRange::forVertexCount(options.vertexCount);
            const double requestedBrushRadius=brushRadius;
            brushRadius=brushRange.clamp(brushRadius);
            if(brushRadius!=requestedBrushRadius)
                std::cout<<"Interactive brush radius clamped from "
                         <<requestedBrushRadius<<" to "<<brushRadius
                         <<" (limits "<<brushRange.minimum<<"-"<<brushRange.maximum<<").\n";
            // The worker publishes immutable geometry/index pairs and dirty
            // face-owner partition IDs. The Vulkan renderer updates only
            // changed partitions; no flat compatibility conversion remains.
            if(recovered)
                liveWorker=std::make_unique<mesher::AsyncSculptWorker>(
                    std::move(*recovered),step,strength,timeSpeed,
                    curvatureSettings,collapseSettings,refineSettings);
            else {
                mesher::SculptCollisionSettings mergePolicy;
                mergePolicy.physicalExact=allowMergeGeometry;
                // The global OFF startup remains bit-for-bit the previous
                // legacy worker construction. Only --merge-geometry explicitly
                // authorizes the sealed original-source worker at launch.
                liveWorker=std::make_unique<mesher::AsyncSculptWorker>(
                    mesh,step,strength,timeSpeed,curvatureSettings,
                    collapseSettings,refineSettings,options.symmetric,
                    mesher::SculptStepValidator{},mergePolicy);
            }
            if(allowMergeGeometry) {
                interaction.mergeSelected=[&](
                    std::shared_ptr<const mesher::RegionTopologyIndex> root,
                    std::vector<mesher::FaceId> nominated) {
                    liveWorker->mergeDiscoveredContacts(
                        std::move(root),std::move(nominated));
                };
                interaction.mergeStatus=[&] {
                    const auto status=liveWorker->status();
                    const auto pending=status.liveMergeRequests>=
                         status.liveMergeAccepted+status.liveMergeRefusals
                        ?status.liveMergeRequests-
                         status.liveMergeAccepted-status.liveMergeRefusals:0;
                    std::string text="A "+std::to_string(status.liveMergeAccepted)+
                        " R "+std::to_string(status.liveMergeRefusals)+
                        " P "+std::to_string(pending);
                    if(status.terminalFailure)
                        text+=" TERMINAL "+status.terminalFailureReason;
                    else if(!status.liveMergeRefusal.empty())
                        text+=" REFUSED "+status.liveMergeRefusal;
                    return text;
                };
            }
            interaction.undo=[&]{liveWorker->undo();};
            interaction.redo=[&]{liveWorker->redo();};
            interaction.checkpoint=[&]{
                liveWorker->saveCheckpoint(checkpointPath);
                std::cout<<"[checkpoint] queued "<<checkpointPath.string()<<"\n";
            };
            interaction.radiusRange=brushRange;
            interaction.radius=brushRadius;
            interaction.verboseInput=verboseInput;
            interaction.setRadius=[&,brushRange](double radius) {
                brushRadius=brushRange.clamp(radius);
                liveWorker->setRadius(brushRadius);
            };
            interaction.begin=[&](mesher::Vec3 point) {
                liveWorker->begin(point,brushRadius);
            };
            interaction.update=[&](mesher::Vec3 point,double elapsed) {
                liveWorker->update(point,elapsed);
            };
            interaction.end=[&](mesher::Vec3 point,double elapsed) {
                liveWorker->end(point,elapsed);
            };
            interaction.initialRevision=liveWorker->initialRevision();
            interaction.takeSnapshot=[&]() -> std::optional<mesher::ViewportMeshUpdate> {
                auto snapshot=liveWorker->takeSnapshot();
                if(!snapshot)return std::nullopt;
                return mesher::ViewportMeshUpdate{
                    std::move(snapshot->index),std::move(snapshot->dirtyPartitions),
                    snapshot->completedStamps};
            };
            interaction.completedStamps=[&]{return liveWorker->status().completedStamps;};
            interaction.skippedStamps=[]{return std::size_t{0};};
            interaction.pendingSamples=[&]{return liveWorker->status().pendingSteps;};
            interaction.pendingSegments=[&]{return liveWorker->status().pendingSegments;};
            interaction.totalQueuedSteps=[&]{return liveWorker->status().totalQueuedSteps;};
            interaction.inputCommands=[&]{return liveWorker->status().inputCommands;};
            interaction.canBegin=[&]{return liveWorker->canBegin();};
            interaction.lastStampDetails=[&] {
                const auto status=liveWorker->status();
                const auto& d=status.details;
                const auto& times=status.timings;
                std::ostringstream os;
                os<<std::fixed<<std::setprecision(4)
                  <<"radius="<<(d.areas.empty()?brushRadius:d.areas.front().radius)
                  <<" brush_centers=[";
                for(std::size_t i=0;i<d.areas.size();++i) {
                    if(i)os<<";";
                    const auto p=d.areas[i].center;
                    os<<"("<<p.x<<","<<p.y<<","<<p.z<<")";
                }
                os<<"] influenced_vertices="<<d.influenced
                  <<" moved_vertices="<<d.moved
                  <<" splits="<<d.splitPairs
                  <<" flips="<<d.flipPairs
                  <<" collapses="<<d.collapsedPairs
                  <<" vertices="<<d.vertices
                  <<" triangles="<<d.triangles
                  <<" inflate_ms="<<times.inflateSeconds*1000
                  <<" refine_ms="<<times.refineSeconds*1000
                  <<" collapse_ms="<<times.collapseSeconds*1000
                  <<" adjacency_rebuild_ms="<<times.rebuildSeconds*1000
                  <<" worker_stamp_ms="<<status.lastWorkerStampMs
                  <<" pending_input_commands="<<status.inputCommands
                   <<" published_revisions="<<status.publishedRevisions
                   <<" modified_partitions="<<status.modifiedPartitions
                   <<" copied_directory_nodes="<<status.copiedDirectoryNodes
                   <<" cloned_partition_bytes="<<status.clonedPartitionBytes
                   <<" live_revisions="<<status.liveRevisions
                   <<" peak_live_revisions="<<status.peakLiveRevisions
                   <<" worker_publication_ms="<<status.lastPublicationMs
                   <<" undo_depth="<<status.history.undoDepth
                   <<" redo_depth="<<status.history.redoDepth
                   <<" history_roots="<<status.history.retainedRevisions
                   <<" history_change_bytes="<<status.history.retainedChangeBytes
                   <<" checkpoints_saved="<<status.checkpoints.completed
                   <<" checkpoint_failures="<<status.checkpoints.failed
                   <<" renderer=dirty_partitions";
                return os.str();
            };
            interaction.stationaryStampsPerSecond=timeSpeed/step;
            interaction.caption=[&] {
                const auto status=liveWorker->status();
                return "Mouse Inflate | vertices "+std::to_string(status.details.vertices
                              ?status.details.vertices:mesh.vertices.size())+
                       " | stamps "+std::to_string(status.completedStamps)+
                       " | pending "+std::to_string(status.pendingSteps)+
                       " | input "+std::to_string(status.inputCommands)+
                       (options.symmetric?" | mirrored":" | asymmetric");
            };
            if(allowMergeGeometry)
                std::cout<<"Merge geometry: explicitly authorized protected original-root worker; "
                         <<"desktop starts OFF. Press M to toggle Merge selection, "
                         <<"left-click indexed faces to nominate stable first-original FaceIds, "
                         <<"Enter to submit, Backspace to clear. The worker may reject "
                         <<"unsupported, stale, no-contact or unverified sources. "
                         <<"Regular startup without --merge-geometry remains unchanged.\n";
            std::cout<<"Mouse Inflate: left-drag or hold to sculpt; radius slider "
                     <<"in lower-left (min "<<brushRange.minimum<<", max "
                     <<brushRange.maximum<<", initial "<<brushRadius<<").\n";
            std::cout<<"Interactive sculpting uses a dedicated background worker; "
                     <<"Vulkan refreshes only dirty face partitions.\n"
                     <<"Ctrl+Z undo, Ctrl+Y / Ctrl+Shift+Z redo, Ctrl+S "
                     <<"background checkpoint to "<<checkpointPath.string()<<".\n";
            if(verboseInput)
                std::cout<<"Verbose interactive input logging enabled: [input] "
                         <<"down, up, moves that queue Steps, and queue drain.\n";
            std::cout<<"Mouse Inflate: left-drag or hold to sculpt; time speed "
                     <<timeSpeed<<" units/sec; step "<<step
                     <<(options.symmetric?"; mirrored.\n":"; single brush.\n");
        }
        if (inflate && demoMode) {
            demo = std::make_unique<mesher::InflateDemoAnimation>(
                mesh, step, strength, segments, brushRadius,
                collapseSettings, options.symmetric, curvatureSettings, refineSettings);
            playback.stampsPerSecond=stampRate;
            playback.advance=[&] { return demo->advance(); };
            playback.reset=[&] { demo->reset(); };
            playback.caption=[&] {

                const auto& d=demo->diagnostics();
                return "Inflate " + std::to_string(demo->completed()) +
                       "/" + std::to_string(demo->total()) +
                       "  verts " + std::to_string(mesh.vertices.size()) +
                       "  short/shrinking " +
                       std::to_string(demo->preCollapseDiagnostics().shrinkingShortEdges) +
                       "  removed " + std::to_string(demo->totalRemoved()) +
                       "  added " + std::to_string(demo->totalAdded()) +
                       "  flips " + std::to_string(demo->totalFlips()) +
                       "  min-edge " + std::to_string(d.minimumEdgeLength) +
                       "  min-alt " + std::to_string(d.minimumTriangleAltitude);
            };
            std::cout<<"Curvature correction: "
                     <<(curvatureSettings.enabled?"on":"off")
                     <<" (gain "<<curvatureSettings.strength<<").\n";
            std::cout<<"Refinement: "<<(refineSettings.enabled?"on":"off")
                     <<" (maximum edge "<<demo->maximumEdgeTarget()
                     <<", flips "<<(refineSettings.optimizeFlips?"on":"off")<<").\n";
            std::cout<<"Remeshing: "<<(collapseSettings.enabled?"directional collapsing":"off")
                     <<" (minimum edge "<<demo->minimumEdgeTarget()<<").\n";
            std::cout<<"Animating "<<demo->total()<<" stamps at "<<stampRate
                     <<" stamps/sec; radius "<<brushRadius<<", hardness 0.3.\n";
        }
        mesher::runViewport(mesh, std::move(playback),std::move(interaction));
    }catch(const std::exception& e) {
        std::cerr<<"Mesher: "<<e.what()<<'\n';
        return 1;
    }
}
