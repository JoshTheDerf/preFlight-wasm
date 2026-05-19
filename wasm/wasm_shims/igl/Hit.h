#pragma once

namespace igl {
// Minimal hit record matching libigl expectations for ray queries.
struct Hit {
    int id;      // primitive index; -1 when no hit
    int fid;     // face index (libigl stores this separately)
    double u;    // barycentric coordinate
    double v;    // barycentric coordinate
    double t;    // ray parameter (distance along ray)

    Hit() : id(-1), fid(-1), u(0.0), v(0.0), t(0.0) {}
    Hit(int id_, int fid_, double u_, double v_, double t_)
        : id(id_), fid(fid_), u(u_), v(v_), t(t_) {}
};
} // namespace igl
