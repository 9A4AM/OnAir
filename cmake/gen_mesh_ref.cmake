# Writes frozen copies of the mesh packet layer (core/src/mesh/mesh_proto*.cpp, mesh_pb.cpp and their headers) in the namespace dect2_mesh_ref: the reference of
# tests/mesh/test_mesh_proto_rs.cpp (the Rust-backed implementation is the real dect2 namespace). Like gen_rs_ref.cmake, but for several files that include
# each other: every "namespace dect2 {" becomes "namespace dect2_mesh_ref { using namespace dect2;", the includes of the mesh headers point at the copies,
# and DECT2_RUST_MESH is undefined at the top of every source so the copy is always the C++ implementation.
# Usage: cmake -DSRC=<repo> -DOUT=<dir> -P gen_mesh_ref.cmake
set(_rep
  "dect2/mesh/mesh_proto.h|mesh_proto_ref.h"
  "mesh_proto_internal.h|mesh_proto_internal_ref.h"
  "dect2/mesh/mesh_pb.h|mesh_pb_ref.h")
function(_mesh_ref_fix text out)
  set(_t "${text}")
  string(REPLACE "namespace dect2 {" "namespace dect2 {} namespace dect2_mesh_ref { using namespace dect2;" _t "${_t}")
  foreach(_r ${_rep})
    string(REPLACE "|" ";" _p "${_r}")
    list(GET _p 0 _from)
    list(GET _p 1 _to)
    string(REPLACE "#include \"${_from}\"" "#include \"${_to}\"" _t "${_t}")
  endforeach()
  set(${out} "${_t}" PARENT_SCOPE)
endfunction()
foreach(_h "include/dect2/mesh/mesh_proto.h|mesh_proto_ref.h" "src/mesh/mesh_proto_internal.h|mesh_proto_internal_ref.h" "include/dect2/mesh/mesh_pb.h|mesh_pb_ref.h")
  string(REPLACE "|" ";" _p "${_h}")
  list(GET _p 0 _in)
  list(GET _p 1 _out)
  file(READ "${SRC}/core/${_in}" _c)
  _mesh_ref_fix("${_c}" _c)
  file(WRITE "${OUT}/${_out}" "${_c}")
endforeach()
foreach(_b mesh_proto mesh_proto_tables mesh_proto_meshtastic mesh_proto_meshcore mesh_proto_aprs mesh_proto_lorawan mesh_pb)
  file(READ "${SRC}/core/src/mesh/${_b}.cpp" _c)
  _mesh_ref_fix("${_c}" _c)
  file(WRITE "${OUT}/${_b}_ref.cpp" "#undef DECT2_RUST_MESH\n${_c}")
endforeach()
