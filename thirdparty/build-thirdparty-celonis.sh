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

build_thirdparty_celonis() {
    build_namedtype
    build_nlohmann_json
    build_onetbb
    build_spdlog
}
