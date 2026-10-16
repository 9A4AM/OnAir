# Writes a copy of the C++ AIS frame layer in the namespace dect2::ais_ref (the reference for tests/ais/test_ais_proto_rs.cpp, which compares it with
# the Rust implementation that dect2::ais then is). Usage: cmake -DSRC=<repo> -DOUT=<dir> -P gen_ais_ref.cmake
file(READ "${SRC}/core/include/dect2/ais/ais_proto.h" _h)
string(REPLACE "namespace ais {" "namespace ais_ref {" _h "${_h}")
file(WRITE "${OUT}/ais_proto_ref.h" "${_h}")
file(READ "${SRC}/core/src/ais/ais_proto.cpp" _c)
string(REPLACE "namespace ais {" "namespace ais_ref {" _c "${_c}")
string(REPLACE "#include \"dect2/ais/ais_proto.h\"" "#undef DECT2_RUST_PROTO\n#include \"ais_proto_ref.h\"" _c "${_c}")
file(WRITE "${OUT}/ais_proto_ref.cpp" "${_c}")
