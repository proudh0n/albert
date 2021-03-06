cmake_minimum_required(VERSION 2.8.11)

PROJECT(files)

find_package(Qt5Widgets 5.2 REQUIRED)

include_directories(
    ../../application
    ../../application/pluginsystem
)

FILE(GLOB_RECURSE SRC src/*)

set(LIB
    ${Qt5Widgets_LIBRARIES}
    albertcore
    xdg
)

qt5_wrap_ui(UI
    forms/configwidget.ui
    forms/mimetypechooser.ui
    forms/mimetypedialog.ui
)

# Define the target
add_library(${PROJECT_NAME} SHARED ${SRC} ${UI})

# Set INCLUDE_DIRS and INTERFACE_INCLUDE_DIRS
target_include_directories(${PROJECT_NAME} PRIVATE src/ forms/)

# Link target to libraries
target_link_libraries(${PROJECT_NAME} ${LIB})

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION share/albert/plugins)
endif(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
