#!/usr/bin/env bash
# Dependencies for the custom Celonis libraries
BOOST_DIR_NAME_FOR_CELONIS_LIBRARIES="boost_1_84_0"
BOOST_ARCHIVE_NAME_FOR_CELONIS_LIBRARIES="${BOOST_DIR_NAME_FOR_CELONIS_LIBRARIES}.tar.gz"
BOOST_DOWNLOAD_FOR_CELONIS_LIBRARIES="https://archives.boost.io/release/1.84.0/source/${BOOST_ARCHIVE_NAME_FOR_CELONIS_LIBRARIES}"

NLOHMANN_JSON_DIR_NAME_FOR_CELONIS_LIBRARIES="v3.10.5"
NLOHMANN_JSON_ARCHIVE_NAME_FOR_CELONIS_LIBRARIES="${NLOHMANN_JSON_DIR_NAME_FOR_CELONIS_LIBRARIES}.tar.gz"
NLOHMANN_JSON_DOWNLOAD_FOR_CELONIS_LIBRARIES="https://github.com/nlohmann/json/archive/refs/tags/${NLOHMANN_JSON_ARCHIVE_NAME_FOR_CELONIS_LIBRARIES}"

FMT_DIR_NAME_FOR_CELONIS_LIBRARIES="fmt-11.1.4"
FMT_ARCHIVE_NAME_FOR_CELONIS_LIBRARIES="${FMT_DIR_NAME_FOR_CELONIS_LIBRARIES}.zip"
FMT_DOWNLOAD_FOR_CELONIS_LIBRARIES="https://github.com/fmtlib/fmt/releases/download/11.1.4/${FMT_ARCHIVE_NAME_FOR_CELONIS_LIBRARIES}"

TBB_DIR_NAME_FOR_CELONIS_LIBRARIES="oneapi-tbb-2021.11.0-lin"
TBB_ARCHIVE_NAME_FOR_CELONIS_LIBRARIES="${TBB_DIR_NAME_FOR_CELONIS_LIBRARIES}.tgz"
TBB_DOWNLOAD_FOR_CELONIS_LIBRARIES="https://github.com/oneapi-src/oneTBB/releases/download/v2021.11.0/${TBB_ARCHIVE_NAME_FOR_CELONIS_LIBRARIES}"

SKA_HASHMAP_DIR_NAME_FOR_CELONIS_LIBRARIES="2c4687431f978f02a3780e24b8b701d22aa32d9c"
SKA_HASHMAP_ARCHIVE_NAME_FOR_CELONIS_LIBRARIES="${SKA_HASHMAP_DIR_NAME_FOR_CELONIS_LIBRARIES}.zip"
SKA_HASHMAP_DOWNLOAD_FOR_CELONIS_LIBRARIES="https://github.com/skarupke/flat_hash_map/archive/${SKA_HASHMAP_ARCHIVE_NAME_FOR_CELONIS_LIBRARIES}"

NAMED_TYPE_DIR_NAME_FOR_CELONIS_LIBRARIES="76668abe09807f92a695ee5e868f9719e888e65f"
NAMED_TYPE_ARCHIVE_NAME_FOR_CELONIS_LIBRARIES="${NAMED_TYPE_DIR_NAME_FOR_CELONIS_LIBRARIES}.zip"
NAMED_TYPE_DOWNLOAD_FOR_CELONIS_LIBRARIES="https://github.com/joboccara/NamedType/archive/${NAMED_TYPE_ARCHIVE_NAME_FOR_CELONIS_LIBRARIES}"

# Reference to the Saola release of the Celonis libraries (currently all packaged in the CPML)
CPML_RELEASE="release-2.223.0"
CPML_RESOURCE="CPML-2.223.0.tar.gz"
CPML_MD5SUM="469a02c2ad651e374e0ead5b5f373895"

# The temporary build directory where all thirdparty dependencies are placed at and where we build the Celonis libraries
# before copying them to their final install directory
# Preferably some directory where no root permissions are required (for easier local reproduction)
CELONIS_LIBRARIES_BUILD_ROOT=/tmp/tmp_celonis_libraries_build_root

# Install directory of the thirdparty dependencies
CELONIS_THIRDPARTY_DEPENDENCIES_INSTALL_DIR=${CELONIS_LIBRARIES_BUILD_ROOT}/thirdparty

# Directory where to extract the Celonis libraries archive and build them
CELONIS_LIBRARIES_BUILD_DIR=${CELONIS_LIBRARIES_BUILD_ROOT}/celonis_library_build

# Directory where to install the build artifacts of the Celonis libraries
CELONIS_LIBRARIES_INSTALL_DIR=${CELONIS_LIBRARIES_BUILD_ROOT}/CPML

CMAKE_BUILD_PARALLELISM=8

download_and_build_boost_for_celonis_libraries() {
  echo "Downloading and building boost..."

  cd $CELONIS_LIBRARIES_BUILD_ROOT
  mkdir boost && \
  cd boost && \
  wget -q ${BOOST_DOWNLOAD_FOR_CELONIS_LIBRARIES} && \
  tar -xzf ${BOOST_ARCHIVE_NAME_FOR_CELONIS_LIBRARIES} && \
  cd ${BOOST_DIR_NAME_FOR_CELONIS_LIBRARIES} && \
  ./bootstrap.sh --prefix=${CELONIS_THIRDPARTY_DEPENDENCIES_INSTALL_DIR} && \
  ./b2 && \
  ./b2 install
  cd $CELONIS_LIBRARIES_BUILD_ROOT
}

download_and_build_nlohmann_json_for_celonis_libraries() {
  echo "Downloading and building nlohmann_json..."

  cd $CELONIS_LIBRARIES_BUILD_ROOT
  mkdir nlohmann_json
  cd nlohmann_json
  wget -q ${NLOHMANN_JSON_DOWNLOAD_FOR_CELONIS_LIBRARIES}
  tar -xzf ${NLOHMANN_JSON_ARCHIVE_NAME_FOR_CELONIS_LIBRARIES}
  cd json-3.10.5
  mkdir build
  cd build
  cmake -DCMAKE_VERBOSE_MAKEFILE=OFF .. -DJSON_BuildTests=False
  cmake --build . --parallel ${CMAKE_BUILD_PARALLELISM}
  cmake --install . --prefix ${CELONIS_THIRDPARTY_DEPENDENCIES_INSTALL_DIR}
#  export NamedType_DIR=${CELONIS_THIRDPARTY_DEPENDENCIES_INSTALL_DIR}/lib/cmake/
  cd $CELONIS_LIBRARIES_BUILD_ROOT
}

download_and_build_fmt_for_celonis_libraries() {
  echo "Downloading and building fmt..."

  cd $CELONIS_LIBRARIES_BUILD_ROOT
  mkdir fmt
  cd fmt
  wget -q ${FMT_DOWNLOAD_FOR_CELONIS_LIBRARIES}
  unzip ${FMT_ARCHIVE_NAME_FOR_CELONIS_LIBRARIES}
  cd $FMT_DIR_NAME_FOR_CELONIS_LIBRARIES
  mkdir build
  cd build
  cmake -DCMAKE_VERBOSE_MAKEFILE=OFF .. -DFMT_TEST=False
  cmake --build . --parallel ${CMAKE_BUILD_PARALLELISM}
  cmake --install . --prefix ${CELONIS_THIRDPARTY_DEPENDENCIES_INSTALL_DIR}
  export fmt_DIR=${CELONIS_THIRDPARTY_DEPENDENCIES_INSTALL_DIR}/lib/cmake/fmt
  cd $CELONIS_LIBRARIES_BUILD_ROOT
}

# TODO(n.weber): Since TBB is a shared lib, we would actually need to copy the shared lib together with the CPML later
# For now we don't do this since we rely on TBB to be present and linked to SR for the Saola code already.
download_and_build_tbb_for_celonis_libraries() {
  echo "Downloading and building tbb..."
  cd $CELONIS_LIBRARIES_BUILD_ROOT
  mkdir tbb && \
  cd tbb && \
  wget -q ${TBB_DOWNLOAD_FOR_CELONIS_LIBRARIES} && \
  tar -xzf ${TBB_ARCHIVE_NAME_FOR_CELONIS_LIBRARIES}
  export TBB_DIR=${CELONIS_LIBRARIES_BUILD_ROOT}/tbb/oneapi-tbb-2021.11.0/lib/cmake/tbb/
  cd $CELONIS_LIBRARIES_BUILD_ROOT
}

download_and_build_ska_hashmap_for_celonis_libraries() {
  echo "Downloading and building flat_hash_map..."
  cd $CELONIS_LIBRARIES_BUILD_ROOT
  mkdir flat_hash_map
  cd flat_hash_map
  wget -q ${SKA_HASHMAP_DOWNLOAD_FOR_CELONIS_LIBRARIES}
  unzip ${SKA_HASHMAP_ARCHIVE_NAME_FOR_CELONIS_LIBRARIES}

  SKA_HASHMAP_INSTALL_DIR=$CELONIS_THIRDPARTY_DEPENDENCIES_INSTALL_DIR/lib/cmake

  mkdir -p $SKA_HASHMAP_INSTALL_DIR && cd $SKA_HASHMAP_INSTALL_DIR
cat <<EOF > ska_flat_hash_map-config.cmake
  if(NOT TARGET ska_flat_hash_map)
  add_library(ska_flat_hash_map INTERFACE IMPORTED)
  set_target_properties(ska_flat_hash_map PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${CELONIS_LIBRARIES_BUILD_ROOT}/flat_hash_map/flat_hash_map-${SKA_HASHMAP_DIR_NAME_FOR_CELONIS_LIBRARIES}")
  endif()
EOF

  export ska_flat_hash_map_DIR=${SKA_HASHMAP_INSTALL_DIR}
  cd $CELONIS_LIBRARIES_BUILD_ROOT
}

download_and_build_named_type_for_celonis_libraries() {
  echo "Downloading and building NameType..."
  cd $CELONIS_LIBRARIES_BUILD_ROOT
  mkdir NamedType && \
  cd NamedType && \
  wget -q ${NAMED_TYPE_DOWNLOAD_FOR_CELONIS_LIBRARIES} && \
  unzip ${NAMED_TYPE_ARCHIVE_NAME_FOR_CELONIS_LIBRARIES} && \
  cd NamedType-${NAMED_TYPE_DIR_NAME_FOR_CELONIS_LIBRARIES} && \
  mkdir build
  cd build
  cmake -DCMAKE_VERBOSE_MAKEFILE=OFF .. -DENABLE_TEST=false
  cmake --build . --parallel ${CMAKE_BUILD_PARALLELISM}
  cmake --install . --prefix ${CELONIS_THIRDPARTY_DEPENDENCIES_INSTALL_DIR}
  export NamedType_DIR=${CELONIS_THIRDPARTY_DEPENDENCIES_INSTALL_DIR}/lib/cmake/
  cd $CELONIS_LIBRARIES_BUILD_ROOT
}

download_and_build_dependencies_for_celonis_libraries() {
  echo "Start downloading and building library dependencies for the custom Celonis libraries at '${CELONIS_LIBRARIES_BUILD_ROOT}'"
  mkdir -p $CELONIS_THIRDPARTY_DEPENDENCIES_INSTALL_DIR
  download_and_build_boost_for_celonis_libraries
  download_and_build_nlohmann_json_for_celonis_libraries
  download_and_build_fmt_for_celonis_libraries
  download_and_build_tbb_for_celonis_libraries
  download_and_build_ska_hashmap_for_celonis_libraries
  download_and_build_named_type_for_celonis_libraries
}

# Variable used below to refer to the current Celonis library we want to build
CURRENT_CELONIS_LIBRARY_TO_BUILD=""

build_celonis_library() (
  BUILD_DIR_NAME="build"
  CURRENT_CELONIS_LIBRARY_FULL_BUILD_PATH=${CELONIS_LIBRARIES_BUILD_DIR}/${CURRENT_CELONIS_LIBRARY_TO_BUILD}/${BUILD_DIR_NAME}
  # The build output is written to the temporary root build directory because we don't want it in the final build dir
  BUILD_OUTPUT_FILE="${CURRENT_CELONIS_LIBRARY_FULL_BUILD_PATH}/${CURRENT_CELONIS_LIBRARY_TO_BUILD}.log"

  echo "Build ${CURRENT_CELONIS_LIBRARY_TO_BUILD} at '${CURRENT_CELONIS_LIBRARY_FULL_BUILD_PATH}' (build log: '${BUILD_OUTPUT_FILE}')"

  mkdir -p $CURRENT_CELONIS_LIBRARY_FULL_BUILD_PATH
  cd $CURRENT_CELONIS_LIBRARY_FULL_BUILD_PATH

  cmake -DCMAKE_VERBOSE_MAKEFILE=OFF \
    -DBOOST_ROOT=${CELONIS_THIRDPARTY_DEPENDENCIES_INSTALL_DIR} \
    -DCMAKE_PREFIX_PATH="${CELONIS_THIRDPARTY_DEPENDENCIES_INSTALL_DIR}/lib/cmake/nlohmann_json" \
    -DCMAKE_INSTALL_PREFIX=${CELONIS_LIBRARIES_INSTALL_DIR} \
    ..  2>&1 | tee -a ${BUILD_OUTPUT_FILE}
  cmake --build . --parallel ${CMAKE_BUILD_PARALLELISM} 2>&1 | tee -a ${BUILD_OUTPUT_FILE}
  cmake --install . --prefix ${CELONIS_LIBRARIES_INSTALL_DIR} 2>&1 | tee -a ${BUILD_OUTPUT_FILE}
)

# Downloads, builds and installs Celonis libraries like the CPML, CTL, ... Does not copy them to the final SR deps
download_and_build_celonis_libraries() {
  set -e
  # set -x # Uncomment for debugging the script

  # First we build all the thirdparty dependencies the Celonis libraries depend on
  download_and_build_dependencies_for_celonis_libraries

  # Prepare build and install directories
  mkdir -p $CELONIS_LIBRARIES_BUILD_DIR
  mkdir -p $CELONIS_LIBRARIES_INSTALL_DIR

  cd ${CELONIS_LIBRARIES_BUILD_DIR}

  echo "Start to download CPML archive into current working directory '$(pwd)'"

  gh release download -R celonis/cpm-query-engine $CPML_RELEASE --pattern "${CPML_RESOURCE}"

  echo "Downloading done"

  echo "Start to extract CPML archive"
  tar -xzf $CPML_RESOURCE
  echo "Extracting done"

  echo "Start building Celonis libraries"

  CURRENT_CELONIS_LIBRARY_TO_BUILD="celonis-formatting-library"
  build_celonis_library

  CURRENT_CELONIS_LIBRARY_TO_BUILD="celonis-template-library"
  build_celonis_library

  CURRENT_CELONIS_LIBRARY_TO_BUILD="celonis-process-mining-library"
  build_celonis_library

  echo "Building done"
}

download_and_build_celonis_libraries