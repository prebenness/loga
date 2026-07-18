cmake_minimum_required(VERSION 3.21)

project(loga)

set(Boost_NO_BOOST_CMAKE        ON  CACHE BOOL "Use FindBoost module, not BoostConfig")
set(Boost_USE_STATIC_LIBS       OFF)
set(Boost_USE_MULTITHREADED     ON)
set(Boost_USE_STATIC_RUNTIME    OFF)

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

if (MSVC)
  set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

find_package(Boost REQUIRED COMPONENTS program_options)
if(DEFINED CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg.cmake")
  find_package(Armadillo CONFIG REQUIRED)
else()
  find_package(Armadillo REQUIRED)
endif()
find_package(cereal REQUIRED)
find_package(igraph REQUIRED)

if(NOT TARGET Armadillo::Armadillo)
  if(TARGET armadillo)  # vcpkg / config package target
    add_library(Armadillo::Armadillo INTERFACE IMPORTED)
    set_property(TARGET Armadillo::Armadillo
      PROPERTY INTERFACE_LINK_LIBRARIES armadillo)
  elseif(DEFINED ARMADILLO_LIBRARIES OR DEFINED ARMADILLO_INCLUDE_DIRS) # FindArmadillo.cmake style
    add_library(Armadillo::Armadillo INTERFACE IMPORTED)
    set_target_properties(Armadillo::Armadillo PROPERTIES
      INTERFACE_LINK_LIBRARIES     "${ARMADILLO_LIBRARIES}"
      INTERFACE_INCLUDE_DIRECTORIES "${ARMADILLO_INCLUDE_DIRS}")
  else()
    message(FATAL_ERROR "Cannot construct Armadillo::Armadillo: neither 'armadillo' target nor ARMADILLO_LIBRARIES/INCLUDE_DIRS are set")
  endif()
endif()


include_directories(includes)

set(SOURCES
  sources/index.cpp
  sources/face.cpp
  sources/collection.cpp
  sources/alignment.cpp
  sources/multi_alignment.cpp
  sources/segment.cpp
  sources/graph.cpp
  sources/path.cpp
  sources/zone.cpp
  sources/distance.cpp
  sources/group.cpp
  sources/bio_alignment.cpp
  sources/tokenized_collection.cpp
  sources/token.cpp
  sources/tokenized_group.cpp
  sources/tokenized_alignment.cpp
  sources/tokenized_distance.cpp
  sources/tokenized_multi_alignment.cpp
  sources/knn_cluster.cpp
  sources/community.cpp
  sources/automata.cpp
  sources/outliers.cpp
  sources/pattern_sequence.cpp
  sources/template_matcher.cpp
  sources/template_output.cpp
)

set(INCLUDES
  includes/loga/fwd.h
  includes/loga/face.h
  includes/loga/index.h
  includes/loga/collection.h
  includes/loga/alignment.h
  includes/loga/multi_alignment.h
  includes/loga/segment.h
  includes/loga/graph.h
  includes/loga/path.h
  includes/loga/zone.h
  includes/loga/package.h
  includes/loga/distance.h
  includes/loga/group.h
  includes/loga/bio_alignment.h
  includes/loga/token.h
  includes/loga/tokenized_collection.h
  includes/loga/tokenized_group.h
  includes/loga/tokenized_alignment.h
  includes/loga/tokenized_distance.h
  includes/loga/tokenized_multi_alignment.h
  includes/loga/colors.h
  includes/loga/knn_cluster.h
  includes/loga/community.h
  includes/loga/automata.h
  includes/loga/pattern_sequence.h
  includes/loga/outliers.h
  includes/loga/template_matcher.h
  includes/loga/template_output.h
)

add_library(loga_lib SHARED ${SOURCES} ${INCLUDES})
set_property(TARGET loga_lib PROPERTY CXX_STANDARD 20)
target_link_libraries(loga_lib PUBLIC Boost::headers igraph::igraph)
target_link_libraries(loga_lib PRIVATE cereal::cereal Armadillo::Armadillo)
set_target_properties(loga_lib PROPERTIES OUTPUT_NAME "loga")
if (WIN32)
    set_target_properties(loga_lib PROPERTIES ARCHIVE_OUTPUT_NAME "loga_lib")
endif()

add_executable(loga_exe sources/main.cpp sources/match_command.cpp sources/match_command.h)
set_property(TARGET loga_exe PROPERTY CXX_STANDARD 20)
target_link_libraries(loga_exe PRIVATE Boost::headers Boost::program_options Armadillo::Armadillo loga_lib)
set_property(TARGET loga_exe PROPERTY OUTPUT_NAME loga)

include(CTest)
if(BUILD_TESTING)
  add_executable(loga_levenshtein_distance_test tests/levenshtein_distance_test.cpp)
  set_property(TARGET loga_levenshtein_distance_test PROPERTY CXX_STANDARD 20)
  target_link_libraries(loga_levenshtein_distance_test PRIVATE Boost::headers)
  add_test(NAME levenshtein_distance COMMAND loga_levenshtein_distance_test)

  add_executable(loga_template_matcher_test tests/template_matcher_test.cpp)
  set_property(TARGET loga_template_matcher_test PROPERTY CXX_STANDARD 20)
  target_link_libraries(loga_template_matcher_test PRIVATE loga_lib)
  add_test(NAME template_matcher COMMAND loga_template_matcher_test)

  add_executable(loga_template_output_test tests/template_output_test.cpp)
  set_property(TARGET loga_template_output_test PROPERTY CXX_STANDARD 20)
  target_link_libraries(loga_template_output_test PRIVATE loga_lib)
  add_test(NAME template_output COMMAND loga_template_output_test)

  add_executable(
    loga_match_command_test
    tests/match_command_test.cpp
    sources/match_command.cpp
    sources/match_command.h
  )
  set_property(TARGET loga_match_command_test PROPERTY CXX_STANDARD 20)
  target_include_directories(loga_match_command_test PRIVATE sources)
  target_link_libraries(
    loga_match_command_test
    PRIVATE Boost::program_options loga_lib
  )
  add_test(NAME match_command COMMAND loga_match_command_test)
endif()

include(GNUInstallDirs)

if(APPLE)
  set_target_properties(loga_exe PROPERTIES
      INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}"
      BUILD_RPATH   "@loader_path/../${CMAKE_INSTALL_LIBDIR}"
  )
elseif(UNIX)
  set_target_properties(loga_exe PROPERTIES
      INSTALL_RPATH "\$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
      BUILD_RPATH   "\$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
  )
endif()

install(TARGETS loga_lib loga_exe
    RUNTIME_DEPENDENCY_SET loga_runtime_deps
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

set(LOGA_RUNTIME_DEP_DIRS "")

# On Windows, help CMake find DLLs in common places
if(WIN32)
  list(APPEND LOGA_RUNTIME_DEP_DIRS
    "$ENV{SystemRoot}/System32"
    "$ENV{SystemRoot}/SysWOW64"
  )
endif()

if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
  list(APPEND LOGA_RUNTIME_DEP_DIRS
    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
  )
endif()

install(RUNTIME_DEPENDENCY_SET loga_runtime_deps
  # DESTINATION ${CMAKE_INSTALL_DATADIR}/loga/deps
  # You can also use DESTINATION "deps" if you want stage/deps directly
  DESTINATION "deps"
  # You usually want to exclude system libs; examples:
  PRE_EXCLUDE_REGEXES
    "^(api|ext)-ms-.*"
    "^(azureattestmanager|azureattestnormal|hvsifiletrust|pdmutilities|wtdsensor|wpaxholder|wtdccm)\\.dll$"
  POST_EXCLUDE_REGEXES
    # Windows System libs
    "^(KERNEL32|USER32|GDI32|ADVAPI32|SHELL32|OLE32|OLEAUT32|WS2_32|CRYPT32|COMDLG32|bcrypt|ntdll)\\.dll$"
    # Linux system libs
    ".*/libc\\.so(\\..*)?$"
    ".*/libm\\.so(\\..*)?$"
    ".*/libpthread\\.so(\\..*)?$"
    ".*/libdl\\.so(\\..*)?$"
    ".*/libgcc_s\\.so(\\..*)?$"
    ".*/libstdc\\+\\+\\.so(\\..*)?$"
    ".*/librt\\.so(\\..*)?$"
    ".*/libutil\\.so(\\..*)?$"
    ".*/ld-linux-.*\\.so(\\..*)?$"
    ".*/libz\\.so(\\..*)?$"
    ".*/libxml2\\.so(\\..*)?$"
    ".*/libzstd\\.so(\\..*)?$"
    ".*/libudev\\.so(\\..*)?$"
    ".*/libevent.*\\.so(\\..*)?$"
    # macOS system libs
    "^/System/.*"
    "^/usr/lib/.*"

  DIRECTORIES 
    ${LOGA_RUNTIME_DEP_DIRS}
)

install(FILES ${INCLUDES}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/loga
)

# Install wrappers into the install-prefix root (i.e., stage/)
if(WIN32)
  install(FILES "${CMAKE_SOURCE_DIR}/pack/loga.cmd" DESTINATION ".")
else()
  install(PROGRAMS "${CMAKE_SOURCE_DIR}/pack/loga" DESTINATION ".")
endif()

install(DIRECTORY "${CMAKE_SOURCE_DIR}/logs/"
  DESTINATION "logs"
  FILES_MATCHING
  PATTERN "*"
)
