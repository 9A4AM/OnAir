# Writes a frozen copy of a C++ protocol layer in a renamed namespace: the reference for the differential tests of the Rust modules
# (tests/<mode>/test_<mode>_proto_rs.cpp compare it with the Rust-backed implementation that the real namespace then is).
# Usage: cmake -DSRC=<repo> -DOUT=<dir> -DNAME=<file base, e.g. same_proto> -DHDR=<header path below core/include> -DCPP=<source path below core/src>
#              -DDEF=<DECT2_RUST_x> -DFROM=<"namespace dect2::same {"> -DTO=<"namespace dect2::same_ref {"> [-DEXTRA=<text put after the namespace line>]
#              [-DINC_FROM=<text> -DINC_TO=<text>] (replaced in the header copy: lets a second layer's header include the first layer's reference header)
# The copy is made with DEF undefined, so it is always the C++ implementation.
file(READ "${SRC}/core/include/${HDR}" _h)
string(REPLACE "${FROM}" "${TO}${EXTRA}" _h "${_h}")
if(INC_FROM)
  string(REPLACE "${INC_FROM}" "${INC_TO}" _h "${_h}")
endif()
file(WRITE "${OUT}/${NAME}_ref.h" "${_h}")
file(READ "${SRC}/core/src/${CPP}" _c)
string(REPLACE "${FROM}" "${TO}${EXTRA}" _c "${_c}")
string(REPLACE "#include \"${HDR}\"" "#undef ${DEF}\n#include \"${NAME}_ref.h\"" _c "${_c}")
file(WRITE "${OUT}/${NAME}_ref.cpp" "${_c}")
