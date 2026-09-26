cmake_minimum_required(VERSION 3.20)
project(DOS VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(dos src/dos.cpp)
target_compile_definitions(dos PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX DOS_VERSION_MAJOR=1 DOS_VERSION_MINOR=0)

if (MSVC)
    target_compile_options(dos PRIVATE /W4 /WX /permissive- /EHsc /O2 /GL /guard:cf)
    target_link_options(dos PRIVATE /LTCG /OPT:REF /OPT:ICF /GUARD:CF /DYNAMICBASE /NXCOMPAT)
elseif (WIN32)
    target_compile_options(dos PRIVATE -Wall -Wextra -Wpedantic -Werror -O3 -flto -ffunction-sections -fdata-sections)
    target_link_options(dos PRIVATE -flto -Wl,--gc-sections,--dynamicbase,--nxcompat)
else()
    target_compile_options(dos PRIVATE -Wall -Wextra -Wpedantic -Werror -O3 -flto -ffunction-sections -fdata-sections)
    target_link_options(dos PRIVATE -flto -Wl,--gc-sections)
endif()

set_property(TARGET dos PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)

add_custom_command(TARGET dos POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/plugins"
            "$<TARGET_FILE_DIR:dos>/plugins"
)

if (WIN32)
    target_link_libraries(dos PRIVATE advapi32 iphlpapi ws2_32 shell32)
endif()

install(TARGETS dos RUNTIME DESTINATION .)
install(DIRECTORY plugins DESTINATION .)
