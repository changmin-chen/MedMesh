cmake_minimum_required(VERSION 3.29)
project(med_mesh LANGUAGES CXX)

if (NOT EMSCRIPTEN)
    message(FATAL_ERROR "This project must be built inside an Emscripten environment (e.g. kitware/vtk-wasm-sdk).")
endif ()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(VTK REQUIRED COMPONENTS
        IOImage
        FiltersCore
        FiltersSources
        InteractionStyle
        RenderingOpenGL2
        RenderingUI
)

add_executable(med_mesh main.cpp)

target_link_libraries(med_mesh PRIVATE
        VTK::IOImage
        VTK::FiltersCore
        VTK::FiltersSources
        VTK::InteractionStyle
        VTK::RenderingOpenGL2
        VTK::RenderingUI
)

# ---- Emscripten link options (fixed) ----
target_link_options(med_mesh PRIVATE
        "-lembind"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sFORCE_FILESYSTEM=1"
        # Export FS so JS can use Module.FS.* without abort()
        "SHELL:-sEXPORTED_RUNTIME_METHODS=['FS']"
        # Debugging sanity: get real messages instead of "unreachable"
        "-sASSERTIONS=1"
        "-sEXIT_RUNTIME=0"
)

vtk_module_autoinit(
        TARGETS med_mesh
        MODULES
        VTK::IOImage
        VTK::FiltersCore
        VTK::FiltersSources
        VTK::InteractionStyle
        VTK::RenderingOpenGL2
        VTK::RenderingUI
)

add_custom_command(
        TARGET med_mesh
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/web/index.html"
        "$<TARGET_FILE_DIR:med_mesh>/index.html"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/web/app.js"
        "$<TARGET_FILE_DIR:med_mesh>/app.js"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/web/style.css"
        "$<TARGET_FILE_DIR:med_mesh>/style.css"
)
