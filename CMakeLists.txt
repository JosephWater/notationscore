cmake_minimum_required(VERSION 3.28)
project(JianpuLab VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.10.2 EXACT REQUIRED COMPONENTS
    Core Gui Widgets Xml Svg PrintSupport Multimedia Qml Quick QuickControls2 Test LinguistTools Core5Compat Concurrent)
message(STATUS "JianpuLab Qt ${Qt6_VERSION}: ${Qt6_DIR}")
qt_standard_project_setup(REQUIRES 6.10)

add_subdirectory(cmake/engine)

qt_add_library(jianpu_document STATIC src/core/runtime.cpp src/core/runtime.h
    src/core/scoredocument.cpp src/core/scoredocument_text.cpp src/core/scoredocument.h src/core/scoretypes.h
    src/input/textformat.cpp src/input/textformat.h src/core/musicaltime.h)
target_include_directories(jianpu_document PUBLIC src)
target_link_libraries(jianpu_document PUBLIC Qt6::Widgets PRIVATE jianpu_muse_core)
if(MSVC)
    target_compile_options(jianpu_document PUBLIC /utf-8 /bigobj)
endif()
qt_add_executable(jianpu_engine_probe tools/engineprobe.cpp)
set_target_properties(jianpu_engine_probe PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
target_link_libraries(jianpu_engine_probe PRIVATE jianpu_document)

qt_add_library(jianpu_widget STATIC src/scorewidget.cpp src/scorewidget.h)
target_include_directories(jianpu_widget PUBLIC src)
target_link_libraries(jianpu_widget PUBLIC Qt6::Widgets)
if(MSVC)
    target_compile_options(jianpu_widget PUBLIC /utf-8 /W4)
else()
    target_compile_options(jianpu_widget PRIVATE -Wall -Wextra -Wpedantic)
endif()

qt_add_executable(jianpu_lab src/main.cpp)
set_target_properties(jianpu_lab PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE WIN32_EXECUTABLE TRUE)
qt_add_library(jianpu_editor STATIC src/layout/jianpulayout.cpp src/layout/jianpulayout.h src/layout/layoutsettings.cpp src/layout/layoutsettings.h
    src/ui/scorecanvas.cpp src/ui/scorecanvas.h src/ui/mainwindow.cpp src/ui/mainwindow.h
    src/ui/sourceeditor.cpp src/ui/sourceeditor.h src/ui/layoutpanel.cpp src/ui/layoutpanel.h
    src/input/inputcontroller.cpp src/input/inputcontroller.h)
target_include_directories(jianpu_editor PUBLIC src)
target_link_libraries(jianpu_editor PUBLIC jianpu_document Qt6::Widgets)
qt_add_resources(jianpu_editor jianpu_fonts PREFIX "/fonts" FILES
    assets/fonts/NotoSerifCJKsc-Regular.otf assets/fonts/NotoSansCJKsc-Regular.otf assets/fonts/DejaVuSans.ttf)
target_link_libraries(jianpu_lab PRIVATE jianpu_editor)
install(TARGETS jianpu_lab RUNTIME DESTINATION bin)
extdeps_install_consumed()
install(FILES LICENSE README.md THIRD_PARTY_NOTICES.md DESTINATION .)
install(DIRECTORY examples/ DESTINATION examples)
install(DIRECTORY licenses/ DESTINATION licenses)
install(DIRECTORY "${JIANPU_MUSESCORE_SOURCE}/fonts/" DESTINATION licenses/fonts
    FILES_MATCHING PATTERN "*LICENSE*" PATTERN "*COPYING*" PATTERN "OFL*")
install(FILES "${JIANPU_MUSESCORE_SOURCE}/LICENSE.txt" DESTINATION licenses/MuseScore)
foreach(_library kors_async kors_profiler kors_modularity kors_logger kors_msgpack)
    install(FILES "${JIANPU_MUSESCORE_SOURCE}/muse/framework/global/thirdparty/${_library}/LICENSE"
        DESTINATION "licenses/${_library}")
endforeach()
install(FILES "${JIANPU_MUSESCORE_SOURCE}/src/importexport/midi/thirdparty/beatroot/COPYING"
    DESTINATION licenses/beatroot)
foreach(_library picojson pugixml utfcpp)
    get_property(_prefix GLOBAL PROPERTY ${_library}_PREFIX)
    install(DIRECTORY "${_prefix}/${_library}/" DESTINATION "licenses/${_library}"
        FILES_MATCHING PATTERN "LICENSE*" PATTERN "COPYING*")
endforeach()

include(CTest)
if(BUILD_TESTING)
    qt_add_executable(tst_textinput tests/tst_textinput.cpp)
    set_target_properties(tst_textinput PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
    target_link_libraries(tst_textinput PRIVATE jianpu_editor Qt6::Test)
    add_test(NAME textinput COMMAND tst_textinput)
    set_tests_properties(textinput PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
    add_test(NAME app_smoke COMMAND jianpu_lab --smoke)
    set_tests_properties(app_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 15)
    qt_add_executable(tst_editor tests/tst_editor.cpp)
    set_target_properties(tst_editor PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
    target_link_libraries(tst_editor PRIVATE jianpu_editor Qt6::Test)
    target_compile_definitions(tst_editor PRIVATE EXAMPLE_SCORE="${CMAKE_CURRENT_SOURCE_DIR}/examples/ensemble.musicxml")
    add_test(NAME editor COMMAND tst_editor)
    set_tests_properties(editor PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
    qt_add_executable(tst_document tests/tst_document.cpp)
    set_target_properties(tst_document PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
    target_link_libraries(tst_document PRIVATE jianpu_document Qt6::Test)
    target_compile_definitions(tst_document PRIVATE EXAMPLE_SCORE="${CMAKE_CURRENT_SOURCE_DIR}/examples/ensemble.musicxml")
    add_test(NAME document COMMAND tst_document)
    set_tests_properties(document PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
    qt_add_executable(tst_scorewidget tests/tst_scorewidget.cpp)
    target_link_libraries(tst_scorewidget PRIVATE jianpu_widget Qt6::Test)
    add_test(NAME scorewidget COMMAND tst_scorewidget)

    qt_add_executable(tst_qtruntime tests/tst_qtruntime.cpp)
    # Inline QML is loaded by the runtime probe. Do not scan the unrelated reference checkout.
    set_target_properties(tst_qtruntime PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
    target_link_libraries(tst_qtruntime PRIVATE Qt6::Widgets Qt6::Test Qt6::Xml Qt6::Svg
        Qt6::PrintSupport Qt6::Multimedia Qt6::Qml Qt6::Quick Qt6::QuickControls2)
    add_test(NAME qtruntime COMMAND tst_qtruntime)
    set_tests_properties(scorewidget qtruntime PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
        TIMEOUT 30)
endif()
