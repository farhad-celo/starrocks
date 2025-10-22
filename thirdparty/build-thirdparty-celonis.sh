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

    # CELOSTAR-1006 Unset the LDFLAGS explicitly as they are set by build-thirdparty.sh and would
    # cause -static-libstdc++ and -static-libgcc flags to be used for TBB, which causes an abort
    # if an exception is thrown inside a TBB context (e.g. a parallel_for) which normally should
    # be propagated & re-thrown in the calling code.
    LDFLAGS="" \
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

# The thirdparty dependencies of the Saola code integrated to SR
build_thirdparty_celonis() {
    build_namedtype
    build_nlohmann_json
    build_onetbb
    build_spdlog
    build_blake2
}
