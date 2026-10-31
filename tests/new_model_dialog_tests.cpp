#include "mesher/new_model.hpp"
#include "mesher/new_model_dialog.hpp"
#include "mesher/sculpt_fixtures.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace mesher;
void require(bool yes,const char* msg) {
    if(!yes)throw std::runtime_error(msg);
}
void models() {
    SphereOptions options;
    options.seed=17;
    options.relaxationIterations=2;
    const std::array requests{
        NewModelRequest{NewModelKind::AsymmetricSphere,256},
        NewModelRequest{NewModelKind::SymmetricSphere,256},
        NewModelRequest{NewModelKind::Torus,256}
    };
    for(const auto request:requests) {
        auto prepared=generateNewModelSource(request,options);
        require(prepared.mesh.vertices.size()==request.vertices,
                "New model generator changed vertex count");
        require(!prepared.mesh.triangles.empty()&&prepared.bounds.hasVertices,
                "New model lacks geometry or bounds");
        require(prepared.symmetric==
                (request.kind==NewModelKind::SymmetricSphere),
                "Wrong symmetry for selected primitive");
        if(request.kind==NewModelKind::Torus) {
            const auto audit=auditFixtureTopology(prepared.mesh);
            require(audit.eulerCharacteristic==0&&audit.components==1,
                    "Torus generator did not preserve genus-one topology");
        }else require(validateSphere(prepared.mesh),
                     "Sphere generator returned nonmanifold geometry");
    }
    require(newModelName(NewModelKind::Torus)=="Torus",
            "New Model title mismatched selected geometry");
}
void invalidCounts() {
    const std::array bad{
        "", "0","7","-1","3.25","1000001","4294967296","abc","10000000"
    };
    for(const auto* input:bad) {
        bool refused=false;
        try {(void)parseNewModelVertexCount(
                 input,NewModelKind::AsymmetricSphere);}
        catch(const std::invalid_argument&){refused=true;}
        require(refused,"Bad sphere vertex input was accepted");
    }
    require(parseNewModelVertexCount("128",NewModelKind::Torus)==128,
            "Minimum torus input rejected");
    bool refused=false;
    try {(void)parseNewModelVertexCount("127",NewModelKind::Torus);}
    catch(const std::invalid_argument&){refused=true;}
    require(refused,"Torus count below supported minimum was accepted");
    SphereOptions options;
    refused=false;
    try {(void)generateNewModelSource(
            {NewModelKind::Torus,127},options);}
    catch(const std::invalid_argument&){refused=true;}
    require(refused,"Invalid New request proceeded to torus generation");
    refused=false;
    try {(void)generateNewModelSource(
            {static_cast<NewModelKind>(4),256},options);}
    catch(const std::invalid_argument&){refused=true;}
    require(refused,"Unknown New primitive was accepted");
}
void dialogClean() {
    NewModelDialog dialog;
    dialog.open(5000);
    require(dialog.phase()==NewDialogPhase::ChoosePrimitive&&
            dialog.verticesText()=="5000","Wrong initial field/default");
    dialog.clearVertexInput();
    dialog.typeDigit('2');
    dialog.typeDigit('5');
    dialog.typeDigit('6');
    dialog.typeDigit('x');
    require(dialog.verticesText()=="256","Numeric input accepted non-digit");
    dialog.backspace();
    require(dialog.verticesText()=="25","Backspace failed");
    dialog.typeDigit('6');
    auto result=dialog.choose(NewModelKind::AsymmetricSphere,false,false);
    require(result.action==NewDialogAction::Create&&
            result.model&&result.model->vertices==256&&
            result.model->kind==NewModelKind::AsymmetricSphere&&
            !dialog.visible(),"Clean New did not create directly");
}
void dialogDirty() {
    NewModelDialog dialog;
    dialog.open(5000);
    auto r=dialog.choose(NewModelKind::Torus,true,false);
    require(r.action==NewDialogAction::NoAction&&
            dialog.phase()==NewDialogPhase::ConfirmDiscard&&
            !dialog.canDirectSave(),
            "Dirty document must open confirmation without direct Save");
    bool refused=false;
    try {(void)dialog.save(false);}
    catch(const std::logic_error&){refused=true;}
    require(refused,"Direct Save was offered for unnamed/foreign document");
    r=dialog.save(true);
    require(r.action==NewDialogAction::RequestSaveAs&&
            dialog.phase()==NewDialogPhase::ConfirmDiscard,
            "Save As button did not request native picker");
    dialog.saveCancelled();
    require(dialog.phase()==NewDialogPhase::ConfirmDiscard,
            "Save As cancellation exited dirty confirmation");
    dialog.saveQueued();
    r=dialog.saveCompleted(false);
    require(r.action==NewDialogAction::NoAction&&
            dialog.phase()==NewDialogPhase::ConfirmDiscard,
            "Failed async save discarded original document");
    dialog.saveQueued();
    r=dialog.saveCompleted(true);
    require(r.action==NewDialogAction::Create&&r.model&&
            r.model->vertices==5000&&
            r.model->kind==NewModelKind::Torus&&!dialog.visible(),
            "New was not released after successful async save");

    dialog.open(350);
    dialog.choose(NewModelKind::SymmetricSphere,true,true);
    require(dialog.canDirectSave(),"Native checkpoint Save button absent");
    r=dialog.save(false);
    require(r.action==NewDialogAction::RequestSave,
            "Native Save shortcut incorrectly chose Save As");
    dialog.cancel();
    require(!dialog.visible()&&!dialog.pendingModel(),
            "Escape/Cancel retained hidden pending New");
    dialog.open(180);
    dialog.choose(NewModelKind::Torus,true,true);
    r=dialog.discard();
    require(r.action==NewDialogAction::Create&&r.model&&
            r.model->vertices==180&&!dialog.visible(),
            "No did not discard edits then create requested New model");
}
void layout() {
    const auto a=newDialogLayout(1280,800,NewDialogPhase::ChoosePrimitive,false);
    require(a.panel.contains(640,400)&&a.asymmetric.x<a.symmetric.x&&
            a.symmetric.x<a.torus.x,
            "Chooser lacks three ordered in-window previews");
    require(a.vertices.x<a.cancel.x&&
            a.cancel.y>a.torus.y&&
            a.panel.contains(a.vertices.x,a.vertices.y)&&
            a.panel.contains(a.cancel.x,a.cancel.y),
            "Chooser does not put Vertices bottom left, Cancel bottom right");
    const auto dirty=newDialogLayout(
        1280,800,NewDialogPhase::ConfirmDiscard,true);
    require(dirty.save.x<dirty.saveAs.x&&
            dirty.saveAs.x<dirty.discard.x&&
            dirty.discard.x<dirty.cancel.x,
            "Native confirmation buttons have wrong left-to-right order");
    const auto noSave=newDialogLayout(
        1280,800,NewDialogPhase::ConfirmDiscard,false);
    require(noSave.save.width==0&&
            noSave.saveAs.x<noSave.discard.x&&
            noSave.discard.x<noSave.cancel.x,
            "Foreign import must hide direct Save but retain Save As/No/Cancel");
}
} // namespace

int main() {
    try {
        models();invalidCounts();dialogClean();dialogDirty();layout();
        std::cout<<"UX09B private new mesh and modal decision tests passed\n";
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"UX09B failed: "<<e.what()<<'\n';
        return 1;
    }
}
