// Xvfb-backed regression for a GLFW + Qt6 GUI lifetime. This exercises
// actual XCB QFileDialog and QMessageBox create/close cycles without Vulkan:
// the process must exit without leaving a QApplication destructor to run
// after glfwTerminate. A source-string contract cannot catch the segfault.
#include "mesher/native_save_dialog.hpp"

#include <QApplication>
#include <QDialog>
#include <QTimer>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
void autoCancelNextDialog() {
    QTimer::singleShot(120,[] {
        if(auto* widget=QApplication::activeModalWidget()) {
            if(auto* dialog=qobject_cast<QDialog*>(widget)) {
                dialog->reject();
                return;
            }
        }
        // Some native file helpers expose their Qt shell as a top-level
        // widget without setting activeModalWidget().
        for(auto* widget:QApplication::topLevelWidgets()) {
            if(auto* dialog=qobject_cast<QDialog*>(widget)) {
                if(dialog->isVisible()) {
                    dialog->reject();
                    return;
                }
            }
        }
    });
}
}
int main() {
    if(!glfwInit()) {
        std::cerr<<"GLFW initialization failed under Xvfb\n";
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API,GLFW_NO_API);
    auto* window=glfwCreateWindow(640,480,"Mesher Qt modal test",
                                  nullptr,nullptr);
    if(!window) {
        glfwTerminate();
        return 2;
    }
    try {
        mesher::initializeNativeDialogs();
        if(!QApplication::instance())
            throw std::runtime_error("Qt application did not initialize");
        for(int i=0;i<2;++i) {
            autoCancelNextDialog();
            if(mesher::nativeOpenMeshDialog(window,"fixture.mshr"))
                throw std::runtime_error("An auto-cancelled Open returned a path");
            autoCancelNextDialog();
            if(mesher::nativeSaveCheckpointDialog(window,"fixture.mshr"))
                throw std::runtime_error("An auto-cancelled Save returned a path");
            autoCancelNextDialog();
            mesher::nativeOpenErrorMessage(window,"Expected test error");
        }
        mesher::shutdownNativeDialogs();
        if(QApplication::instance())
            throw std::runtime_error("Qt remains alive after UI shutdown");
        mesher::shutdownNativeDialogs(); // idempotent
        glfwDestroyWindow(window);
        glfwTerminate();
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"Qt dialog lifetime regression: "<<e.what()<<"\n";
        // This is a failing test, but still enforce orderly cleanup.
        mesher::shutdownNativeDialogs();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 3;
    }
}
