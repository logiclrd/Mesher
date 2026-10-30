#include "mesher/new_model_dialog.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace mesher {
NewDialogLayout newDialogLayout(
    int viewportWidth,int viewportHeight,NewDialogPhase phase,bool canSave) {
    const int width=std::max(320,viewportWidth);
    const int height=std::max(220,viewportHeight);
    NewDialogLayout layout;
    layout.backdrop={0,0,width,height};
    const int panelWidth=std::min(width-20,phase==NewDialogPhase::ChoosePrimitive?
                                          860:720);
    const int panelHeight=std::min(height-20,phase==NewDialogPhase::ChoosePrimitive?
                                           410:230);
    const int left=(width-panelWidth)/2;
    const int top=(height-panelHeight)/2;
    layout.panel={left,top,panelWidth,panelHeight};
    const int pad=24;
    if(phase==NewDialogPhase::ChoosePrimitive) {
        const int separation=12;
        const int cardWidth=(panelWidth-2*pad-2*separation)/3;
        const int cardHeight=std::max(40,panelHeight-142);
        const int cardTop=top+46;
        layout.asymmetric={left+pad,cardTop,cardWidth,cardHeight};
        layout.symmetric={left+pad+cardWidth+separation,cardTop,
                          cardWidth,cardHeight};
        layout.torus={left+pad+2*(cardWidth+separation),cardTop,
                      cardWidth,cardHeight};
        layout.vertices={left+pad+101,top+panelHeight-64,150,35};
        layout.cancel={left+panelWidth-pad-120,top+panelHeight-64,120,35};
    }else {
        const int buttonWidth=canSave?130:145;
        const int gap=10;
        const int buttons=canSave?4:3;
        const int rowWidth=buttons*buttonWidth+(buttons-1)*gap;
        const int begin=left+(panelWidth-rowWidth)/2;
        const int y=top+panelHeight-71;
        int index=0;
        if(canSave)layout.save={begin+index++*(buttonWidth+gap),
                              y,buttonWidth,39};
        layout.saveAs={begin+index++*(buttonWidth+gap),
                       y,buttonWidth,39};
        layout.discard={begin+index++*(buttonWidth+gap),
                        y,buttonWidth,39};
        layout.cancel={begin+index*(buttonWidth+gap),
                       y,buttonWidth,39};
    }
    return layout;
}

void NewModelDialog::open(std::uint32_t startupVertices) {
    if(!startupVertices||startupVertices>1000000)
        throw std::invalid_argument("Invalid default vertex count");
    phase_=NewDialogPhase::ChoosePrimitive;
    vertices_=std::to_string(startupVertices);
    canSave_=false;
    pending_.reset();
}
void NewModelDialog::typeDigit(char digit) {
    if(phase_!=NewDialogPhase::ChoosePrimitive||
       digit<'0'||digit>'9'||vertices_.size()>=7)return;
    vertices_.push_back(digit);
}
void NewModelDialog::backspace() {
    if(phase_==NewDialogPhase::ChoosePrimitive&&!vertices_.empty())
        vertices_.pop_back();
}
void NewModelDialog::clearVertexInput() {
    if(phase_==NewDialogPhase::ChoosePrimitive)vertices_.clear();
}
NewDialogResult NewModelDialog::choose(
    NewModelKind kind,bool dirty,bool canSave) {
    if(phase_!=NewDialogPhase::ChoosePrimitive)
        throw std::logic_error("New Model selection requires chooser");
    const auto count=parseNewModelVertexCount(vertices_,kind);
    pending_=NewModelRequest{kind,count};
    if(dirty) {
        phase_=NewDialogPhase::ConfirmDiscard;
        canSave_=canSave;
        return {};
    }
    NewDialogResult result{NewDialogAction::Create,pending_};
    cancel();
    return result;
}
NewDialogResult NewModelDialog::save(bool saveAs) {
    if(phase_!=NewDialogPhase::ConfirmDiscard||!pending_)
        throw std::logic_error("Save requested outside New dirty-document confirmation");
    if(!saveAs&&!canSave_)
        throw std::logic_error("Direct Save requires a native document destination");
    return {saveAs?NewDialogAction::RequestSaveAs:
                   NewDialogAction::RequestSave,std::nullopt};
}
NewDialogResult NewModelDialog::discard() {
    if(phase_!=NewDialogPhase::ConfirmDiscard||!pending_)
        throw std::logic_error("Discard requested without a pending New document");
    NewDialogResult result{NewDialogAction::Create,pending_};
    cancel();
    return result;
}
void NewModelDialog::saveQueued() {
    if(phase_!=NewDialogPhase::ConfirmDiscard||!pending_)
        throw std::logic_error("New save was queued in wrong modal phase");
    phase_=NewDialogPhase::AwaitSave;
}
NewDialogResult NewModelDialog::saveCompleted(bool successful) {
    if(phase_!=NewDialogPhase::AwaitSave||!pending_)
        throw std::logic_error("Unexpected New save completion");
    if(!successful) {
        phase_=NewDialogPhase::ConfirmDiscard;
        return {};
    }
    NewDialogResult result{NewDialogAction::Create,pending_};
    cancel();
    return result;
}
void NewModelDialog::saveCancelled() {
    if(phase_!=NewDialogPhase::ConfirmDiscard&&
       phase_!=NewDialogPhase::AwaitSave)
        throw std::logic_error("Unexpected New save cancellation");
    phase_=NewDialogPhase::ConfirmDiscard;
}
void NewModelDialog::cancel() noexcept {
    phase_=NewDialogPhase::Closed;
    canSave_=false;
    pending_.reset();
    vertices_.clear();
}
} // namespace mesher
