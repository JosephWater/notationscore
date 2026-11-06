cmake_minimum_required(VERSION 3.28)
project(JianpuLab VERSION 0.6.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.10.2 EXACT REQUIRED COMPONENTS
    Core Gui Widgets Xml Svg PrintSupport Multimedia Qml Quick QuickControls2 Test LinguistTools Core5Compat Concurrent Network)
message(STATUS "JianpuLab Qt ${Qt6_VERSION}: ${Qt6_DIR}")
qt_standard_project_setup(REQUIRES 6.10)

add_subdirectory(cmake/engine)

qt_add_library(jianpu_notation STATIC src/input/textformat.cpp src/input/textformat.h
    src/input/textmarks.cpp src/input/textmarks.h src/input/textreflow.cpp src/input/lyricsourcemap.cpp src/input/lyricsourcemap.h src/core/musicaltime.h src/core/scoretypes.h)
target_include_directories(jianpu_notation PUBLIC src)
target_link_libraries(jianpu_notation PUBLIC Qt6::Core)
qt_add_library(jianpu_presentation STATIC src/layout/jianpulayout.cpp src/layout/jianpulayout.h
    src/layout/scorelayout.h src/layout/visualelement.h src/layout/visualelement.cpp
    src/layout/markplacement.h src/layout/markplacement.cpp
    src/layout/alignmentsnapper.h src/layout/alignmentsnapper.cpp
    src/layout/layoutsettings.cpp src/layout/layoutsettings.h
    src/presentation/scoreprojection.cpp src/presentation/scoreprojection.h)
target_include_directories(jianpu_presentation PUBLIC src)
target_link_libraries(jianpu_presentation PUBLIC jianpu_notation Qt6::Gui)
qt_add_library(jianpu_document STATIC src/core/runtime.cpp src/core/runtime.h
    src/core/scoredocument.cpp src/core/scoredocument_text.cpp src/core/scoredocument_layout.cpp
    src/core/scoredocument_fragments.cpp src/core/scoredocument_clone.cpp
    src/core/scoredocument.h)
target_include_directories(jianpu_document PUBLIC src)
target_compile_definitions(jianpu_document PUBLIC JIANPU_APP_VERSION="${PROJECT_VERSION}")
target_link_libraries(jianpu_document PUBLIC jianpu_presentation Qt6::Widgets PRIVATE jianpu_muse_core Qt6::Xml)
if(MSVC)
    target_compile_options(jianpu_notation PUBLIC /utf-8 /bigobj)
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
qt_add_library(jianpu_ai STATIC src/ai/aiconfig.cpp src/ai/aiconfig.h
    src/ai/aiclient.cpp src/ai/aiclient.h src/ai/recognitiontask.cpp src/ai/recognitiontask.h)
target_include_directories(jianpu_ai PUBLIC src)
target_link_libraries(jianpu_ai PUBLIC jianpu_document Qt6::Network)
qt_add_executable(jianpu_ai_probe tools/airecognitionprobe.cpp)
set_target_properties(jianpu_ai_probe PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
target_link_libraries(jianpu_ai_probe PRIVATE jianpu_ai)
qt_add_resources(jianpu_ai jianpu_ai_prompt PREFIX "/ai" BASE src/ai FILES src/ai/recognition-system-v1.txt)
qt_add_library(jianpu_editor STATIC
    src/ui/airecognition.cpp src/ui/airecognition.h src/ui/scorecanvas.cpp src/ui/scorecanvas_selection.cpp src/ui/scorecanvas.h src/ui/mainwindow.cpp src/ui/mainwindow.h
    src/ui/sourceeditor.cpp src/ui/sourceeditor.h src/ui/layoutpanel.cpp src/ui/layoutpanel.h
    src/input/inputcontroller.cpp src/input/inputcontroller.h
    src/input/scoreentrycontroller.cpp src/input/scoreentrycontroller.h
    src/input/lyricentrycontroller.cpp src/input/lyricentrycontroller.h
    src/input/selectionpolicy.cpp src/input/selectionpolicy.h
    src/input/musictextediting.cpp src/input/musictextediting.h src/input/entrysession.h)
target_include_directories(jianpu_editor PUBLIC src)
target_link_libraries(jianpu_editor PUBLIC jianpu_document jianpu_ai Qt6::Widgets)
qt_add_resources(jianpu_presentation jianpu_fonts PREFIX "/fonts" FILES
    assets/fonts/NotoSerifCJKsc-Regular.otf assets/fonts/NotoSansCJKsc-Regular.otf assets/fonts/DejaVuSans.ttf)
qt_add_resources(jianpu_presentation jianpu_symbols PREFIX "/musicfonts" BASE "${JIANPU_MUSESCORE_SOURCE}/fonts/bravura" FILES "${JIANPU_MUSESCORE_SOURCE}/fonts/bravura/Bravura.otf")
target_link_libraries(jianpu_lab PRIVATE jianpu_editor)
install(TARGETS jianpu_lab RUNTIME DESTINATION bin)
extdeps_install_consumed()
install(FILES LICENSE README.md THIRD_PARTY_NOTICES.md DESTINATION .)
install(DIRECTORY docs/ DESTINATION docs FILES_MATCHING PATTERN "*.md")
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
    qt_add_executable(tst_notation tests/tst_notation.cpp)
    set_target_properties(tst_notation PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
    target_link_libraries(tst_notation PRIVATE jianpu_editor Qt6::Test)
    target_compile_definitions(tst_notation PRIVATE NOTATION_EXAMPLE="${CMAKE_CURRENT_SOURCE_DIR}/examples/ai-recognition.jptxt")
    add_test(NAME notation COMMAND tst_notation)
    set_tests_properties(notation PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
    qt_add_executable(tst_ai tests/tst_ai.cpp)
    set_target_properties(tst_ai PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
    target_link_libraries(tst_ai PRIVATE jianpu_editor Qt6::Test)
    add_test(NAME ai COMMAND tst_ai)
    set_tests_properties(ai PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
    qt_add_executable(tst_selection tests/tst_selection.cpp)
    set_target_properties(tst_selection PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
    target_link_libraries(tst_selection PRIVATE jianpu_editor Qt6::Test)
    add_test(NAME selection COMMAND tst_selection)
    set_tests_properties(selection PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
    qt_add_executable(tst_lyrics tests/tst_lyrics.cpp)
    set_target_properties(tst_lyrics PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
    target_link_libraries(tst_lyrics PRIVATE jianpu_editor Qt6::Test)
    add_test(NAME lyrics COMMAND tst_lyrics)
    set_tests_properties(lyrics PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
    qt_add_executable(tst_projection tests/tst_projection.cpp)
    set_target_properties(tst_projection PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
    target_link_libraries(tst_projection PRIVATE jianpu_editor Qt6::Test)
    target_compile_definitions(tst_projection PRIVATE RHYTHM_SCORE="${CMAKE_CURRENT_SOURCE_DIR}/examples/rhythm.musicxml")
    add_test(NAME projection COMMAND tst_projection)
    set_tests_properties(projection PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
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
