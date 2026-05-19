#pragma once

#if defined(__has_include_next)
#  if __has_include_next(<CGAL/Exact_predicates_inexact_constructions_kernel.h>)
#    include_next <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#    define ORCA_WASM_USE_SYSTEM_CGAL_KERNEL
#  endif
#endif

#ifndef ORCA_WASM_USE_SYSTEM_CGAL_KERNEL
// Fallback stub (legacy path) – extremely limited kernel definition
namespace CGAL {
	struct Exact_predicates_inexact_constructions_kernel {
		using FT = double;
		using RT = double;

		struct Point_3 {
			double x{}, y{}, z{};
			Point_3() = default;
			Point_3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
		};

		struct Vector_3 {
			double x{}, y{}, z{};
			Vector_3() = default;
			Vector_3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
		};

		struct Triangle_3 {
			Point_3 p1, p2, p3;
			Triangle_3(const Point_3& a, const Point_3& b, const Point_3& c)
				: p1(a), p2(b), p3(c) {}
		};
	};

	using Kernel = Exact_predicates_inexact_constructions_kernel;
}

#define CGAL_EXACT_PREDICATES_INEXACT_CONSTRUCTIONS_KERNEL_H
#endif