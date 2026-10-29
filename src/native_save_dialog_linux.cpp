#include "mesher/native_save_dialog.hpp"

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QString>
#include <QStringList>

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>

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
constexpr const char* checkpointFilter="Mesher mesh files (*.mshr)";
constexpr const char* objFilter="Wavefront OBJ (*.obj)";
constexpr const char* allFilter="All Files (*)";
} // namespace

std::optional<std::filesystem::path> nativeSaveCheckpointDialog(
    GLFWwindow*,const std::filesystem::path& suggestion) {
    (void)desktopQt();
    QFileDialog dialog;
    dialog.setWindowTitle(QStringLiteral("Save Mesher checkpoint"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setWindowModality(Qt::ApplicationModal);
    dialog.setNameFilters({QStringLiteral("Mesher checkpoints (*.mshr)")});
    dialog.setDefaultSuffix(QStringLiteral("mshr"));
    dialog.setOption(QFileDialog::DontConfirmOverwrite,false);
    setInitialFile(dialog,suggestion);
    if(dialog.exec()!=QDialog::Accepted)return std::nullopt;
    const auto names=dialog.selectedFiles();
    if(names.size()!=1||names.front().isEmpty())
        throw std::runtime_error("Qt Save As did not return a filename");
    return asPath(names.front());
}
std::optional<NativeOpenSelection> nativeOpenMeshDialog(
    GLFWwindow*,const std::filesystem::path& suggestion) {
    (void)desktopQt();
    QFileDialog dialog;
    dialog.setWindowTitle(QStringLiteral("Open Mesher mesh"));
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setWindowModality(Qt::ApplicationModal);
    dialog.setNameFilters({QString::fromUtf8(checkpointFilter),
                           QString::fromUtf8(objFilter),
                           QString::fromUtf8(allFilter)});
    dialog.selectNameFilter(QString::fromUtf8(checkpointFilter));
    setInitialFile(dialog,suggestion);
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
void nativeOpenErrorMessage(GLFWwindow*,const std::string& message) {
    (void)desktopQt();
    QMessageBox::critical(nullptr,QStringLiteral("Mesher — Open failed"),
        QString::fromUtf8(message.data(),
                          static_cast<qsizetype>(message.size())));
}
} // namespace mesher
