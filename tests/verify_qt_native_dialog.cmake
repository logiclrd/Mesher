# UX06: Linux uses one integrated Qt file-type chooser for both actions;
# never regress to external kdialog/zenity or a second type-picker.
if(NOT DEFINED LINUX_DIALOG_SOURCE OR NOT DEFINED APP_CMAKE_SOURCE)
    message(FATAL_ERROR "Qt dialog source-contract input paths missing")
endif()
file(READ "${LINUX_DIALOG_SOURCE}" dialog)
file(READ "${APP_CMAKE_SOURCE}" cmakeSrc)
foreach(required
    "QApplication"
    "QFileDialog"
    "QMessageBox"
    "dialog.selectedNameFilter()"
    "dialog.setNameFilters("
    "dialog.setDefaultSuffix("
    "std::optional<NativeOpenSelection> nativeOpenMeshDialog"
    "std::optional<std::filesystem::path> nativeSaveCheckpointDialog"
    "static QApplication application(argc,argv)"
    "dialog.exec()")
    string(FIND "${dialog}" "${required}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "Linux native Open/Save requires Qt: ${required}")
    endif()
endforeach()
foreach(disallowed "kdialog" "zenity" "posix_spawn" "QFileDialog::DontUseNativeDialog")
    string(FIND "${dialog}" "${disallowed}" at)
    if(NOT at EQUAL -1)
        message(FATAL_ERROR "Linux dialog unexpectedly uses ${disallowed}")
    endif()
endforeach()
foreach(required "find_package(Qt6 REQUIRED COMPONENTS Widgets)"
                 "target_link_libraries(Mesher PRIVATE Qt6::Widgets)")
    string(FIND "${cmakeSrc}" "${required}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "Linux Qt dependency must be desktop-scoped: ${required}")
    endif()
endforeach()
