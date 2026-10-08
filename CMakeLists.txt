# Forest Launcher is licensed under GPL-3.0-only. See LICENSE for the terms.
cmake_minimum_required(VERSION 3.20)
project(ForestLauncher VERSION 0.22.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)

option(FOREST_ARTWORK_PREVIEW "Enable the independent native artwork preview" ON)
add_compile_definitions(FOREST_ARTWORK_PREVIEW=$<BOOL:${FOREST_ARTWORK_PREVIEW}>)

add_executable(forest-launcher
    src/main.cpp
    src/backendclient.cpp
    src/gamedialog.cpp
    src/gameoptionswidget.cpp
    src/settingsdialog.cpp
    src/mainwindow.cpp
    src/newsdialog.cpp
    src/welcomedialog.cpp
    src/startupnewsdialog.cpp
    src/protonmanager.cpp
)
target_link_libraries(forest-launcher PRIVATE Qt6::Widgets)
target_compile_definitions(forest-launcher PRIVATE
    FOREST_SOURCE_BACKEND_DIR="${CMAKE_CURRENT_SOURCE_DIR}/backend"
    FOREST_APPLICATION_VERSION="${PROJECT_VERSION}"
)
target_compile_options(forest-launcher PRIVATE -Wall -Wextra -Wpedantic)

include(GNUInstallDirs)
install(TARGETS forest-launcher RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(DIRECTORY backend/forest_backend DESTINATION "${CMAKE_INSTALL_DATADIR}/forest-launcher/backend"
    FILES_MATCHING PATTERN "*.py")
install(FILES data/io.github.Benaeo.forest-launcher.desktop DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
install(FILES LICENSE DESTINATION "${CMAKE_INSTALL_DATADIR}/forest-launcher")
install(FILES docs/CHANGELOG.md DESTINATION "${CMAKE_INSTALL_DATADIR}/forest-launcher")
