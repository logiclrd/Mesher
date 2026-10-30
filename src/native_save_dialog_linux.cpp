#include "mesher/native_save_dialog.hpp"
#include "mesher/native_dialog_path.hpp"

#include <QApplication>
#include <QDialog>
#include <QGuiApplication>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QPoint>
#include <QRect>
#include <QScreen>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QWindow>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

// GLFW's native X11 API declares XRandR types (RRCrtc/RROutput).
// Do not define GLFW_NATIVE_INCLUDE_NONE here: glfw3native.h must pull
// in both Xlib.h and Xrandr.h before exposing its X11 function prototypes.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>

namespace mesher {
namespace {
// GLFW owns the application window and remains the primary event loop.
// Qt is initialized only when the first modal dialog is needed. Its
// QApplication must be destroyed BEFORE glfwDestroyWindow/glfwTerminate;
// otherwise its static destructor would run at process exit after the GLFW
// X11 display and windows had already been torn down.
struct QtLifetime {
    int argc=1;
    char name[7]="Mesher";
    char* argv[2]={name,nullptr};
    std::unique_ptr<QApplication> application;
    bool shutdown=false;
};
QtLifetime& qtLifetime() {
    static QtLifetime state;
    return state;
}
QApplication& desktopQt() {
    auto& state=qtLifetime();
    if(state.shutdown)
        throw std::runtime_error("Qt dialogs used after native dialog shutdown");
    if(!state.application) {
        state.application=std::make_unique<QApplication>(state.argc,state.argv);
        state.application->setApplicationName(QStringLiteral("Mesher"));
        state.application->setQuitOnLastWindowClosed(false);
    }
    // Never overwrite a user's QT_QPA_PLATFORMTHEME or platform choice.
    return *state.application;
}

QString asQString(const std::filesystem::path& path) {
    const auto utf8=path.string();
    return QString::fromUtf8(utf8.data(),static_cast<qsizetype>(utf8.size()));
}
std::filesystem::path asPath(const QString& name) {
    // On Linux Qt paths are UTF-8; retain exact bytes and don't rely on
    // the user's current C locale during a filesystem path conversion.
    const auto utf8=name.toUtf8();
    return std::filesystem::path(
        std::string(utf8.constData(),static_cast<std::size_t>(utf8.size())));
}
void setInitialFile(QFileDialog& dialog,const std::filesystem::path& suggestion) {
    // CWD, not an earlier checkpoint's folder or Qt's last-used directory.
    dialog.setDirectory(asQString(nativeDialogInitialDirectory()));
    if(!suggestion.filename().empty())
        dialog.selectFile(asQString(suggestion.filename()));
}
// Platform helpers may hide the final wildcard group from the UI. The
// description repeats it so users still see the extension in the label.
constexpr const char* supportedFilter=
    "All supported files (*.mshr, *.obj, *.ply, *.stl) (*.mshr *.obj *.ply *.stl)";
constexpr const char* checkpointFilter="Mesher mesh files (*.mshr) (*.mshr)";
constexpr const char* objFilter="Wavefront OBJ (*.obj) (*.obj)";
constexpr const char* plyFilter="Stanford PLY (*.ply) (*.ply)";
constexpr const char* stlFilter="STL surface (*.stl) (*.stl)";
constexpr const char* allFilter="All Files (*)";
// Qt has no QWidget ownership of Mesher's GLFW window. On X11/XWayland,
// represent the GLFW XID as a non-owning QWindow, and make each modal
// dialog transient for it (NOT an embedded child via setParent).
class ScopedGlfwDialogOwner {
public:
    explicit ScopedGlfwDialogOwner(GLFWwindow* owner):owner_(owner) {
        if(!owner_||QGuiApplication::platformName()!=QStringLiteral("xcb"))
            return;
        const Window xid=glfwGetX11Window(owner_);
        if(xid)foreign_.reset(QWindow::fromWinId(static_cast<WId>(xid)));
    }
    ScopedGlfwDialogOwner(const ScopedGlfwDialogOwner&)=delete;
    ScopedGlfwDialogOwner& operator=(const ScopedGlfwDialogOwner&)=delete;
    void attach(QDialog& dialog) {
        if(!foreign_)return; // Native Wayland cannot wrap GLFW X11 IDs.
        // A foreign-window transient relationship does not provide Qt
        // with the GLFW window's desktop geometry. On multi-monitor X11,
        // Qt may otherwise choose its own primary screen for the picker.
        int x=0,y=0,width=0,height=0;
        glfwGetWindowPos(owner_,&x,&y);
        glfwGetWindowSize(owner_,&width,&height);
        if(width<=0||height<=0)return;
        const QPoint center(x+width/2,y+height/2);
        QScreen* screen=QGuiApplication::screenAt(center);
        if(!screen)screen=foreign_->screen();
        (void)dialog.winId();
        if(auto* handle=dialog.windowHandle()) {
            // setScreen can recreate a Qt native window: do it BEFORE
            // setting its transient parent, never the other way around.
            if(screen&&handle->screen()!=screen)handle->setScreen(screen);
            handle->setTransientParent(foreign_.get());
        }
        if(screen) {
            // Explicit placement is needed even when the X11 WM knows
            // the transient parent: QFileDialog has no QWidget parent.
            const QRect available=screen->availableGeometry();
            if(available.isValid()) {
                const QSize desired=dialog.sizeHint().expandedTo(
                    QSize(540,360)).boundedTo(available.size());
                dialog.resize(desired);
                const int left=std::clamp(center.x()-desired.width()/2,
                    available.left(),available.right()-desired.width()+1);
                const int top=std::clamp(center.y()-desired.height()/2,
                    available.top(),available.bottom()-desired.height()+1);
                dialog.move(left,top);
                // With the Qt-owned widget dialog, a window manager may
                // still apply placement at map time. Move once more as
                // the modal event loop starts, after the top-level window
                // exists and the window manager has had its first chance
                // to position it. The dialog remains on the GLFW screen.
                QTimer::singleShot(0,&dialog,[&dialog,left,top] {
                    dialog.move(left,top);
                });
            }
        }
    }
    ~ScopedGlfwDialogOwner() {
        if(owner_)glfwFocusWindow(owner_);
    }
private:
    GLFWwindow* owner_{};
    std::unique_ptr<QWindow> foreign_;
};
} // namespace

void initializeNativeDialogs() {
    (void)desktopQt();
}
void shutdownNativeDialogs() noexcept {
    auto& state=qtLifetime();
    if(state.shutdown)return;
    state.shutdown=true;
    // Every QFileDialog, QMessageBox and foreign QWindow wrapper has left
    // scope before the viewport invokes this. Qt's XCB/GTK event handlers
    // must be torn down while GLFW's X11 display is still alive.
    state.application.reset();
}

std::optional<std::filesystem::path> nativeSaveCheckpointDialog(
    GLFWwindow* owner,const std::filesystem::path& suggestion) {
    (void)desktopQt();
    ScopedGlfwDialogOwner parent(owner);
    QFileDialog dialog;
    // The QPA/native platform helper is free to place its OWN popup
    // on the primary display and ignore geometry on this QFileDialog.
    // Use Qt's actual widget-backed window so screen, transient parent
    // and post-map placement refer to the same visible top-level.
    // Must be set before other QFileDialog properties or showing it.
    dialog.setOption(QFileDialog::DontUseNativeDialog,true);
    // Both native helpers and the fallback Qt widget dialog display only
    // the description. It already contains the visible extension; the
    // final wildcard group remains reserved for actual filtering.
    dialog.setOption(QFileDialog::HideNameFilterDetails,true);
    dialog.setWindowTitle(QStringLiteral("Save Mesher checkpoint"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setWindowModality(Qt::ApplicationModal);
    dialog.setNameFilters({QString::fromUtf8(checkpointFilter)});
    dialog.setDefaultSuffix(QStringLiteral("mshr"));
    dialog.setOption(QFileDialog::DontConfirmOverwrite,false);
    setInitialFile(dialog,suggestion);
    parent.attach(dialog);
    if(dialog.exec()!=QDialog::Accepted)return std::nullopt;
    const auto names=dialog.selectedFiles();
    if(names.size()!=1||names.front().isEmpty())
        throw std::runtime_error("Qt Save As did not return a filename");
    return acceptNativeDialogFilePath(asPath(names.front()));
}
std::optional<NativeMeshExportSelection> nativeSaveMeshExportDialog(
    GLFWwindow* owner,const std::filesystem::path& suggestion) {
    (void)desktopQt();
    ScopedGlfwDialogOwner parent(owner);
    QFileDialog dialog;
    dialog.setOption(QFileDialog::DontUseNativeDialog,true);
    dialog.setOption(QFileDialog::HideNameFilterDetails,true);
    dialog.setWindowTitle(QStringLiteral("Export Mesher mesh"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setWindowModality(Qt::ApplicationModal);
    dialog.setOption(QFileDialog::DontConfirmOverwrite,false);
    QStringList filters;
    for(const auto& format:meshExportFormats) {
        const auto label=QString::fromUtf8(
            format.label.data(),static_cast<qsizetype>(format.label.size()));
        const auto suffix=QString::fromUtf8(
            format.extension.data(),
            static_cast<qsizetype>(format.extension.size()));
        filters.push_back(label+QStringLiteral(" (*.")+suffix+
                          QStringLiteral(")"));
    }
    dialog.setNameFilters(filters);
    auto index=static_cast<int>(MeshExportPreset::PlyLittleMeshAlpha);
    dialog.selectNameFilter(filters.at(index));
    dialog.setDefaultSuffix(QStringLiteral("ply"));
    const auto resolveFilter=[&](const QString& chosen) {
        // Qt themes may strip the final wildcard group. The user-visible
        // label stays unique even when multiple encodings share *.ply.
        for(int i=0;i<static_cast<int>(meshExportFormats.size());++i) {
            const auto label=QString::fromUtf8(
                meshExportFormats[static_cast<std::size_t>(i)].label.data(),
                static_cast<qsizetype>(
                    meshExportFormats[static_cast<std::size_t>(i)].label.size()));
            if(chosen.startsWith(label))return i;
        }
        return -1;
    };
    bool known=true;
    QObject::connect(&dialog,&QFileDialog::filterSelected,&dialog,
        [&](const QString& chosen) {
            const auto position=resolveFilter(chosen);
            known=position>=0;
            if(!known)return;
            index=position;
            const auto& format=meshExportFormats[static_cast<std::size_t>(position)];
            dialog.setDefaultSuffix(QString::fromUtf8(
                format.extension.data(),
                static_cast<qsizetype>(format.extension.size())));
        });
    setInitialFile(dialog,suggestion);
    parent.attach(dialog);
    if(dialog.exec()!=QDialog::Accepted)return std::nullopt;
    const auto names=dialog.selectedFiles();
    if(names.size()!=1||names.front().isEmpty())
        throw std::runtime_error("Qt Export Save As did not return a filename");
    // Prefer the final filter when Qt exposes it; its signal is retained
    // because some native Qt themes clear the final selectedNameFilter().
    const auto finalIndex=resolveFilter(dialog.selectedNameFilter());
    if(finalIndex>=0)index=finalIndex;
    else if(!known)
        throw std::runtime_error("Qt Export returned an unknown file-type filter");
    const auto preset=static_cast<MeshExportPreset>(index);
    const auto pathname=validateExportFilename(asPath(names.front()),preset);
    return NativeMeshExportSelection{
        acceptNativeDialogFilePath(pathname),preset};
}
bool nativeConfirmMeshExportDisclosure(GLFWwindow* owner,
                                       std::string_view disclosure) {
    (void)desktopQt();
    ScopedGlfwDialogOwner parent(owner);
    QMessageBox dialog(QMessageBox::Warning,
        QStringLiteral("Confirm mesh export format"),
        QString::fromUtf8(disclosure.data(),
                          static_cast<qsizetype>(disclosure.size()))+
            QStringLiteral("\n\nContinue with this export?"),
        QMessageBox::Yes|QMessageBox::Cancel);
    dialog.setDefaultButton(QMessageBox::Cancel);
    dialog.setWindowModality(Qt::ApplicationModal);
    parent.attach(dialog);
    return dialog.exec()==QMessageBox::Yes;
}
void nativeMeshExportErrorMessage(GLFWwindow* owner,const std::string& message) {
    (void)desktopQt();
    ScopedGlfwDialogOwner parent(owner);
    QMessageBox dialog(QMessageBox::Critical,
        QStringLiteral("Mesher - Export failed"),
        QString::fromUtf8(message.data(),
                          static_cast<qsizetype>(message.size())),
        QMessageBox::Ok);
    dialog.setWindowModality(Qt::ApplicationModal);
    parent.attach(dialog);
    (void)dialog.exec();
}
std::optional<NativeOpenSelection> nativeOpenMeshDialog(
    GLFWwindow* owner,const std::filesystem::path& suggestion) {
    (void)desktopQt();
    ScopedGlfwDialogOwner parent(owner);
    QFileDialog dialog;
    // The QPA/native platform helper is free to place its OWN popup
    // on the primary display and ignore geometry on this QFileDialog.
    // Use Qt's actual widget-backed window so screen, transient parent
    // and post-map placement refer to the same visible top-level.
    // Must be set before other QFileDialog properties or showing it.
    dialog.setOption(QFileDialog::DontUseNativeDialog,true);
    // Both native helpers and the fallback Qt widget dialog display only
    // the description. It already contains the visible extension; the
    // final wildcard group remains reserved for actual filtering.
    dialog.setOption(QFileDialog::HideNameFilterDetails,true);
    dialog.setWindowTitle(QStringLiteral("Open Mesher mesh"));
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setWindowModality(Qt::ApplicationModal);
    dialog.setNameFilters({QString::fromUtf8(supportedFilter),
                           QString::fromUtf8(checkpointFilter),
                           QString::fromUtf8(objFilter),
                           QString::fromUtf8(plyFilter),
                           QString::fromUtf8(stlFilter),
                           QString::fromUtf8(allFilter)});
    dialog.selectNameFilter(QString::fromUtf8(supportedFilter));
    // QFileDialog may clear or normalize selectedNameFilter() when a
    // selection is accepted. Track the live selection instead: Qt emits
    // filterSelected when the user changes the format dropdown.
    MeshOpenFilter selectedFilter=MeshOpenFilter::AllFiles;
    bool filterKnown=true;
    QObject::connect(&dialog,&QFileDialog::filterSelected,&dialog,
        [&](const QString& name) {
            filterKnown=true;
            if(name.startsWith(QStringLiteral("All supported files"))||
               name.startsWith(QStringLiteral("All Files")))
                selectedFilter=MeshOpenFilter::AllFiles;
            else if(name.startsWith(QStringLiteral("Mesher mesh files")))
                selectedFilter=MeshOpenFilter::MesherCheckpoint;
            else if(name.startsWith(QStringLiteral("Wavefront OBJ")))
                selectedFilter=MeshOpenFilter::WavefrontObj;
            else if(name.startsWith(QStringLiteral("Stanford PLY")))
                selectedFilter=MeshOpenFilter::StanfordPly;
            else if(name.startsWith(QStringLiteral("STL surface")))
                selectedFilter=MeshOpenFilter::StereolithographyStl;
            else filterKnown=false;
        });
    setInitialFile(dialog,suggestion);
    parent.attach(dialog);
    if(dialog.exec()!=QDialog::Accepted)return std::nullopt;
    const auto names=dialog.selectedFiles();
    if(names.size()!=1||names.front().isEmpty())
        throw std::runtime_error("Qt Open did not return a filename");
    const QString chosen=dialog.selectedNameFilter();
    // The parser still follows the ACTUAL dropdown selection, never the
    // filename extension. An unknown explicit filter change fails closed.
    if(!filterKnown)
        throw std::runtime_error(
            "Qt Open returned an unknown file-type filter: "+
            chosen.toStdString());
    return NativeOpenSelection{
        acceptNativeDialogFilePath(asPath(names.front())),selectedFilter};
}
std::optional<bool> nativePlyAlphaOpacityDialog(
    GLFWwindow* owner,float minimum,float maximum) {
    (void)desktopQt();
    ScopedGlfwDialogOwner parent(owner);
    const auto range=QString::number(double(minimum)*255.0,'f',0)+
        QStringLiteral(" to ")+QString::number(double(maximum)*255.0,'f',0);
    QMessageBox dialog(QMessageBox::Question,
        QStringLiteral("Interpret PLY alpha?"),
        QStringLiteral("This PLY has varying vertex alpha values (")+
            range+QStringLiteral(" / 255). They may represent opacity or "
            "unrelated data.\n\n"
            "Yes: use them as Mesher alpha.\n"
            "No: show opaque RGB and preserve source alpha for export.\n"
            "Cancel: leave the current mesh unchanged."),
        QMessageBox::Yes|QMessageBox::No|QMessageBox::Cancel);
    dialog.setDefaultButton(QMessageBox::No);
    dialog.setWindowModality(Qt::ApplicationModal);
    parent.attach(dialog);
    const auto answer=dialog.exec();
    if(answer==QMessageBox::Yes)return true;
    if(answer==QMessageBox::No)return false;
    if(answer==QMessageBox::Cancel||answer==QMessageBox::NoButton)
        return std::nullopt;
    throw std::runtime_error("Unexpected Qt PLY alpha choice");
}
void nativeOpenErrorMessage(GLFWwindow* owner,const std::string& message) {
    (void)desktopQt();
    ScopedGlfwDialogOwner parent(owner);
    QMessageBox dialog(QMessageBox::Critical,
        QStringLiteral("Mesher — Open failed"),
        QString::fromUtf8(message.data(),
                          static_cast<qsizetype>(message.size())),
        QMessageBox::Ok);
    dialog.setWindowModality(Qt::ApplicationModal);
    parent.attach(dialog);
    (void)dialog.exec();
}
} // namespace mesher
