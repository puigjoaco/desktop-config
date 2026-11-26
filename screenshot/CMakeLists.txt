cmake_minimum_required(VERSION 3.22)
project(quick-region-overlay LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui DBus Widgets Test)
find_package(LayerShellQt REQUIRED)
find_package(KF6GuiAddons REQUIRED)
find_package(KF6GlobalAccel REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WaylandClient REQUIRED IMPORTED_TARGET wayland-client)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
# wayland-protocols 1.47, staging/ext-data-control/ext-data-control-v1.xml, unchanged upstream protocol.
add_custom_command(OUTPUT ext-data-control-v1-client-protocol.h
    COMMAND ${WAYLAND_SCANNER} client-header ${CMAKE_CURRENT_SOURCE_DIR}/ext-data-control-v1.xml ${CMAKE_CURRENT_BINARY_DIR}/ext-data-control-v1-client-protocol.h
    DEPENDS ext-data-control-v1.xml)
add_custom_command(OUTPUT ext-data-control-v1-protocol.c
    COMMAND ${WAYLAND_SCANNER} private-code ${CMAKE_CURRENT_SOURCE_DIR}/ext-data-control-v1.xml ${CMAKE_CURRENT_BINARY_DIR}/ext-data-control-v1-protocol.c
    DEPENDS ext-data-control-v1.xml)
add_executable(quick-region-overlay quick-region-overlay.cpp selection-geometry.h capture-checks.h daemon-logic.h clipboard-image.h
    clipboard-stack.h stack-storage.h stack-controller.h stack-controller.cpp stack-busy.h stack-busy.cpp stack-clipboard.h stack-clipboard.cpp stack-holder.h stack-holder.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/ext-data-control-v1-client-protocol.h ${CMAKE_CURRENT_BINARY_DIR}/ext-data-control-v1-protocol.c)
target_include_directories(quick-region-overlay PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(quick-region-overlay PRIVATE Qt6::Core Qt6::Gui Qt6::DBus Qt6::Widgets LayerShellQt::Interface KF6::GuiAddons KF6::GlobalAccel PkgConfig::WaylandClient)
install(TARGETS quick-region-overlay RUNTIME DESTINATION bin)

enable_testing()
add_executable(selection-geometry-test tests/selection-geometry-test.cpp selection-geometry.h)
target_link_libraries(selection-geometry-test PRIVATE Qt6::Core Qt6::Test)
add_test(NAME selection-geometry COMMAND selection-geometry-test)
add_executable(capture-checks-test tests/capture-checks-test.cpp capture-checks.h)
target_link_libraries(capture-checks-test PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME capture-checks COMMAND capture-checks-test)
add_executable(daemon-logic-test tests/daemon-logic-test.cpp daemon-logic.h)
target_link_libraries(daemon-logic-test PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME daemon-logic COMMAND daemon-logic-test)
add_executable(clipboard-image-test tests/clipboard-image-test.cpp clipboard-image.h)
target_link_libraries(clipboard-image-test PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME clipboard-image COMMAND clipboard-image-test)
add_executable(clipboard-stack-test tests/clipboard-stack-test.cpp clipboard-stack.h stack-storage.h)
target_link_libraries(clipboard-stack-test PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME clipboard-stack COMMAND clipboard-stack-test)
add_test(NAME scripts COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/check-scripts.sh)
add_test(NAME script-helpers COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/script-helpers-test.sh)

# Manual, isolated Wayland integration harness (not run against a user's desktop by ctest).
add_library(stack-modifiers SHARED EXCLUDE_FROM_ALL tests/stack-modifiers.c)
target_link_libraries(stack-modifiers PRIVATE ${CMAKE_DL_LIBS})
add_custom_command(OUTPUT fake-input-client-protocol.h
    COMMAND ${WAYLAND_SCANNER} client-header ${CMAKE_CURRENT_SOURCE_DIR}/tests/fake-input.xml ${CMAKE_CURRENT_BINARY_DIR}/fake-input-client-protocol.h
    DEPENDS tests/fake-input.xml)
add_custom_command(OUTPUT fake-input-protocol.c
    COMMAND ${WAYLAND_SCANNER} private-code ${CMAKE_CURRENT_SOURCE_DIR}/tests/fake-input.xml ${CMAKE_CURRENT_BINARY_DIR}/fake-input-protocol.c
    DEPENDS tests/fake-input.xml)
add_executable(stack-input EXCLUDE_FROM_ALL tests/stack-input.cpp ${CMAKE_CURRENT_BINARY_DIR}/fake-input-client-protocol.h ${CMAKE_CURRENT_BINARY_DIR}/fake-input-protocol.c)
add_dependencies(stack-input stack-modifiers)
target_include_directories(stack-input PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(stack-input PRIVATE PkgConfig::WaylandClient)
add_executable(stack-probe EXCLUDE_FROM_ALL tests/stack-probe.cpp stack-clipboard.cpp stack-clipboard.h clipboard-stack.h
    ${CMAKE_CURRENT_BINARY_DIR}/ext-data-control-v1-client-protocol.h ${CMAKE_CURRENT_BINARY_DIR}/ext-data-control-v1-protocol.c)
target_include_directories(stack-probe PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(stack-probe PRIVATE Qt6::Core Qt6::Gui Qt6::DBus Qt6::Widgets PkgConfig::WaylandClient)
find_package(Qt6 QUIET COMPONENTS Qml)
if (TARGET Qt6::Qml)
    add_executable(stack-klipper EXCLUDE_FROM_ALL tests/stack-klipper.cpp)
    target_link_libraries(stack-klipper PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Qml)
endif()
