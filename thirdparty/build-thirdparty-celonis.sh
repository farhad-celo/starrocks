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
# TODO: consider to download CPML from release artifacts of cpm-query-engine repo
build_cpml() {
  echo "Start to download CPML"
  CPML_RELEASE="CPML-2.177-ubuntu22"
  CPML_RESOURCE="CPML-2.177-ubuntu22.tar.gz"
  CPML_FOLDER="CPML"
  CPML_MD5SUM="4b57fba407c6a7c2b8a1a91d91ee1294"

  gh release download -R celonis/celostar-starrocks $CPML_RELEASE --pattern "${CPML_RESOURCE}"
  md5=`md5sum ${CPML_RESOURCE}`
  if [ "$md5" != "$CPML_MD5SUM  $CPML_RESOURCE" ]; then
    echo "$CPML_RESOURCE md5sum check failed!"
    echo -e "expect-md5 $CPML_MD5SUM \nactual-md5 $md5"
    exit 1
  fi

  echo "Start to unpack CPML artifact"
  tar -xzf ${CPML_RESOURCE}

  echo "Start to copy CMPL artifact"
  cp -r $CPML_FOLDER ${STARROCKS_THIRDPARTY}/installed/
  echo "CPML is installed"
}

build_thirdparty_celonis() {
    build_namedtype
    build_nlohmann_json
    build_onetbb
    build_spdlog
    build_blake2
    build_cpml
}
