# OpenVDB is not built for the WASM target. We deliberately do NOT define
# OpenVDB::openvdb so that the `if (TARGET OpenVDB::openvdb)` guards in
# libslic3r/CMakeLists.txt evaluate to false and OpenVDBUtils.cpp is excluded
# from the source list.

set(OPENVDB_FOUND FALSE)
set(OpenVDB_FOUND FALSE)
