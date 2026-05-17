cmake_minimum_required(VERSION 3.20)

project(stringtable)

# Override CMake's hardcoded /MANIFEST:EMBED in the link template
# so no default manifest is embedded (we use external manifest)
string(REPLACE "-Xlinker /MANIFEST:EMBED" "-Xlinker /MANIFEST:NO" CMAKE_CXX_LINK_EXECUTABLE "${CMAKE_CXX_LINK_EXECUTABLE}")
# Also do the same for C (copies from CXX)
string(REPLACE "-Xlinker /MANIFEST:EMBED" "-Xlinker /MANIFEST:NO" CMAKE_C_LINK_EXECUTABLE "${CMAKE_C_LINK_EXECUTABLE}")

add_executable(stringtable main.cpp StringTable.cpp)

target_link_libraries(stringtable comctl32 comdlg32 uxtheme)

target_link_options(stringtable PRIVATE "LINKER:/SUBSYSTEM:WINDOWS")

add_custom_command(TARGET stringtable POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/app.manifest"
        "$<TARGET_FILE_DIR:stringtable>/stringtable.exe.manifest"
)

set_target_properties(stringtable PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)
