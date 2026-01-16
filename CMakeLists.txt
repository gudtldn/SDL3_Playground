cmake_minimum_required(VERSION 4.0)
project(SDL3_Playground)

# C++ 표준 설정
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- 아키텍처 이름 설정 (x64 / x86) ---
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(PROJECT_ARCHITECTURE "x64")
else ()
    set(PROJECT_ARCHITECTURE "x86")
endif ()

# 출력 디렉토리 설정
set(BINARY_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/Binaries/${CMAKE_SYSTEM_NAME}/${PROJECT_ARCHITECTURE})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${BINARY_OUTPUT_PATH}/${CMAKE_BUILD_TYPE}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${BINARY_OUTPUT_PATH}/${CMAKE_BUILD_TYPE})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${BINARY_OUTPUT_PATH}/${CMAKE_BUILD_TYPE})
set(CMAKE_PDB_OUTPUT_DIRECTORY ${BINARY_OUTPUT_PATH}/${CMAKE_BUILD_TYPE})

# Visual Studio 다중 구성 생성기를 위한 설정
foreach (OUTPUT_CONFIG ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${OUTPUT_CONFIG} OUTPUT_CONFIG_UPPER)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${OUTPUT_CONFIG_UPPER} ${BINARY_OUTPUT_PATH}/${OUTPUT_CONFIG}/lib)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUT_CONFIG_UPPER} ${BINARY_OUTPUT_PATH}/${OUTPUT_CONFIG})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${OUTPUT_CONFIG_UPPER} ${BINARY_OUTPUT_PATH}/${OUTPUT_CONFIG})
    set(CMAKE_PDB_OUTPUT_DIRECTORY_${OUTPUT_CONFIG_UPPER} ${BINARY_OUTPUT_PATH}/${OUTPUT_CONFIG})
endforeach ()

# UTF-8 인코딩 사용
if (MSVC)
    add_compile_options(
            /utf-8
            /Zc:preprocessor # MSVC 전처리기 최신 표준 기능을 활성화
            /W4              # 경고 수준을 최고 수준으로 설정
            /MP              # 멀티스레드 빌드 사용
            /wd4251 /wd4275
    )
else ()
    add_compile_options(
            -finput-charset=UTF-8
            -fexec-charset=UTF-8
            -Wall -Wextra
    )
endif ()

# 컴파일러 빌드 설정
add_compile_definitions(
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<CONFIG:Release>:NDEBUG>
)

add_executable(SDL3_Playground
        SDL3_Playground/main.cpp
        SDL3_Playground/App.cpp
        ThirdParty/SimpleEngine/Editor/Source/Rendering/Compiler/Compiler.cpp
        ThirdParty/SimpleEngine/Editor/Source/Rendering/Compiler/Provider.cpp
)

add_subdirectory(ThirdParty)

find_package(SDL3 REQUIRED)
target_link_libraries(SDL3_Playground PRIVATE
        EngineCore

        # Tracy
        Tracy::TracyClient

        # SDL3
        SDL3::SDL3
        SDL3_shadercross::SDL3_shadercross

        # ImGui
        imgui
)

target_include_directories(SDL3_Playground PRIVATE
        # 내부 구현에서 짧은 경로를 사용하기 위한 경로
        ${CMAKE_CURRENT_SOURCE_DIR}/SDL3_Playground
        ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/SimpleEngine/Editor/Source
)

target_compile_definitions(SDL3_Playground PRIVATE
        PROJECT_ROOT_DIR="${PROJECT_SOURCE_DIR}"
)
