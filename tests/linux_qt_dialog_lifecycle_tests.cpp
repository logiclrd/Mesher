// Xvfb-backed regression for a GLFW + Qt6 GUI lifetime. This exercises
// actual XCB QFileDialog and QMessageBox create/close cycles without Vulkan:
// the process must exit without leaving a QApplication destructor to run
// after glfwTerminate. A source-string contract cannot catch the segfault.
#include "mesher/native_save_dialog.hpp"

#include <QApplication>
#include <QDialog>
#include <QFileDialog>
#include <QScreen>
#include <QTimer>

#include <cmath>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void autoCancelNextDialog(GLFWwindow* owner,bool* wasPositioned=nullptr) {
    QTimer::singleShot(120,[owner,wasPositioned] {
        if(auto* dialog=qobject_cast<QDialog*>(
               QApplication::activeModalWidget())) {
            if(wasPositioned) {
                // A real widget QFileDialog must be positioned near the
                // GLFW window, not implicitly centered on screen zero.
                if(auto* picker=qobject_cast<QFileDialog*>(dialog)) {
                    int x=0,y=0,width=0,height=0;
                    glfwGetWindowPos(owner,&x,&y);
                    glfwGetWindowSize(owner,&width,&height);
                    const QPoint center(x+width/2,y+height/2);
                    const QPoint actual=picker->frameGeometry().center();
                    QScreen* screen=QGuiApplication::screenAt(center);
                    const bool onOwnerScreen=screen&&
                        screen->geometry().contains(actual);
                    *wasPositioned=picker->testOption(
                        QFileDialog::DontUseNativeDialog)&&onOwnerScreen&&
                        std::abs(actual.x()-center.x())<190&&
                        std::abs(actual.y()-center.y())<190;
                }
            }
            dialog->reject();
            return;
        }
        // Error alerts or unusual desktop Qt backends may expose a
        // visible top-level without activeModalWidget().
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
    glfwSetWindowPos(window,30,500);
    glfwShowWindow(window);
    glfwPollEvents();
    try {
        mesher::initializeNativeDialogs();
        if(!QApplication::instance())
            throw std::runtime_error("Qt application did not initialize");
        for(int i=0;i<2;++i) {
            bool openPositioned=false;
            autoCancelNextDialog(window,&openPositioned);
            if(mesher::nativeOpenMeshDialog(window,"fixture.mshr"))
                throw std::runtime_error("An auto-cancelled Open returned a path");
            if(!openPositioned)
                throw std::runtime_error("Open did not appear near GLFW owner");
            bool savePositioned=false;
            autoCancelNextDialog(window,&savePositioned);
            if(mesher::nativeSaveCheckpointDialog(window,"fixture.mshr"))
                throw std::runtime_error("An auto-cancelled Save returned a path");
            if(!savePositioned)
                throw std::runtime_error("Save As did not appear near GLFW owner");
            autoCancelNextDialog(window);
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
