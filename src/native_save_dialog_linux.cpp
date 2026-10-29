#include "mesher/native_save_dialog.hpp"

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QGuiApplication>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QString>
#include <QStringList>
#include <QWindow>

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
// GLFW remains Mesher's main event loop. Qt is used solely for modal desktop
// dialogs on the UI thread; no Qt widgets are created at process startup.
// QApplication and its argument storage MUST outlive every dialog, and Qt
// does not support repeatedly destroying/reconstructing the application.
QApplication& desktopQt() {
    static int argc=1;
    static char appName[]="Mesher";
    static char* argv[]{appName,nullptr};
    static QApplication application(argc,argv);
    static const bool initialized=[] {
        application.setApplicationName(QStringLiteral("Mesher"));
        application.setQuitOnLastWindowClosed(false);
        return true;
    }();
    (void)initialized;
    // Respect QT_QPA_PLATFORMTHEME and system QPA plugins supplied by
    // the user's desktop. Never forcibly override their chosen theme.
    return application;
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
std::filesystem::path suggestedDesktopPath(
    const std::filesystem::path& suggestion) {
    if(!suggestion.parent_path().empty())
        return std::filesystem::absolute(suggestion);
    const QString documents=QDir::home().filePath(QStringLiteral("Documents"));
    const auto folder=QDir(documents).exists()?asPath(documents):
                      asPath(QDir::homePath());
    return folder/suggestion.filename();
}
void setInitialFile(QFileDialog& dialog,const std::filesystem::path& file) {
    const auto absolute=suggestedDesktopPath(file);
    dialog.setDirectory(asQString(absolute.parent_path()));
    if(!absolute.filename().empty())
        dialog.selectFile(asQString(absolute.filename()));
}
// Platform helpers may hide the final wildcard group from the UI. The
// description repeats it so users still see the extension in the label.
constexpr const char* checkpointFilter="Mesher mesh files (*.mshr) (*.mshr)";
constexpr const char* objFilter="Wavefront OBJ (*.obj) (*.obj)";
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
        if(!foreign_)return; // Wayland/non-foreign QPA fallback
        (void)dialog.winId();
        if(auto* handle=dialog.windowHandle())
            handle->setTransientParent(foreign_.get());
    }
    ~ScopedGlfwDialogOwner() {
        if(owner_)glfwFocusWindow(owner_);
    }
private:
    GLFWwindow* owner_{};
    std::unique_ptr<QWindow> foreign_;
};
} // namespace

std::optional<std::filesystem::path> nativeSaveCheckpointDialog(
    GLFWwindow* owner,const std::filesystem::path& suggestion) {
    (void)desktopQt();
    ScopedGlfwDialogOwner parent(owner);
    QFileDialog dialog;
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
    return asPath(names.front());
}
std::optional<NativeOpenSelection> nativeOpenMeshDialog(
    GLFWwindow* owner,const std::filesystem::path& suggestion) {
    (void)desktopQt();
    ScopedGlfwDialogOwner parent(owner);
    QFileDialog dialog;
    // Both native helpers and the fallback Qt widget dialog display only
    // the description. It already contains the visible extension; the
    // final wildcard group remains reserved for actual filtering.
    dialog.setOption(QFileDialog::HideNameFilterDetails,true);
    dialog.setWindowTitle(QStringLiteral("Open Mesher mesh"));
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setWindowModality(Qt::ApplicationModal);
    dialog.setNameFilters({QString::fromUtf8(checkpointFilter),
                           QString::fromUtf8(objFilter),
                           QString::fromUtf8(allFilter)});
    dialog.selectNameFilter(QString::fromUtf8(checkpointFilter));
    setInitialFile(dialog,suggestion);
    parent.attach(dialog);
    if(dialog.exec()!=QDialog::Accepted)return std::nullopt;
    const auto names=dialog.selectedFiles();
    if(names.size()!=1||names.front().isEmpty())
        throw std::runtime_error("Qt Open did not return a filename");
    const QString chosen=dialog.selectedNameFilter();
    // The filter is an actual part of the QFileDialog result, NOT inferred
    // from the extension. Unexpected/empty results fail closed.
    const auto filter=chosen==QString::fromUtf8(checkpointFilter)?
        MeshOpenFilter::MesherCheckpoint:
        chosen==QString::fromUtf8(objFilter)?MeshOpenFilter::WavefrontObj:
        chosen==QString::fromUtf8(allFilter)?MeshOpenFilter::AllFiles:
        throw std::runtime_error("Qt Open returned an unknown file-type filter");
    return NativeOpenSelection{asPath(names.front()),filter};
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
