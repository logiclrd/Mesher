# UX06: Linux uses one integrated Qt file-type chooser for both actions;
# never regress to external kdialog/zenity or a second type-picker.
if(NOT DEFINED LINUX_DIALOG_SOURCE OR NOT DEFINED APP_CMAKE_SOURCE
   OR NOT DEFINED VIEWPORT_SOURCE)
    message(FATAL_ERROR "Qt dialog source-contract input paths missing")
endif()
file(READ "${LINUX_DIALOG_SOURCE}" dialog)
file(READ "${APP_CMAKE_SOURCE}" cmakeSrc)
file(READ "${VIEWPORT_SOURCE}" viewport)
foreach(required
    "QApplication"
    "QFileDialog"
    "QMessageBox"
    "dialog.selectedNameFilter()"
    "dialog.setNameFilters("
    "dialog.setDefaultSuffix("
    "dialog.setOption(QFileDialog::HideNameFilterDetails,true)"
    "std::optional<NativeOpenSelection> nativeOpenMeshDialog"
    "std::optional<std::filesystem::path> nativeSaveCheckpointDialog"
    "state.application=std::make_unique<QApplication>(state.argc,state.argv)"
    "state.application.reset()"
    "void shutdownNativeDialogs() noexcept"
    "QGuiApplication::screenAt(center)"
    "glfwGetWindowPos(owner_,&x,&y)"
    "glfwGetWindowSize(owner_,&width,&height)"
    "handle->setScreen(screen)"
    "dialog.move(left,top)"
    "dialog.exec()"
    "QWindow::fromWinId("
    "handle->setTransientParent(foreign_.get())"
    "glfwGetX11Window(owner_)"
    "parent.attach(dialog)"
    "glfwFocusWindow(owner_)"
    "Mesher mesh files (*.mshr) (*.mshr)"
    "Wavefront OBJ (*.obj) (*.obj)")
    string(FIND "${dialog}" "${required}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "Linux native Open/Save requires Qt: ${required}")
    endif()
endforeach()
foreach(disallowed "kdialog" "zenity" "posix_spawn" "QFileDialog::DontUseNativeDialog" "handle->setParent(")
    string(FIND "${dialog}" "${disallowed}" at)
    if(NOT at EQUAL -1)
        message(FATAL_ERROR "Linux dialog unexpectedly uses ${disallowed}")
    endif()
endforeach()
foreach(required "shutdownNativeDialogs();"
                 "if(window)glfwDestroyWindow(window);")
    string(FIND "${viewport}" "${required}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "Qt must shut down before GLFW: ${required}")
    endif()
endforeach()
string(FIND "${viewport}" "shutdownNativeDialogs();" shutdownAt)
string(FIND "${viewport}" "if(window)glfwDestroyWindow(window);" glfwAt)
if(shutdownAt GREATER glfwAt)
    message(FATAL_ERROR "Qt desktop lifetime must end before GLFW destroys X11")
endif()
foreach(required "find_package(Qt6 REQUIRED COMPONENTS Widgets)"
                 "target_link_libraries(Mesher PRIVATE Qt6::Widgets)")
    string(FIND "${cmakeSrc}" "${required}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "Linux Qt dependency must be desktop-scoped: ${required}")
    endif()
endforeach()
