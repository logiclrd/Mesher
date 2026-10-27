#include "mesher/brush.hpp"
#include "mesher/async_sculpt.hpp"
#include "mesher/brush_radius.hpp"
#include "mesher/mesh.hpp"
#include "mesher/viewport.hpp"
#include "mesher/desktop_merge_input.hpp"
#include "mesher/desktop_merge_fixture.hpp"
#include "mesher/durable_checkpoint.hpp"
#include "mesher/desktop_collision_policy.hpp"
#include "mesher/desktop_offmode_fixture.hpp"
#include "mesher/sculpt_fixtures.hpp"
#include "mesher/repair_inspection.hpp"
#include "mesher/repair_case.hpp"
#include "mesher/repair_step_replay_session.hpp"

#include <array>
#include <set>
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
    bool avoidCollisions = false;
    bool avoidTestSheets = false;
    bool repairInspect = false;
    bool repairPreview = false;
    bool repairExcise = false;
    bool repairZipper = false;
    bool repairCap = false;
    bool repairAuto = false;
    bool repairDumpContours = false;
    bool repairQualityWarn = false,repairScaleAware=false,
         repairFrontOptimize=false,repairCurvatureOptimize=false,
         repairCurvaturePoisson=false,repairFeasibilityProbe=false,
         repairDiagonalProbe=false,repairFeasibilityPreview=false,
         repairDiagonalPreview=false,repairWholeCapFairingPreview=false;
    bool repairSeamAudit=false,repairOverlayExact=false;
    bool repairEarClipping=false,repairUnifiedZipper=false;
    bool torusFixture = false, hollowSphereFixture = false;
    bool explicitSymmetric = false;
    bool mergeTestTetra = false;
    bool mergeTestReflectedTetra = false;
    double step = .02, strength = .012, brushRadius = .4, stampRate = 10.0, timeSpeed = .2;
    std::uint32_t segments = 128;
    // M73: desktop CLI defaults ON; library/settings structs and the
    // independent four-way CPU comparison controls are unchanged.
    mesher::CollapseSettings collapseSettings;
    bool compareRemesh = false, compareMitigations = false;
    mesher::CurvatureSettings curvatureSettings;
    mesher::RefineSettings refineSettings;
    collapseSettings.enabled=true;
    curvatureSettings.enabled=true;
    refineSettings.enabled=true;
    bool compareLocalRemesh = false;
    std::filesystem::path checkpointPath="mesher-checkpoint.mshr";
    std::filesystem::path recoverPath;
    std::filesystem::path repairCaseOut,repairReplayPath;
    bool repairReplayTest=false,repairStepDebug=false;
    bool classifierExplicit=false;
    mesher::AutoClosureClassifier repairClassifier=mesher::AutoClosureClassifier::Area;
    try {
        for(int i=1;i<argc;++i) {
            const std::string_view arg=argv[i];
            if(arg=="--asymmetric") {
                options.symmetric=false;explicitSymmetric=false;
            }
            else if(arg=="--no-collapse") collapseSettings.enabled=false;
            else if(arg=="--no-refine") refineSettings.enabled=false;
            else if(arg=="--no-edge-flips") refineSettings.optimizeFlips=false;
            else if(arg=="--compare-local-remesh") compareLocalRemesh=true;
            else if(arg=="--compare-remesh") compareRemesh=true;
            else if(arg=="--compare-mitigations") compareMitigations=true;
            else if(arg=="--no-curvature-correction") curvatureSettings.enabled=false;
            else if(arg=="--no-inflate") inflate=false;
            else if(arg=="--demo") demoMode=true;
            else if(arg=="--verbose-input") verboseInput=true;
            else if(arg=="--merge-geometry")allowMergeGeometry=true;
            else if(arg=="--avoid-collisions")avoidCollisions=true;
            else if(arg=="--avoid-test-sheets")avoidTestSheets=true;
            else if(arg=="--repair-inspect")repairInspect=true;
            else if(arg=="--repair-preview")repairPreview=true;
            else if(arg=="--repair-excise")repairExcise=true;
            else if(arg=="--repair-zipper")repairZipper=true;
            else if(arg=="--repair-cap")repairCap=true;
            else if(arg=="--repair-auto")repairAuto=true;
            else if(arg=="--repair-dump-contours")repairDumpContours=true;
            else if(arg=="--repair-quality-warn")repairQualityWarn=true;
            else if(arg=="--repair-ear-clipping")repairEarClipping=true;
            else if(arg=="--repair-unified-zipper")repairUnifiedZipper=true;
            else if(arg=="--repair-step-debug")repairStepDebug=true;
            else if(arg=="--repair-scale-aware")repairScaleAware=true;
            else if(arg=="--repair-front-optimize")repairFrontOptimize=true;
            else if(arg=="--repair-curvature-optimize")repairCurvatureOptimize=true;
            else if(arg=="--repair-curvature-poisson")repairCurvaturePoisson=true;
            else if(arg=="--repair-feasibility-probe")repairFeasibilityProbe=true;
            else if(arg=="--repair-feasibility-preview")repairFeasibilityPreview=true;
            else if(arg=="--repair-diagonal-probe")repairDiagonalProbe=true;
            else if(arg=="--repair-diagonal-preview")repairDiagonalPreview=true;
            else if(arg=="--repair-whole-cap-fairing-preview")repairWholeCapFairingPreview=true;
            else if(arg=="--repair-seam-audit")repairSeamAudit=true;
            else if(arg=="--repair-overlay-exact")repairOverlayExact=true;
            else if(arg=="--repair-classifier") {
                if(++i>=argc)throw std::invalid_argument(
                    "--repair-classifier requires area or proximity");
                const std::string_view value=argv[i];
                if(value=="area")
                    repairClassifier=mesher::AutoClosureClassifier::Area;
                else if(value=="proximity")
                    repairClassifier=mesher::AutoClosureClassifier::Proximity;
                else throw std::invalid_argument(
                    "--repair-classifier requires area or proximity");
                classifierExplicit=true;
            }
            else if(arg=="--repair-case-out"||arg=="--repair-replay"||
                    arg=="--repair-replay-test") {
                if(++i>=argc)throw std::invalid_argument("Missing R06 case filename");
                if(arg=="--repair-case-out")repairCaseOut=argv[i];
                else {repairReplayPath=argv[i];repairReplayTest=(arg=="--repair-replay-test");}
            }
            else if(arg=="--repair") {
                if(++i>=argc)throw std::invalid_argument("--repair requires zipper, cap, or auto");
                const std::string_view operation=argv[i];
                if(operation=="zipper")repairZipper=true;
                else if(operation=="cap")repairCap=true;
                else if(operation=="auto")repairAuto=true;
                else throw std::invalid_argument("--repair requires zipper, cap, or auto");
            }
            else if(arg=="--torus")torusFixture=true;
            else if(arg=="--hollow-sphere")hollowSphereFixture=true;
            else if(arg=="--merge-test-tetra")mergeTestTetra=true;
            else if(arg=="--merge-test-reflected-tetra")mergeTestReflectedTetra=true;
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
                    // Strength configures a parameter, not the enable
                    // state. --no-curvature-correction must win regardless
                    // of argument order.
                    curvatureSettings.strength=v;
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
            else if(arg=="--symmetric") {
                options.symmetric=true;explicitSymmetric=true;
            }
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
                         <<"       [--stamps-per-second HZ] [--no-collapse] [--compare-remesh]\n"
                         <<"       [--minimum-edge-ratio R] [--max-collapse-pairs N]\n"
                         <<"       [--no-curvature-correction] [--curvature-strength GAIN]\n"
                         <<"       [--compare-mitigations] [--compare-local-remesh]\n"
                         <<"       [--no-refine] [--no-edge-flips] [--maximum-edge-ratio R]\n"
                         <<"       [--max-split-pairs N] [--max-flip-pairs N]\n"
                         <<"       [--demo] [--time-speed MODEL_UNITS_PER_SECOND]\n"
                         <<"       [--verbose-input] (interactive mouse event trace)\n"
                         <<"       [--checkpoint FILE] [--recover FILE]\n"
                         <<"       [--merge-geometry] (explicit protected opt-in; desktop M toggles mode)\n"
                         <<"       [--avoid-collisions] (separate Merge OFF collision avoidance, session-only)\n"
                         <<"       [--avoid-test-sheets] (requires --avoid-collisions OR --repair-inspect, --asymmetric)\n"
                         <<"       [--repair-preview] (R02 classify excision faces and boundary loops; no topology edits)\n"
                         <<"       [--repair-excise] (R03 remove vertex fans in a FROZEN PRIVATE open mesh)\n"
                         <<"       [--repair zipper] (R04 forced private blue zipper with bounded subdivision)\n"
                         <<"       [--repair cap] (R05 forced independent private GREEN caps with subdivision)\n"
                         <<"       [--repair auto] (R07 live certified R06 AUTO; publishes one root and continues brush)\n"
                         <<"       [--repair-inspect --repair auto] (explicit frozen/private R06 diagnostic mode)\n"
                         <<"       [--repair-classifier area|proximity] (R06 auto and replay; default area)\n"
                         <<"       [--repair-dump-contours] (R06 exact R03 rim replay coordinates, optional)\n"
                         <<"       [--repair-quality-warn] (inspection-only, untrusted low-q faces and edges)\n"
                         <<"       [--repair-seam-audit] (R06 log exact new/survivor seams, T junctions and crossing edges)\n"
                         <<"       [--repair-overlay-exact] (R06 draw undisplaced cap/zipper and exact highlighted seam)\n"
                         <<"       [--repair-case-out FILE] (Ctrl+S on any frozen R06; Shift=zipper, Alt=caps)\n"
                         <<"       [--repair-replay FILE] (rerun saved exact R06 candidate/contacts and inspect Vulkan)\n"
                         <<"       [--repair-step-debug --repair-replay FILE] (interactive staged operation debugger: N Next, P Previous, C Current, Enter Continue, Backspace Cancel, PgUp/PgDn arguments)\n"
                         <<"       [--repair-scale-aware] (experimental M17C bounded quality-aware splitting; auto/replay only)\n"
                         <<"       [--repair-front-optimize] (experimental M17D mirrored Poisson ring correction; auto/replay only)\n"
                         <<"       [--repair-curvature-optimize] (M18D opt-in bounded cap seam-continuity placement; auto/replay only, combine with --repair-front-optimize for refusal-1)\n"
                         <<"       [--repair-curvature-poisson] (M18E opt-in fixed-rim directional curvature-guided Poisson solve; auto/replay only)\n"
                         <<"       [--repair-feasibility-probe] (M19A read-only finite tangent-plane/orbit position search, no cap edits)\n"
                         <<"       [--repair-feasibility-preview] (M19A provisional candidate positions in step debugger ONLY; private, NOT CERTIFIED)\n"
                         <<"       [--repair-diagonal-probe] (M19C read-only mirrored strip-diagonal search, no staged topology changes)\n"
                         <<"       [--repair-diagonal-preview] (M19D PRIVATE provisional mirrored diagonal topology in step debugger ONLY; NOT CERTIFIED)\n"
                         <<"       [--repair-ear-clipping] (R06: try projected ears first, then adaptive advancing front on refusal; default AFM only)\\n"
                         <<"       [--repair-unified-zipper] (M23B diagnostic/replay opt-in; live R06 uses accepted unified zipper)\\n"
                         <<"       [--repair-whole-cap-fairing-preview] (M21A optional whole-cap fairing on a PRIVATE saved-case step; baseline fallback)\n"
                         <<"       [--repair-replay-test FILE] (headless regression, exit nonzero on outcome change)\n"
                         <<"       [--torus|--hollow-sphere] (area-sampled, relaxed topology test meshes)\n"
                         <<"       [--repair-inspect] (R01 freeze at post-stamp intersections; read-only red markers)\n"
                         <<"       [--merge-test-tetra] (requires --merge-geometry --asymmetric; M50 fixture)\n"
                         <<"       [--merge-test-reflected-tetra] (requires --merge-geometry --symmetric; M51 fixture)\n"
                         <<"Defaults: --symmetric --vertices 5000 --seed 1 --relax 65; collapse/curvature/refine ON\n"
                         <<"          Mouse Inflate by default; --demo replays 128 segments at 10 stamps/sec.\n"
                         <<"Escape exits. Space pauses, Right steps (when paused), R replays.\n";
                return 0;
            } else throw std::invalid_argument("Unknown argument: "+std::string(arg));
        }
        std::cout<<"Build: "<<MESHER_BUILD_CONFIGURATION
                 <<"; hull backend: "<<mesher::sphereHullBackend()
                 <<"; OpenMP max relaxation threads: "<<mesher::sphereRelaxationThreads()
                 <<"\n";
        if(torusFixture&&hollowSphereFixture)
            throw std::invalid_argument("Choose either --torus or --hollow-sphere");
        if(torusFixture||hollowSphereFixture) {
            if(explicitSymmetric)
                throw std::invalid_argument("Randomized fixture does not have exact --symmetric topology; use spatially independent --asymmetric");
            if(avoidTestSheets||mergeTestTetra||mergeTestReflectedTetra||
               !recoverPath.empty()||allowMergeGeometry||demoMode||
               compareRemesh||compareLocalRemesh||compareMitigations)
                throw std::invalid_argument("--torus and --hollow-sphere require fresh interactive geometry, not recovery, demo, compare, or protected Merge");
            options.symmetric=false;
        }
        if(mergeTestTetra&&mergeTestReflectedTetra)
            throw std::invalid_argument("Choose only one experimental Merge fixture");
        if(mergeTestReflectedTetra&&(!allowMergeGeometry||!options.symmetric))
            throw std::invalid_argument(
                "--merge-test-reflected-tetra requires --merge-geometry --symmetric");
        if(mergeTestTetra&&(!allowMergeGeometry||options.symmetric))
            throw std::invalid_argument(
                "--merge-test-tetra requires explicit --merge-geometry --asymmetric");
        if(allowMergeGeometry&&(demoMode||compareLocalRemesh||
           compareRemesh||compareMitigations||!inflate))
            throw std::invalid_argument(
                "--merge-geometry only supports interactive, non-demo sculpt mode");
        if(static_cast<int>(repairCap)+static_cast<int>(repairZipper)+
           static_cast<int>(repairAuto)>1)
            throw std::invalid_argument("Select only one repair: cap, zipper or auto");
        if(classifierExplicit&&!repairAuto&&repairReplayPath.empty())
            throw std::invalid_argument(
                "--repair-classifier requires --repair auto, --repair-replay or --repair-replay-test");
        if((repairSeamAudit||repairOverlayExact)&&
           !repairAuto&&repairReplayPath.empty())
            throw std::invalid_argument(
                "--repair-seam-audit/--repair-overlay-exact require --repair auto or --repair-replay[-test]");
        if(repairEarClipping&&!repairAuto&&repairReplayPath.empty())
            throw std::invalid_argument("--repair-ear-clipping requires --repair auto or --repair-replay[-test]");
        if(repairUnifiedZipper&&!repairAuto&&repairReplayPath.empty())
            throw std::invalid_argument("--repair-unified-zipper requires --repair auto or --repair-replay[-test]");
        if(repairQualityWarn&&!repairAuto&&repairReplayPath.empty())
            throw std::invalid_argument("--repair-quality-warn requires --repair auto or --repair-replay[-test]");
        if(!repairCaseOut.empty()&&!repairAuto)
            throw std::invalid_argument("--repair-case-out requires --repair auto");
        if(repairFrontOptimize&&!repairAuto&&repairReplayPath.empty())
            throw std::invalid_argument(
                "--repair-front-optimize requires --repair auto or --repair-replay[-test]");
        if(repairDiagonalProbe&&!repairAuto&&repairReplayPath.empty())
            throw std::invalid_argument("--repair-diagonal-probe requires --repair auto or --repair-replay[-test]");
        if(repairFeasibilityProbe&&!repairAuto&&repairReplayPath.empty())
            throw std::invalid_argument("--repair-feasibility-probe requires --repair auto or --repair-replay[-test]");
        if(repairCurvaturePoisson&&!repairAuto&&repairReplayPath.empty())
            throw std::invalid_argument(
                "--repair-curvature-poisson requires --repair auto or --repair-replay[-test]");
        if(repairCurvatureOptimize&&!repairAuto&&repairReplayPath.empty())
            throw std::invalid_argument(
                "--repair-curvature-optimize requires --repair auto or --repair-replay[-test]");
        if(repairScaleAware&&!repairAuto&&repairReplayPath.empty())
            throw std::invalid_argument(
                "--repair-scale-aware requires --repair auto or --repair-replay[-test]");
        if(repairFeasibilityPreview&&(!repairStepDebug||repairReplayPath.empty()||repairReplayTest))
            throw std::invalid_argument("--repair-feasibility-preview requires interactive --repair-step-debug --repair-replay FILE");
        if(repairDiagonalPreview&&(!repairStepDebug||repairReplayPath.empty()||repairReplayTest))
            throw std::invalid_argument("--repair-diagonal-preview requires interactive --repair-step-debug --repair-replay FILE");
        if(repairWholeCapFairingPreview&&(!repairStepDebug||repairReplayPath.empty()||repairReplayTest))
            throw std::invalid_argument("--repair-whole-cap-fairing-preview requires interactive --repair-step-debug --repair-replay FILE");
        if(repairDiagonalPreview&&repairFeasibilityPreview)
            throw std::invalid_argument("--repair-diagonal-preview and --repair-feasibility-preview must be tested separately");
        if(repairStepDebug&&(repairReplayPath.empty()||repairReplayTest))
            throw std::invalid_argument(
                "--repair-step-debug requires interactive --repair-replay FILE (not --repair-replay-test)");
        if(!repairReplayPath.empty()&&(repairAuto||repairCap||repairZipper||repairInspect||
           repairPreview||repairExcise||!recoverPath.empty()||torusFixture||
           hollowSphereFixture||allowMergeGeometry||avoidCollisions||avoidTestSheets||
           mergeTestTetra||mergeTestReflectedTetra||demoMode||compareRemesh||
           compareLocalRemesh||compareMitigations))
            throw std::invalid_argument("--repair-replay is exclusive with live repair and mesh generation/recovery modes");
        if(repairDumpContours&&!repairAuto&&repairReplayPath.empty())
            throw std::invalid_argument("--repair-dump-contours requires --repair auto");
        // R07: --repair auto is LIVE by default. R01-R06 frozen
        // inspection is explicitly requested with --repair-inspect (or an
        // earlier preview/excision mode). The replay step debugger remains
        // independently opt-in and never owns a live sculpt worker.
        const bool liveCertifiedAuto=repairAuto&&!repairInspect&&
            !repairPreview&&!repairExcise;
        if(liveCertifiedAuto&&
           (repairQualityWarn||repairSeamAudit||repairOverlayExact||
            repairDumpContours||!repairCaseOut.empty()||
            repairScaleAware||repairFrontOptimize||
            repairCurvatureOptimize||repairCurvaturePoisson||
            repairFeasibilityProbe||repairDiagonalProbe||
            repairEarClipping))
            throw std::invalid_argument(
                "R07 live --repair auto does not accept frozen diagnostic flags; "
                "use --repair-inspect --repair-auto or --repair-replay");
        repairExcise=repairExcise||repairZipper||repairCap||
            (repairAuto&&!liveCertifiedAuto);
        repairPreview=repairPreview||repairExcise;
        repairInspect=repairInspect||repairPreview;
        if(repairInspect&&(allowMergeGeometry||avoidCollisions||demoMode||
           !inflate||compareLocalRemesh||compareRemesh||compareMitigations))
            throw std::invalid_argument(
                "--repair-inspect is exclusive with --merge-geometry/--avoid-collisions and requires interactive Inflate");
        auto desktopPolicy=mesher::desktopCollisionSettings({
            allowMergeGeometry,avoidCollisions,
            inflate&&!demoMode&&!compareRemesh&&
            !compareLocalRemesh&&!compareMitigations
        });
        // The live worker owns R06's complete certified private repair;
        // viewport callbacks never see its intermediate open R03 root.
        desktopPolicy.certifiedAutoClosure=liveCertifiedAuto;
        desktopPolicy.certifiedAutoUnifiedZipper=true;
        if(avoidTestSheets&&(!(avoidCollisions||repairInspect)||options.symmetric||
            !recoverPath.empty()||mergeTestTetra||
            mergeTestReflectedTetra))
            throw std::invalid_argument(
                "--avoid-test-sheets requires --avoid-collisions --asymmetric (or --repair-inspect --asymmetric), no --recover or protected Merge");
        if(avoidTestSheets&&
            (!inflate||demoMode||compareLocalRemesh||
             compareRemesh||compareMitigations))
            throw std::invalid_argument(
                "--avoid-test-sheets requires interactive Inflate mode");
        if(allowMergeGeometry&&!recoverPath.empty())
            throw std::invalid_argument(
                "--merge-geometry refuses V13 disk recovery without sealed first-original M29 authority");

        if(!repairReplayPath.empty()) {
            // Exact R01-approved contact evidence and entire immutable post-stamp
            // candidate are decoded BEFORE any Vulkan/GPU setup. The source
            // sculpt and its previous worker history are never synthesized.
            const auto saved=mesher::readRepairCase(repairReplayPath);
            if(repairStepDebug) {
                // M16D opt-in: Vulkan is fully initialized before the replay
                // worker starts. No private open stage enters the immutable
                // revision publisher, picking root, history or checkpoint.
                auto viewportMesh=mesher::flattenRevisionForViewport(
                    *saved.candidate.index,mesher::FlatMeshIndex{});
                if(viewportMesh.vertices.empty())
                    throw std::runtime_error("R06 stepped replay has no candidate");
                auto session=std::make_shared<mesher::RepairReplayStepSession>(
                    saved,repairClassifier,repairQualityWarn,
                    repairSeamAudit,repairOverlayExact,repairScaleAware,
                    repairFrontOptimize,repairCurvatureOptimize,
                    repairCurvaturePoisson,repairFeasibilityProbe,
                    repairDiagonalProbe,repairFeasibilityPreview,
                    repairDiagonalPreview,repairWholeCapFairingPreview,
                    repairEarClipping,repairUnifiedZipper);
                mesher::ViewportInteraction inspect;
                inspect.initialRevision=saved.candidate.index;
                inspect.canBegin=[]{return false;};
                inspect.completedStamps=[]{return std::size_t{0};};
                inspect.repairStepMode=true;
                inspect.onReady=[session] {session->start();};
                inspect.repairStepCommand=[session](int key) {
                    session->command(key);
                };
                inspect.takeSnapshot=[session] {return session->takeSnapshot();};
                inspect.caption=[session] {return session->caption();};
                std::cout<<"[repair-r06-m16d] Interactive exact replay: "
                            "N Next, P Previous, C Current, Enter Continue, "
                            "Backspace Cancel, PgUp/PgDn scroll arguments; "
                            "orbit/W/G and Escape exit. Private only.\n";
                mesher::runViewport(viewportMesh,{},std::move(inspect));
                session->cancelAndJoin(); // also handled by destructor on throw
                return 0;
            }
            const auto frozen=mesher::replayPrivateAutomaticRepair(
                saved,repairClassifier,repairQualityWarn,
                repairSeamAudit,repairOverlayExact,{},repairScaleAware,
                repairFrontOptimize,repairCurvatureOptimize,
                    repairCurvaturePoisson,repairFeasibilityProbe,
                    repairDiagonalProbe,false,false,false,
                    repairEarClipping,repairUnifiedZipper);
            const bool succeeded=frozen->privateAuto.has_value();
            std::cout<<"[repair-r06] CLASSIFIERS area="<<frozen->classifierChoices[0]
                     <<" proximity="<<frozen->classifierChoices[1]
                     <<" comparable="<<frozen->classifierComparable
                     <<" agree="<<frozen->classifiersAgree
                     <<" active="<<mesher::autoClosureClassifierName(repairClassifier)
                     <<" (agreement is not correctness or M04 completion)\n";
            const auto chosenCaps=frozen->autoPlan?
                frozen->autoPlan->cappedContours.size():0;
            const auto chosenZippers=frozen->autoPlan?
                frozen->autoPlan->zipperPairs.size():0;
            const auto choice=chosenZippers?
                (chosenCaps?"mixed":"zipper"):
                (chosenCaps?"caps":"none");
            const bool intentMatches=saved.intendedSelection==
                mesher::RepairCaseIntent::Unspecified||
                (saved.intendedSelection==mesher::RepairCaseIntent::PreferZipper?
                    chosenZippers>0:(chosenCaps>0&&chosenZippers==0));
            std::cout<<"[repair-r06] REPLAY file="<<repairReplayPath.string()
                     <<" classifier="<<mesher::autoClosureClassifierName(repairClassifier)
                     <<" expected="<<(saved.expectedClosure?"AUTO_CLOSED":"AUTO_REFUSED")
                     <<" actual="<<(succeeded?"AUTO_CLOSED":"AUTO_REFUSED")
                     <<" expected_choice="<<mesher::repairCaseIntentName(saved.intendedSelection)
                     <<" actual_choice="<<choice
                     <<" choice_match="<<intentMatches
                     <<" contacts="<<saved.contacts.size()
                     <<" symmetric_source="<<saved.symmetricSource
                     <<" exact_candidate=1 worker_history_checkpoint_unchanged=1\n";
            std::cout<<"[repair-r06] RECORDED original_reason="
                     <<saved.originalDiagnostic<<"\n";
            if(frozen->autoPlan) {
                const auto& plan=*frozen->autoPlan;
                std::cout<<"[repair-r06] PLAN status="
                         <<static_cast<int>(plan.status)
                         <<" regions="<<plan.geometryRegions
                         <<" caps="<<plan.cappedContours.size()
                         <<" zipper_pairs="<<plan.zipperPairs.size()
                         <<" contact_links="<<plan.trustedContactLinks<<"\n";
                for(std::size_t i=0;i<plan.regions.size();++i) {
                    const auto& region=plan.regions[i];
                    std::cout<<"[repair-r06] region="<<i
                             <<" caps="<<region.caps.size()
                             <<" zippers="<<region.zippers.size()
                             <<" contact_links="<<region.contactLinks
                             <<" rim_metrics_valid="<<region.twoRimEvidenceValid
                             <<" centroid_gap="<<region.rimCentroidSeparation
                             <<" normals_dot="<<region.rimNormalAlignment
                             <<" nearest_gap_in_edges="<<region.rimNearestSeparationInEdges
                             <<" cap_area_estimate="<<region.estimatedCapArea
                             <<" zipper_area_estimate="<<region.estimatedZipperArea
                             <<" selection="<<region.selectionReason<<"\n";
                }
            }
            if(succeeded) {
                const auto& completed=*frozen->privateAuto;
                std::cout<<"[repair-r06] AUTO_CLOSED private_closed_mesh=1"
                         <<" cap_loops="<<(frozen->autoPlan?
                             frozen->autoPlan->cappedContours.size():0)
                         <<" zipper_pairs="<<(frozen->autoPlan?
                             frozen->autoPlan->zipperPairs.size():0)
                         <<" green_faces="<<completed.greenFaces.size()
                         <<" blue_faces="<<completed.blueFaces.size()
                         <<" min_cap_quality="<<completed.minimumCapQuality
                         <<" min_zipper_quality="<<completed.minimumZipperQuality
                         <<" refinement_steps="<<completed.refinementSteps
                         <<" interior_quality_flips="<<completed.interiorQualityFlips
                         <<" interior_fairing_moves="<<completed.interiorFairingMoves
                         <<" interior_cap_collapses="<<completed.interiorCapCollapses
                         <<" proven_rim_tip_exempt_faces="<<completed.certifiedPlanarRimTipFaces
                         <<" inserted_vertices="<<completed.insertedVertices
                         <<" mirrored_ribbon_loops="<<completed.mirroredRibbonLoops
                         <<" unpublished=1 worker_history_checkpoint_unchanged=1\n";
            }else {
                std::cout<<"[repair-r06] AUTO_REFUSED private_closed_mesh=0"
                         <<" reason="<<frozen->autoDiagnostic
                         <<" (R03 cut retained where available; unpublished=1)\n";
            }
            if(repairQualityWarn) {
                if(const auto& preview=frozen->warningPreview) {
                    std::cout<<"[repair-r06] QUALITY_WARNING_PREVIEW untrusted=1"
                             <<" private_closed_mesh=0"
                             <<" deficient_faces="<<preview->qualityWarningFaces.size()
                             <<" green_faces="<<preview->greenFaces.size()
                             <<" blue_faces="<<preview->blueFaces.size()
                             <<" first_warning="<<preview->qualityWarningReason
                             <<" unpublished=1 worker_history_checkpoint_unchanged=1\n";
                }else if(!frozen->warningPreviewDiagnostic.empty())
                    std::cout<<"[repair-r06] QUALITY_WARNING_UNAVAILABLE reason="
                             <<frozen->warningPreviewDiagnostic<<"\n";
            }
            if(repairSeamAudit) {
                const auto mesh=frozen->privateAuto?
                    frozen->privateAuto->index:
                    frozen->warningPreview?frozen->warningPreview->index:nullptr;
                if(frozen->seamAudit&&mesh)
                    mesher::printRepairSeamAudit(std::cout,*mesh,
                        *frozen->seamAudit,
                        succeeded?"CERTIFIED_PRIVATE":"UNTRUSTED_WARNING_PREVIEW");
                if(frozen->seamProvenance)
                    mesher::printRepairSeamProvenance(
                        std::cout,*frozen->seamProvenance);
                else
                    std::cout<<"[repair-r06-seam] UNAVAILABLE reason="
                             <<frozen->seamAuditDiagnostic<<"\n";
            }
            if(repairDumpContours&&frozen->privateCut) {
                const auto& open=*frozen->privateCut->index;
                for(std::size_t i=0;i<frozen->excision.contours.size();++i) {
                    const auto& rim=frozen->excision.contours[i];
                    for(std::size_t j=0;j<rim.vertexIds.size();++j) {
                        const auto vertex=rim.vertexIds[j];
                        const auto xyz=open.vertex(vertex).position;
                        std::cout<<std::setprecision(17)
                                 <<"[repair-r06-contour] loop="<<i
                                 <<" ordinal="<<j<<" id="<<vertex
                                 <<" xyz="<<xyz.x<<","<<xyz.y<<","<<xyz.z
                                 <<" surviving_face="<<rim.survivingFaces.at(j)<<"\n";
                    }
                }
            }
            if(repairReplayTest) {
                const bool outcomeMatches=succeeded==saved.expectedClosure;
                std::cout<<"[repair-r06] REPLAY_TEST "
                         <<(!outcomeMatches?"OUTCOME_MISMATCH":
                            !intentMatches?"CHOICE_MISMATCH":"PASS")
                         <<" expected_choice="<<mesher::repairCaseIntentName(
                             saved.intendedSelection)
                         <<" actual_choice="<<choice
                         <<" stored_reason="<<saved.originalDiagnostic<<"\n";
                return !outcomeMatches?2:(!intentMatches?3:0);
            }
            auto viewportMesh=mesher::flattenRevisionForViewport(
                *saved.candidate.index,mesher::FlatMeshIndex{});
            if(viewportMesh.vertices.empty())
                throw std::runtime_error("R06 replay candidate has no displayable mesh");
            mesher::ViewportInteraction inspect;
            inspect.initialRevision=saved.candidate.index;
            inspect.canBegin=[]{return false;};
            inspect.undo=[]{};
            inspect.redo=[]{};
            inspect.checkpoint=[]{};
            inspect.completedStamps=[]{return std::size_t{0};};
            inspect.caption=[succeeded] {
                return std::string("R06 exact case replay FROZEN | ")+
                       (succeeded?"AUTO COMPLETE":"AUTO REFUSED");
            };
            inspect.takeSnapshot=[frozen,already=false]()
                mutable -> std::optional<mesher::ViewportMeshUpdate> {
                if(already)return std::nullopt;
                already=true;
                auto display=frozen->privateAuto?
                    frozen->privateAuto->index:
                    frozen->warningPreview?frozen->warningPreview->index:
                    frozen->privateCut?frozen->privateCut->index:frozen->candidate;
                std::set<mesher::PartitionId> touched;
                frozen->candidate->revision().forEachPartition(
                    [&](mesher::PartitionId id,const mesher::MeshPartition&) {
                        touched.insert(id);
                    });
                display->revision().forEachPartition(
                    [&](mesher::PartitionId id,const mesher::MeshPartition&) {
                        touched.insert(id);
                    });
                auto update=mesher::ViewportMeshUpdate{
                    display,{touched.begin(),touched.end()},0,true,
                    mesher::buildRepairInspectionOverlay(*frozen),
                    true,true,frozen->privateCut.has_value(),
                    false,false,false,false,true,
                    frozen->privateAuto.has_value()};
                update.inspectionQualityWarningPreview=frozen->warningPreview.has_value();
                update.classifierChoices=frozen->classifierChoices;
                update.classifierComparable=frozen->classifierComparable;
                update.classifiersAgree=frozen->classifiersAgree;
                update.activeClassifier=
                    frozen->activeClassifier==mesher::AutoClosureClassifier::Proximity?1:0;
                return update;
            };
            std::cout<<"[repair-r06] Opened read-only replay viewport; orbit/W/G, Escape to exit.\n";
            mesher::runViewport(viewportMesh,{},std::move(inspect));
            return 0;
        }
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
        }else if(avoidTestSheets) {
            mesh=mesher::desktopOffModeNearSheetsFixture();
            options.vertexCount=static_cast<std::uint32_t>(mesh.vertices.size());
            std::cout<<"[collision] M07 developer near-sheet fixture: "
                     <<mesh.vertices.size()<<" stable vertices, "
                     <<mesh.triangles.size()<<" independently owned faces; "
                        "separate close sheets near origin and safe remote face at X=3.5. "
                        "No protected Merge source is authorized.\n";
        }else if(torusFixture) {
            std::cout<<"[fixture] Constructing torus R=0.55 r=0.45 (hole radius 0.10); "
                     <<options.vertexCount<<" random area-stratified vertices, "
                     <<options.relaxationIterations<<" relaxation passes.\n";
            mesh=mesher::generateTorusFixture(options.vertexCount,options.seed,
                                             options.relaxationIterations);
            std::cout<<"[fixture] TORUS topology: "
                     <<mesh.vertices.size()<<" vertices, "
                     <<mesh.triangles.size()<<" triangles, connected genus 1. "
                     <<"Independent (non-mirrored) mesh topology.\n";
        }else if(hollowSphereFixture) {
            std::cout<<"[fixture] Constructing hollow shell; outer R=1, "
                     <<"inner R=0.95, negative-Y mouth r=0.1, "
                     <<"tube r=0.095 at Y=-0.96 and Y=-0.93.\n";
            mesh=mesher::generateHollowSphereFixture(
                options.vertexCount,options.seed,options.relaxationIterations);
            options.vertexCount=static_cast<std::uint32_t>(mesh.vertices.size());
            std::cout<<"[fixture] HOLLOW SHELL topology: "
                     <<mesh.vertices.size()<<" vertices after cut/connector, "
                     <<mesh.triangles.size()<<" triangles; one connected "
                     <<"closed genus-0 surface with an accessible inner wall. "
                     <<"Independent (non-mirrored) mesh topology.\n";
        }else if(mergeTestReflectedTetra) {
            mesh=mesher::desktopMergeReflectedTetraFixture();
            options.vertexCount=static_cast<std::uint32_t>(mesh.vertices.size());
            std::cout<<"M51 sealed reflected desktop fixture: "
                     <<mesh.vertices.size()<<" independently owned original vertices, "
                     <<mesh.triangles.size()<<" faces; two reciprocal source-positive "
                     <<"groups. Merge starts OFF.\n";
        }else if(mergeTestTetra) {
            // Deliberate, separately authorized supported-domain source;
            // never constructed in a normal interactive desktop launch.
            // The ordinary protected worker below independently verifies
            // complete contacts, exact old-root identity and source proof.
            mesh=mesher::desktopMergeAsymmetricTetraFixture();
            options.vertexCount=static_cast<std::uint32_t>(mesh.vertices.size());
            std::cout<<"M50 experimental desktop fixture: "
                     <<mesh.vertices.size()<<" independent original vertices, "
                     <<mesh.triangles.size()<<" faces, two contacting asymmetric "
                     <<"tetrahedral shells. Merge selection remains OFF.\n";
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
        std::vector<mesher::FaceId> fixtureFirstFaces;
        // The lambdas in ViewportInteraction outlive the setup block.
        std::shared_ptr<const mesher::RegionTopologyIndex> lastDisplayed;
        bool inspectionPresented=false;
        auto repairSession=repairInspect
            ?std::make_shared<mesher::RepairInspectionSession>(
                repairPreview,repairExcise,repairZipper,repairCap,
                repairAuto,options.symmetric,repairClassifier,repairQualityWarn,
                repairSeamAudit,repairOverlayExact,repairScaleAware,
                repairFrontOptimize,repairCurvatureOptimize,
                repairCurvaturePoisson,repairFeasibilityProbe,
                repairDiagonalProbe,repairEarClipping,repairUnifiedZipper):nullptr;
        const mesher::SculptStepValidator repairValidator=repairSession
            ?mesher::SculptStepValidator{
                [repairSession](const mesher::ProposedSculptStep& proposed) {
                    return repairSession->inspect(proposed);
                }}:mesher::SculptStepValidator{};

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
                    curvatureSettings,collapseSettings,refineSettings,
                    repairValidator,desktopPolicy);
            else {
                const auto mergePolicy=desktopPolicy;
                if(mergeTestReflectedTetra) {
                    // The ordinary symmetric sphere constructor cannot
                    // authenticate this separate pair of original tetra
                    // components. Use the independently source-sealed
                    // original indexed root with exact stable reciprocal IDs.
                    auto source=mesher::desktopMergeReflectedFirstOriginal();
                    fixtureFirstFaces=source.firstFaces;
                    liveWorker=std::make_unique<mesher::AsyncSculptWorker>(
                        std::move(source.checkpoint),step,strength,timeSpeed,
                        curvatureSettings,collapseSettings,refineSettings,
                        repairValidator,mergePolicy,
                        std::move(source.firstFaces));
                } else {
                    // The global OFF startup remains bit-for-bit the previous
                    // legacy worker construction. Only --merge-geometry
                    // authorizes the sealed first-original worker.
                    if(mergeTestTetra)
                        fixtureFirstFaces=mesher::partitionFlatMesh(mesh,256)
                                              .flatIndex.faceIds;
                    liveWorker=std::make_unique<mesher::AsyncSculptWorker>(
                        mesh,step,strength,timeSpeed,curvatureSettings,
                        collapseSettings,refineSettings,options.symmetric,
                        repairValidator,mergePolicy);
                }
            }
            if(avoidTestSheets)
                interaction.cameraScale=3.5; // covers near and safe remote face
            if(mergeTestTetra||mergeTestReflectedTetra) {
                interaction.mergeFixtureOriginal=liveWorker->initialRevision();
                interaction.mergeFixtureOriginals=std::move(fixtureFirstFaces);
                // Keep the positive source actually on-screen for targeted
                // rendered acceptance. Default unit-sphere camera is
                // untouched; both Vulkan projection and CPU rays scale.
                interaction.cameraScale=mergeTestReflectedTetra?4.0:2.0;
            }
            if(avoidCollisions) {
                interaction.collisionStatus=[&] {
                    const auto status=liveWorker->status();
                    const auto& bt=status.stepResources.backtracking;
                    std::ostringstream out;
                    out<<"ON R "<<status.rejectedStamps
                       <<" A "<<status.completedStamps
                       <<" P "<<status.publishedRevisions
                       <<" trials "<<bt.trials
                       <<" fraction "<<bt.chosenNormalFraction;
                    if(status.terminalFailure)
                        out<<" TERMINAL "<<status.terminalFailureReason;
                    else if(status.stepResources.refused)
                        out<<" REFUSED "<<status.lastRejection;
                    return out.str();
                };
            }
            if(allowMergeGeometry) {
                interaction.mergeSelected=[&](
                    std::shared_ptr<const mesher::RegionTopologyIndex> root,
                    std::vector<mesher::FaceId> nominated) {
                    liveWorker->mergeDiscoveredContacts(
                        std::move(root),std::move(nominated),
                        mesher::desktopMergeExactLimits(options.symmetric));
                };
                interaction.mergeStatus=[&] {
                    const auto status=liveWorker->status();
                    const auto pending=status.pendingLiveMergeRequests;
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
            interaction.undo=[&]{if(!repairSession||!repairSession->frozen())liveWorker->undo();};
            interaction.redo=[&]{if(!repairSession||!repairSession->frozen())liveWorker->redo();};
            interaction.checkpoint=[&] {
                // The frozen branch uses a distinct, outcome-aware callback.
                // Never checkpoint unpublished automatic repair geometry.
                if(repairSession&&repairSession->frozen())return;
                liveWorker->saveCheckpoint(checkpointPath);
                std::cout<<"[checkpoint] queued "<<checkpointPath.string()<<"\n";
            };
            interaction.frozenRepairCapture=[&](int requestedIntent) {
                if(!repairAuto||!repairSession||!repairSession->frozen())return;
                const auto frozen=repairSession->frozenSnapshot();
                if(!frozen||!frozen->candidate||frozen->contacts.empty()) {
                    std::cerr<<"[repair-r06] CASE_SAVE_REFUSED no frozen candidate / complete contacts\n";
                    return;
                }
                try {
                    if(requestedIntent<0||requestedIntent>2)
                        throw std::invalid_argument("Invalid requested classification annotation");
                    const auto intent=static_cast<mesher::RepairCaseIntent>(requestedIntent);
                    const bool completed=frozen->privateAuto.has_value();
                    const auto stem=intent==mesher::RepairCaseIntent::PreferZipper?
                        "classification-zipper":
                        intent==mesher::RepairCaseIntent::PreferCaps?
                        "classification-caps":
                        completed?"closure":"refusal";
                    const auto destination=repairCaseOut.empty()?
                        mesher::uniqueRepairCasePath("repair-cases",stem):repairCaseOut;
                    mesher::RepairCase capture;
                    capture.candidate.index=frozen->candidate;
                    capture.symmetricSource=options.symmetric;
                    capture.contacts=frozen->contacts;
                    capture.expectedClosure=completed;
                    capture.intendedSelection=intent;
                    // Preserve the ORIGINAL M03 classification explanation
                    // as human-readable archive evidence. A later executable
                    // may have different decision thresholds and cannot
                    // reconstruct what the old planner actually reasoned.
                    std::ostringstream original;
                    original<<(completed?"R06 AUTO_CLOSED":"R06 AUTO_REFUSED");
                    if(frozen->autoPlan) {
                        const auto& plan=*frozen->autoPlan;
                        original<<" caps="<<plan.cappedContours.size()
                                <<" zippers="<<plan.zipperPairs.size();
                        for(std::size_t i=0;i<plan.regions.size();++i) {
                            const auto& region=plan.regions[i];
                            original<<" region["<<i<<"] caps="
                                    <<region.caps.size()<<" zippers="
                                    <<region.zippers.size()<<" selection="
                                    <<region.selectionReason;
                        }
                    }
                    if(!completed)original<<" refusal="<<frozen->autoDiagnostic;
                    capture.originalDiagnostic=original.str();
                    mesher::writeRepairCase(destination,capture);
                    std::cout<<"[repair-r06] CASE_SAVED "<<destination.string()
                             <<" original="<<(completed?"AUTO_CLOSED":"AUTO_REFUSED")
                             <<" desired_choice="<<mesher::repairCaseIntentName(intent)
                             <<" exact_candidate=1 contacts="<<capture.contacts.size()
                             <<" unpublished=1 worker_history_checkpoint_unchanged=1\n";
                }catch(const std::exception& error) {
                    std::cerr<<"[repair-r06] CASE_SAVE_REFUSED "<<error.what()<<"\n";
                }
            };
            interaction.radiusRange=brushRange;
            interaction.radius=brushRadius;
            interaction.verboseInput=verboseInput;
            interaction.setRadius=[&,brushRange](double radius) {
                brushRadius=brushRange.clamp(radius);
                liveWorker->setRadius(brushRadius);
            };
            interaction.begin=[&](mesher::Vec3 point) {
                if(repairSession&&repairSession->frozen())return;
                liveWorker->begin(point,brushRadius);
            };
            interaction.update=[&](mesher::Vec3 point,double elapsed) {
                liveWorker->update(point,elapsed);
            };
            interaction.end=[&](mesher::Vec3 point,double elapsed) {
                liveWorker->end(point,elapsed);
            };
            interaction.initialRevision=liveWorker->initialRevision();
            // R01 preview is NOT a published worker root. Preserve its
            // immutable source separately from ordinary live snapshots and
            // invalidate all visible partition owners once at freeze.
            lastDisplayed=liveWorker->initialRevision();
            interaction.takeSnapshot=[&]() -> std::optional<mesher::ViewportMeshUpdate> {
                if(repairSession)if(const auto frozen=repairSession->frozenSnapshot()) {
                    if(inspectionPresented)return std::nullopt;
                    inspectionPresented=true;
                    std::set<mesher::PartitionId> owners;
                    if(lastDisplayed)lastDisplayed->revision().forEachPartition(
                        [&](mesher::PartitionId id,const mesher::MeshPartition&) {
                            owners.insert(id);
                        });
                    frozen->candidate->revision().forEachPartition(
                        [&](mesher::PartitionId id,const mesher::MeshPartition&) {
                            owners.insert(id);
                        });
                    std::cout<<(repairAuto?"[repair-r06] ":(repairCap?"[repair-r05] ":(repairZipper?"[repair-r04] ":(repairExcise?"[repair-r03] ":(repairPreview?"[repair-r02] ":"[repair-r01] ")))))
                             <<"FROZEN candidate state="
                             <<(frozen->diagnostic.empty()?"CONTACT":"QUERY_INCOMPLETE")
                             <<" contacts="<<frozen->contacts.size()
                             <<" faces="<<frozen->affectedFaces.size()
                             <<" incident_vertices="<<frozen->incidentVertices.size()
                             <<" incident_edges="<<frozen->incidentEdges.size()
                             <<" seed_faces="<<frozen->work.seedFaces
                             <<" box_tests="<<frozen->work.boxesTested
                             <<" partition_tests="<<frozen->work.partitionsTested
                             <<" candidate_triangles="<<frozen->work.candidateTriangles
                             <<" narrow_tests="<<frozen->work.narrowPhaseTests
                             <<" diagnostic="<<(frozen->diagnostic.empty()?"none":frozen->diagnostic)
                             <<" (no CAS/history/checkpoint publication)\n";
                    for(const auto& pair:frozen->contacts)
                        std::cout<<"[repair-r01] FaceId "<<pair.first
                                 <<" x "<<pair.second
                                 <<(pair.ambiguous?" AMBIGUOUS":" CONTACT")<<"\n";
                    if(repairPreview) {
                        const auto& preview=frozen->excision;
                        std::cout<<"[repair-r02] PREVIEW proposed_vertices="<<preview.proposedVertices.size()
                                  <<" proposed_faces="<<preview.proposedFaces.size()
                                  <<" expanded_boundary_vertices="<<preview.expandedBoundaryVertices.size()
                                  <<" orphan_vertices="<<preview.newlyOrphanedVertices.size()
                                  <<" boundary_repairs="<<preview.boundaryRepairs
                                 <<" uncertain_faces="<<preview.uncertainFaces.size()
                                 <<" mixed_faces="<<preview.mixedFaces.size()
                                 <<" contact_islands="<<preview.contactIslands
                                 <<" excision_components="<<preview.excisionComponents
                                 <<" boundary_loops="<<preview.contours.size()
                                 <<" boundary_edges="<<preview.boundaryEdges
                                 <<" classified_faces="<<preview.classifiedFaces
                                  <<" classification_advisory="<<(preview.classificationAdvisory.empty()?
                                      "none":preview.classificationAdvisory)
                                 <<" complete="<<preview.complete
                                 <<" reason="<<(preview.diagnostic.empty()?"none":preview.diagnostic)
                                 <<" closure_advisory="<<(preview.closureAdvisory.empty()?
                                    "none":preview.closureAdvisory)
                                 <<" (R02 plan only; no faces published)\n";
                        for(std::size_t i=0;i<preview.contours.size();++i)
                            std::cout<<"[repair-r02] loop="<<i
                                     <<" vertices="<<preview.contours[i].vertexIds.size()
                                     <<" perimeter="<<preview.contours[i].perimeter<<"\n";
                        for(const auto id:preview.proposedVertices)
                             std::cout<<"[repair-r02] retire VertexId "<<id<<"\n";
                         for(const auto id:preview.proposedFaces)
                            std::cout<<"[repair-r02] remove FaceId "<<id<<"\n";
                    }
                    if(repairExcise) {
                        if(const auto& cut=frozen->privateCut) {
                            std::cout<<"[repair-r03] EXCISED private_open_mesh=1"
                                     <<" removed_vertices="<<cut->removedVertices
                                     <<" removed_faces="<<cut->removedFaces
                                     <<" modified_partitions="<<cut->modifiedPartitions
                                     <<" surviving_faces="<<cut->survivingFaces
                                     <<" open_boundary_edges="<<cut->boundaryEdges
                                     <<" open_boundary_loops="<<cut->boundaryLoops
                                     <<" no_caps=1 no_zippers=1"
                                     <<" unpublished=1 (worker/history/checkpoint unchanged)\n";
                        }else {
                            std::cout<<"[repair-r03] EXCISION_REFUSED private_open_mesh=0"
                                     <<" reason="<<(frozen->excisionDiagnostic.empty()?
                                        "no complete approved cut":frozen->excisionDiagnostic)
                                     <<" (original candidate retained, unpublished)\n";
                        }
                    }
                    if(repairZipper) {
                        if(const auto& zipper=frozen->privateZipper) {
                            std::cout<<"[repair-r04] ZIPPED forced=1 private_closed_mesh=1"
                                     <<" zipper_pairs="<<zipper->matchedLoops.size()
                                     <<" blue_faces="<<zipper->blueFaces.size()
                                     <<" inserted_vertices="<<zipper->insertedVertices.size()
                                     <<" new_faces="<<zipper->totalNewFaces
                                     <<" boundary_splits="<<zipper->splitBoundaryEdges
                                     <<" refinement_steps="<<zipper->refinementSteps
                                     <<" min_seam_quality="<<zipper->minimumSeamQuality
                                     <<" no_caps=1 unpublished=1\n";
                            for(const auto& pair:zipper->matchedLoops)
                                std::cout<<"[repair-r04] pair "<<pair.first
                                         <<" -> "<<pair.second<<"\n";
                        }else {
                            std::cout<<"[repair-r04] ZIPPER_REFUSED private_closed_mesh=0"
                                     <<" reason="<<frozen->zipperDiagnostic
                                     <<" (R03 cut retained where available; no publication)\n";
                        }
                    }
                    if(repairCap) {
                        if(const auto& cap=frozen->privateCap) {
                            std::cout<<"[repair-r05] CAPPED forced=1 private_closed_mesh=1"
                                     <<" independent_caps="<<cap->cappedLoops
                                     <<" green_faces="<<cap->greenFaces.size()
                                     <<" inserted_vertices="<<cap->insertedVertices.size()
                                     <<" new_faces="<<cap->totalNewFaces
                                     <<" refinement_steps="<<cap->refinementSteps
                                     <<" min_cap_quality="<<cap->minimumCapQuality
                                     <<" no_zippers=1 unpublished=1\n";
                        }else {
                            std::cout<<"[repair-r05] CAP_REFUSED private_closed_mesh=0"
                                     <<" reason="<<frozen->capDiagnostic
                                     <<" (R03 cut retained where available; no publication)\n";
                        }
                    }
                    if(repairAuto&&repairSeamAudit) {
                        const auto mesh=frozen->privateAuto?
                            frozen->privateAuto->index:
                            frozen->warningPreview?frozen->warningPreview->index:nullptr;
                        if(frozen->seamAudit&&mesh)
                            mesher::printRepairSeamAudit(std::cout,*mesh,
                                *frozen->seamAudit,
                                frozen->privateAuto?"CERTIFIED_PRIVATE":
                                    "UNTRUSTED_WARNING_PREVIEW");
                        if(frozen->seamProvenance)
                            mesher::printRepairSeamProvenance(
                                std::cout,*frozen->seamProvenance);
                        else
                            std::cout<<"[repair-r06-seam] UNAVAILABLE reason="
                                     <<frozen->seamAuditDiagnostic<<"\n";
                    }
                    if(repairAuto&&repairDumpContours&&frozen->privateCut) {
                        // An opt-in exact loop-coordinate capture makes
                        // an otherwise one-shot physical folded contact
                        // reproducible as a bounded CPU cap fixture.
                        const auto& open=*frozen->privateCut->index;
                        for(std::size_t i=0;i<frozen->excision.contours.size();++i) {
                            const auto& rim=frozen->excision.contours[i];
                            for(std::size_t j=0;j<rim.vertexIds.size();++j) {
                                const auto id=rim.vertexIds[j];
                                const auto xyz=open.vertex(id).position;
                                std::ostringstream row;
                                row<<std::setprecision(17)
                                   <<"[repair-r06-contour] loop="<<i
                                   <<" ordinal="<<j
                                   <<" id="<<id
                                   <<" xyz="<<xyz.x<<","<<xyz.y<<","<<xyz.z
                                   <<" surviving_face="<<rim.survivingFaces.at(j);
                                std::cout<<row.str()<<"\n";
                            }
                        }
                    }
                    if(repairAuto)
                        std::cout<<"[repair-r06] CLASSIFIERS area="
                                 <<frozen->classifierChoices[0]
                                 <<" proximity="<<frozen->classifierChoices[1]
                                 <<" comparable="<<frozen->classifierComparable
                                 <<" agree="<<frozen->classifiersAgree
                                 <<" active="<<mesher::autoClosureClassifierName(
                                     frozen->activeClassifier)
                                 <<" (M03 only, no correctness claim)\n";
                    if(repairAuto&&frozen->autoPlan) {
                        const auto& plan=*frozen->autoPlan;
                        std::cout<<"[repair-r06] PLAN status="<<static_cast<int>(plan.status)
                                 <<" reason="<<(plan.diagnostic.empty()?"none":plan.diagnostic)
                                 <<" regions="<<plan.geometryRegions
                                 <<" caps="<<plan.cappedContours.size()
                                 <<" zipper_pairs="<<plan.zipperPairs.size()
                                 <<" contact_links="<<plan.trustedContactLinks<<"\n";
                        for(std::size_t i=0;i<plan.regions.size();++i) {
                            const auto& region=plan.regions[i];
                            std::cout<<"[repair-r06] region="<<i
                                     <<" caps="<<region.caps.size()
                                     <<" zippers="<<region.zippers.size()
                                     <<" patches="<<region.excisionPatches.size()
                                     <<" contacts="<<region.contactLinks
                                     <<" zero_rim="<<region.zeroLoopComponentRetirement
                                     <<" estimated_area="<<region.estimatedAddedArea
                                     <<" selection="<<region.selectionReason
                                     <<" ambiguous_cap_fallback="<<region.usedAmbiguousCapFallback
                                     <<" rim_metrics_valid="<<region.twoRimEvidenceValid
                                     <<" centroid_gap="<<region.rimCentroidSeparation
                                     <<" normals_dot="<<region.rimNormalAlignment
                                     <<" nearest_gap_in_edges="<<region.rimNearestSeparationInEdges
                                     <<" cap_area_estimate="<<region.estimatedCapArea
                                     <<" zipper_area_estimate="<<region.estimatedZipperArea
                                     <<"\n";
                        }
                    }
                    if(repairAuto) {
                        if(const auto& preview=frozen->warningPreview) {
                            std::cout<<"[repair-r06] QUALITY_WARNING_PREVIEW"
                                     <<" untrusted=1 private_closed_mesh=0"
                                     <<" deficient_faces="<<preview->qualityWarningFaces.size()
                                     <<" green_faces="<<preview->greenFaces.size()
                                     <<" blue_faces="<<preview->blueFaces.size()
                                     <<" first_warning="<<preview->qualityWarningReason
                                     <<" unpublished=1\n";
                        }else if(repairQualityWarn&&!frozen->warningPreviewDiagnostic.empty())
                            std::cout<<"[repair-r06] QUALITY_WARNING_UNAVAILABLE reason="
                                     <<frozen->warningPreviewDiagnostic<<"\n";
                        if(const auto& automatic=frozen->privateAuto) {
                            const auto& plan=*frozen->autoPlan;
                            std::cout<<"[repair-r06] AUTO_CLOSED private_closed_mesh=1"
                                     <<" regions="<<plan.geometryRegions
                                     <<" cap_loops="<<plan.cappedContours.size()
                                     <<" zipper_pairs="<<plan.zipperPairs.size()
                                     <<" green_faces="<<automatic->greenFaces.size()
                                     <<" blue_faces="<<automatic->blueFaces.size()
                                     <<" retired_cavity_components="<<automatic->retiredCavityComponents
                                     <<" retired_cavity_faces="<<automatic->retiredCavityFaces.size()
                                     <<" retired_cavity_vertices="<<automatic->retiredCavityVertices.size()
                                     <<" exterior_components="<<automatic->acceptedExteriorComponents
                                     <<" post_closure_faces="<<automatic->postClosureFaces
                                     <<" final_faces="<<automatic->postRetirementFaces
                                     <<" trusted_contact_links="<<plan.trustedContactLinks
                                     <<" estimated_area="<<plan.estimatedAddedArea
                                     <<" generated_faces="<<automatic->generatedFaces
                                     <<" inserted_vertices="<<automatic->insertedVertices
                                     <<" refinement_steps="<<automatic->refinementSteps
                                     <<" interior_quality_flips="<<automatic->interiorQualityFlips
                                     <<" interior_fairing_moves="<<automatic->interiorFairingMoves
                                     <<" interior_cap_collapses="<<automatic->interiorCapCollapses
                                     <<" proven_rim_tip_exempt_faces="<<automatic->certifiedPlanarRimTipFaces
                                     <<" min_cap_quality="<<automatic->minimumCapQuality
                                     <<" min_zipper_quality="<<automatic->minimumZipperQuality
                                     <<" spatial_cap_loops="<<automatic->spatialCapLoops
                                     <<" mirrored_ribbon_loops="<<automatic->mirroredRibbonLoops
                                     <<" mirrored_seam_zipper_pairs="<<automatic->mirroredSeamZipperPairs
                                     <<" mirrored_seam_vertices="<<automatic->mirroredSeamVertices
                                     <<" spatial_poisson_iterations="<<automatic->spatialPoissonIterations
                                     <<" spatial_initial_min_quality="<<automatic->minimumSpatialInitialQuality
                                     <<" spatial_density_splits="<<automatic->spatialDensitySplits
                                     <<" spatial_density_remaining_edges="<<automatic->spatialDensityRemainingLongEdges
                                     <<" spatial_max_final_edge_ratio="<<automatic->maximumFinalSpatialCapEdgeRatio
                                     <<" enclosure_winding_visits="<<automatic->windingTriangleVisits
                                     <<" enclosure_narrow_tests="<<automatic->collisionNarrowPhaseTests
                                     <<" unpublished=1 worker_history_checkpoint_unchanged=1\n";
                        }else {
                            std::cout<<"[repair-r06] AUTO_REFUSED private_closed_mesh=0"
                                     <<" plan_status="<<(frozen->autoPlan?
                                         static_cast<int>(frozen->autoPlan->status):-1)
                                     <<" selected_caps="<<(frozen->autoPlan?
                                         frozen->autoPlan->cappedContours.size():0)
                                     <<" selected_zippers="<<(frozen->autoPlan?
                                         frozen->autoPlan->zipperPairs.size():0)
                                     <<" reason="<<frozen->autoDiagnostic
                                     <<" (R03 cut retained where available; unpublished=1)\n";
                        }
                    }
                    const auto displayed=repairAuto&&frozen->privateAuto
                        ?frozen->privateAuto->index:
                        repairAuto&&frozen->warningPreview
                        ?frozen->warningPreview->index:
                        repairCap&&frozen->privateCap
                        ?frozen->privateCap->index:
                        repairZipper&&frozen->privateZipper
                        ?frozen->privateZipper->index:
                        repairExcise&&frozen->privateCut
                        ?frozen->privateCut->index:frozen->candidate;
                    auto update=mesher::ViewportMeshUpdate{
                        displayed,
                        {owners.begin(),owners.end()},
                        liveWorker->status().completedStamps,
                        true,mesher::buildRepairInspectionOverlay(*frozen),
                        repairPreview,repairExcise,
                        repairExcise&&frozen->privateCut.has_value(),
                        repairZipper,repairZipper&&frozen->privateZipper.has_value(),
                        repairCap,repairCap&&frozen->privateCap.has_value(),
                        repairAuto,repairAuto&&frozen->privateAuto.has_value()};
                    update.inspectionQualityWarningPreview=frozen->warningPreview.has_value();
                    update.classifierChoices=frozen->classifierChoices;
                    update.classifierComparable=frozen->classifierComparable;
                    update.classifiersAgree=frozen->classifiersAgree;
                    update.activeClassifier=
                        frozen->activeClassifier==mesher::AutoClosureClassifier::Proximity?1:0;
                    return update;
                }
                auto snapshot=liveWorker->takeSnapshot();
                if(!snapshot)return std::nullopt;
                lastDisplayed=snapshot->index;
                return mesher::ViewportMeshUpdate{
                    std::move(snapshot->index),std::move(snapshot->dirtyPartitions),
                    snapshot->completedStamps,false,{},false};
            };
            interaction.completedStamps=[&]{return liveWorker->status().completedStamps;};
            interaction.skippedStamps=[]{return std::size_t{0};};
            interaction.pendingSamples=[&]{return liveWorker->status().pendingSteps;};
            interaction.pendingSegments=[&]{return liveWorker->status().pendingSegments;};
            interaction.totalQueuedSteps=[&]{return liveWorker->status().totalQueuedSteps;};
            interaction.inputCommands=[&]{return liveWorker->status().inputCommands;};
            interaction.canBegin=[&]{return liveWorker->canBegin()&&
                (!repairSession||!repairSession->frozen());};
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
                if(avoidCollisions) {
                    const auto& bt=status.stepResources.backtracking;
                    os<<" collision_avoidance=ON"
                      <<" rejected_stamps="<<status.rejectedStamps
                      <<" collision_checked="<<status.collisionCheckedStamps
                      <<" trial_count="<<bt.trials
                      <<" declined_trials="<<bt.declined
                      <<" normal_fraction="<<bt.chosenNormalFraction
                      <<" trial_copy_bytes="<<bt.privateCopiedBytes
                      <<" trial_changed_id_references="<<bt.changedIdReferences
                      <<" trial_distance_tests="<<bt.distanceTests
                      <<" private_copy_bytes="<<status.stepResources.privateCopiedBytes
                      <<" step_refused="<<(status.stepResources.refused?1:0)
                      <<" budget_refusals="<<status.resources.budgetRefusals;
                    if(status.stepResources.refused)
                        os<<" refusal_reason="<<status.lastRejection;
                }
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
                       (options.symmetric?" | mirrored":" | asymmetric")+
                       (avoidCollisions?" | collision avoidance ON (Merge OFF)":"")+
                       (repairInspect?(repairSession->frozen()?
                         " | R01 inspection FROZEN":" | R01 inspection armed"):"");
            };
            if(mergeTestTetra||mergeTestReflectedTetra)
                std::cout<<"[merge] This named developer fixture permits "
                            "explicit A-key nomination of all its ORIGINAL "
                            "FaceIds while ON and displaying the INITIAL root. "
                            "A is disabled on other sources and later roots.\n";
            if(repairAuto)
                std::cout<<"[repair-r06] On any frozen AUTO result, Ctrl+S saves "
                            "the exact repair candidate. Ctrl+Shift+S marks "
                            "PREFER ZIPPER; Ctrl+Alt+S marks PREFER CAPS. "
                            "The annotation does not modify the displayed result. "
                            "Replay via --repair-replay FILE or --repair-replay-test FILE.\n";
            if(repairAuto)
                std::cout<<"[repair-r06] classifier="
                         <<mesher::autoClosureClassifierName(repairClassifier)
                         <<" (M08 experimental proximity for trusted two-rim regions only; "
                            "V15 multi-rim fallback)\n";
            if(repairAuto)
                std::cout<<"[repair-r06] Armed AUTO private closure: green caps, "
                            "blue safe zippers, proven enclosed-shell retirement. "
                            "First contact freezes; no worker/history/checkpoint publication.\n";
            if(repairCap)
                std::cout<<"[repair-r05] Armed ALWAYS CAP (independent green caps on R03 holes, "
                            "private and frozen; never zippers).\n";
            if(repairZipper)
                std::cout<<"[repair-r04] Armed ALWAYS ZIPPER in unpublished private mesh. "
                            "Blue new seam triangles, bounded refinement, no caps; "
                            "the brush freezes on contact.\n";
            if(repairExcise&&!repairZipper&&!repairCap&&!repairAuto)
                std::cout<<"[repair-r03] Armed PRIVATE OPEN CUT: first certified contact freezes, "
                            "removes selected complete face fans/vertices only in a separate "
                            "unpublished debug revision; surviving hole rims are gold. "
                            "No cap/zipper; normal worker/history/checkpoint untouched.\n";
            if(repairPreview&&!repairExcise)
                std::cout<<"[repair-r02] Armed read-only proposed excision preview. "
                            "Orange selected faces; purple uncertain faces; "
                            "gold boundary loops; red incident contacts. "
                             "No geometry removed or published.\n";
            if(repairInspect)
                std::cout<<"[repair-r01] Armed: stamp a crossing to freeze an "
                            "unpublished candidate. Red dots are incident vertices; "
                            "dark-red rods are attached edges. Move camera/orbit "
                            "inside the surface, W/G; Escape to exit/relaunch. "
                            "No excision, no checkpoint/history publication.\n";
            if(avoidTestSheets&&!repairInspect)
                std::cout<<"[collision] M07 GPU exercise: orbit to view the two "
                            "closely separated faces near X=0, sculpt repeatedly "
                            "toward the other sheet, observe reduced normal_fraction "
                            "and eventual REFUSED. Sculpt the separate face "
                            "at X=3.5 to verify recovery; Ctrl+Z/Y and Ctrl+S, "
                            "then --recover with/without --avoid-collisions. "
                            "This is a finite fixture, not CCD.\n";
            if(avoidCollisions)
                std::cout<<"[collision] OFF-mode collision avoidance explicitly ON "
                            "for this session; protected Merge unavailable. "
                            "Unsafe/uncertain Steps may backtrack or REFUSE; "
                            "check [collision] and [sculpt] diagnostics. "
                            "Checkpoint recovery never re-enables avoidance "
                            "unless --avoid-collisions is supplied again. "
                            "No exact swept-volume CCD is claimed.\n";
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
