# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources "${CMAKE_SOURCE_DIR}/src/*.*")
FILE(GLOB_RECURSE web_static "${CMAKE_SOURCE_DIR}/static/*.*")

idf_component_register(SRCS "${app_sources}"
                       EMBED_TXTFILES "${web_static}")
