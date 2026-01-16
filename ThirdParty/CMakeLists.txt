add_subdirectory(SimpleEngine/EngineCore)
add_subdirectory(SimpleEngine/ThirdParty)

set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "If you want to build assimp tests" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "If you want to install assimp" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "If you want to build assimp samples" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "If you want to build assimp tools" FORCE)
add_subdirectory(assimp)
