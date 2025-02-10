# namedtype 
build_namedtype() {
    check_if_source_exist $NAMEDTYPE_SOURCE

    cp -r $TP_SOURCE_DIR/$NAMEDTYPE_SOURCE/include/NamedType $TP_INCLUDE_DIR/
}

# nlohmann_json
build_nlohmann_json() {
    check_if_source_exist $NLOHMANN_JSON_SOURCE
    cd $TP_SOURCE_DIR/$NLOHMANN_JSON_SOURCE

    $CMAKE_CMD -G "${CMAKE_GENERATOR}" -DCMAKE_BUILD_TYPE=Release \
	    -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_PREFIX=$TP_INSTALL_DIR -DCMAKE_INSTALL_LIBDIR=lib
    ${BUILD_SYSTEM} -j$PARALLEL install
}

# onetbb
# onetbb supports only shared libraries officially. Either LD_LIBRARY_PATH should include
# $TP_INSTALL_DIR/lib or libtbb.so should be copied into a running environment.
build_onetbb() {
    check_if_source_exist $ONETBB_SOURCE
    cd $TP_SOURCE_DIR/$ONETBB_SOURCE

    $CMAKE_CMD -G "${CMAKE_GENERATOR}" -DCMAKE_BUILD_TYPE=Release \
	    -DBUILD_SHARED_LIBS=ON -DCMAKE_INSTALL_PREFIX=$TP_INSTALL_DIR -DCMAKE_INSTALL_LIBDIR=lib \
	    -DTBB_ENABLE_IPO=OFF -DTBB_TEST=OFF -DTBBMALLOC_BUILD=OFF
    ${BUILD_SYSTEM} -j$PARALLEL install
}

# spdlog
build_spdlog() {
    check_if_source_exist $SPDLOG_SOURCE

    cp -r $TP_SOURCE_DIR/$SPDLOG_SOURCE/include/spdlog $TP_INCLUDE_DIR/
}

# BLAKE2
build_blake2() {
    check_if_source_exist $BLAKE2_SOURCE
    cd $TP_SOURCE_DIR/$BLAKE2_SOURCE/ref
    gcc -O2 -I../testvectors -Wall -Wextra -std=c89 -pedantic -Wno-long-long -c blake2s-ref.c -o blake2s.o
    ar rcs libblake2s.a blake2s.o
    cp -r $TP_SOURCE_DIR/$BLAKE2_SOURCE/ref/blake2.h $TP_INCLUDE_DIR/
    cp -r $TP_SOURCE_DIR/$BLAKE2_SOURCE/ref/libblake2s.a $TP_LIB_DIR/
}

# CPML
build_cpml() {
  set -e
  set -x
  echo "start building CPML...."
  CPML_BUILD_ROOT=$(pwd)
  echo "current dir $(pwd)"

  echo "before boost..."
  cd /
  mkdir boost && \
  cd boost && \
  wget -q https://archives.boost.io/release/1.84.0/source/boost_1_84_0.tar.gz && \
  tar -xzf boost_1_84_0.tar.gz && \
  cd boost_1_84_0 && \
  ./bootstrap.sh --prefix=/usr/ && \
  ./b2 && \
  ./b2 install

  echo "before tbb..."
  cd /
  mkdir tbb && \
  cd tbb && \
  wget -q https://github.com/oneapi-src/oneTBB/releases/download/v2021.11.0/oneapi-tbb-2021.11.0-lin.tgz && \
  tar -xzf oneapi-tbb-2021.11.0-lin.tgz
  export TBB_DIR=/tbb/oneapi-tbb-2021.11.0/lib/cmake/tbb/

  echo "before flat_hash_map..."
  cd /
  mkdir flat_hash_map
  cd flat_hash_map
  wget -q https://github.com/skarupke/flat_hash_map/archive/2c4687431f978f02a3780e24b8b701d22aa32d9c.zip
  unzip 2c4687431f978f02a3780e24b8b701d22aa32d9c.zip
  mkdir cmake && cd cmake
cat <<EOF > ska_flat_hash_map-config.cmake
  if(NOT TARGET ska_flat_hash_map)
  add_library(ska_flat_hash_map INTERFACE IMPORTED)
  set_target_properties(ska_flat_hash_map PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "/flat_hash_map/flat_hash_map-2c4687431f978f02a3780e24b8b701d22aa32d9c")
  endif()
EOF

  export ska_flat_hash_map_DIR=/flat_hash_map/cmake/

  echo "before nameType..."
  cd /
  mkdir NamedType && \
  cd NamedType && \
  wget -q https://github.com/joboccara/NamedType/archive/76668abe09807f92a695ee5e868f9719e888e65f.zip && \
  unzip 76668abe09807f92a695ee5e868f9719e888e65f.zip && \
  cd NamedType-76668abe09807f92a695ee5e868f9719e888e65f && \
  mkdir build
  cd build
  cmake -DCMAKE_VERBOSE_MAKEFILE=OFF .. -DENABLE_TEST=false
  cmake --build .
  cmake --install .
  export NamedType_DIR=/usr/local/lib/cmake/

  mkdir -p /build/CPML
  CPML_BUILD_ROOT=/build/CPML
  cd ${CPML_BUILD_ROOT}
  CPML_RELEASE="release-2.204.1"
  CPML_RESOURCE="CPML-2.204.1.tar.gz"
  CPML_MD5SUM="ed0a545cdca0a63ddde83f324f2efbe9"

  echo "Start to download CPML"

  echo "Current working directory: $(pwd)"
  gh release download -R celonis/cpm-query-engine $CPML_RELEASE --pattern "${CPML_RESOURCE}"

  echo "Start to unpack CPML artifact"
  tar -xzf $CPML_RESOURCE
  echo "done unpacking"


  echo "build formatting"
  mkdir -p $CPML_BUILD_ROOT/celonis-formatting-library/build
  cd $CPML_BUILD_ROOT/celonis-formatting-library/build
  echo "build start..." > format_output.log
  (cmake -DCMAKE_VERBOSE_MAKEFILE=OFF .. && cmake --build . && cmake --install .) 2>&1 | tee -a format_output.log
  (cmake --build . && cmake --build . && cmake --install .) 2>&1 | tee -a format_output.log
  (cmake --install . && cmake --build . && cmake --install .) 2>&1 | tee -a format_output.log


  echo "build template library"
  mkdir -p $CPML_BUILD_ROOT/celonis-template-library/build && \
  cd $CPML_BUILD_ROOT/celonis-template-library/build && \
  echo "build start..." > format_output.log
  (cmake -DCMAKE_VERBOSE_MAKEFILE=OFF .. && cmake --build . && cmake --install .) 2>&1 | tee -a format_output.log
  (cmake --build . && cmake --build . && cmake --install .) 2>&1 | tee -a format_output.log
  (cmake --install . && cmake --build . && cmake --install .) 2>&1 | tee -a format_output.log

  echo "build concurrency"
  mkdir -p $CPML_BUILD_ROOT/celonis-concurrency-library/build && \
  cd $CPML_BUILD_ROOT/celonis-concurrency-library/build && \
  echo "build start..." > format_output.log
  (cmake -DCMAKE_VERBOSE_MAKEFILE=OFF .. && cmake --build . && cmake --install .) 2>&1 | tee -a format_output.log
  (cmake --build . && cmake --build . && cmake --install .) 2>&1 | tee -a format_output.log
  (cmake --install . && cmake --build . && cmake --install .) 2>&1 | tee -a format_output.log

  echo "build process mining"
  mkdir -p $CPML_BUILD_ROOT/celonis-process-mining-library/build && \
  cd $CPML_BUILD_ROOT/celonis-process-mining-library/build && \
  echo "build start..." > format_output.log
  (cmake -DCMAKE_VERBOSE_MAKEFILE=OFF .. && cmake --build . && cmake --install .) 2>&1 | tee -a format_output.log
  (cmake --build . && cmake --build . && cmake --install .) 2>&1 | tee -a format_output.log
  (cmake --install . && cmake --build . && cmake --install .) 2>&1 | tee -a format_output.log

  echo "done copying..." >  final_out.log

  mkdir -p $STARROCKS_THIRDPARTY/installed/CPML
  cp -r $CPML_BUILD_ROOT $STARROCKS_THIRDPARTY/installed
}

build_thirdparty_celonis() {
    build_namedtype
    build_nlohmann_json
    build_onetbb
    build_spdlog
    build_blake2
    build_cpml
}
