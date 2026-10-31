#pragma once

#include "mesher/new_model.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace mesher {

// Headless state machine for the in-viewport Ctrl+N UI. Callers display
// modal controls, dispatch approved save requests and only publish a
// prepared source after Result::Create. Failed/cancelled writes never
// result in Create, and the previous document remains authoritative.
enum class NewDialogPhase {Closed,ChoosePrimitive,ConfirmDiscard,AwaitSave};
enum class NewDialogAction {NoAction,Create,RequestSave,RequestSaveAs};
struct NewDialogResult {
    NewDialogAction action=NewDialogAction::NoAction;
    std::optional<NewModelRequest> model;
};
struct NewDialogRect {
    int x{},y{},width{},height{};
    bool contains(double px,double py) const noexcept {
        return px>=x&&py>=y&&px<x+width&&py<y+height;
    }
};
struct NewDialogLayout {
    NewDialogRect backdrop,panel;
    NewDialogRect asymmetric,symmetric,torus,vertices,cancel;
    NewDialogRect save,saveAs,discard;
};
NewDialogLayout newDialogLayout(int viewportWidth,int viewportHeight,
                                NewDialogPhase phase,bool canSave);

class NewModelDialog {
public:
    void open(std::uint32_t startupVertices);
    bool visible() const noexcept {return phase_!=NewDialogPhase::Closed;}
    NewDialogPhase phase() const noexcept {return phase_;}
    const std::string& verticesText() const noexcept {return vertices_;}
    const std::optional<NewModelRequest>& pendingModel() const noexcept {
        return pending_;
    }
    bool canDirectSave() const noexcept {return canSave_;}
    void typeDigit(char digit);
    void backspace();
    void clearVertexInput();
    // Pick a card. Returns Create immediately for a clean document, or
    // enters ConfirmDiscard when the caller reports unsaved edits.
    NewDialogResult choose(NewModelKind kind,bool dirty,bool canSave);
    NewDialogResult save(bool saveAs);
    NewDialogResult discard();
    // Called only after the asynchronous save has been successfully queued.
    void saveQueued();
    // Only a confirmed successful, complete disk write permits Create.
    NewDialogResult saveCompleted(bool successful);
    void saveCancelled();
    void cancel() noexcept;
private:
    NewDialogPhase phase_=NewDialogPhase::Closed;
    std::string vertices_;
    bool canSave_=false;
    std::optional<NewModelRequest> pending_;
};

} // namespace mesher
