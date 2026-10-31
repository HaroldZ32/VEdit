cmake_minimum_required(VERSION 3.20)
project(fold_probe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(KF6TextEditor REQUIRED)
find_package(KF6SyntaxHighlighting REQUIRED)

add_executable(fold-probe src/main.cpp)
target_link_libraries(fold-probe PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    KF6::TextEditor
    KF6::SyntaxHighlighting
)

message(STATUS "KTextEditor include dirs: ${KF6TextEditor_INCLUDE_DIRS}")
message(STATUS "KTextEditor version:      ${KF6TextEditor_VERSION}")
message(STATUS "KSyntaxHighlighting:      ${KF6SyntaxHighlighting_VERSION}")
