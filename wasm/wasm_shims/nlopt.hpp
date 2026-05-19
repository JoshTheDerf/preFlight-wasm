#ifndef NLOPT_HPP
#define NLOPT_HPP

#include <vector>
#include <exception>

#include "nlopt.h"

namespace nlopt {

enum algorithm {
    GN_DIRECT = NLOPT_GN_DIRECT,
    GN_ESCH = NLOPT_GN_ESCH,
    GN_MLSL = NLOPT_GN_MLSL,
    GN_MLSL_LDS = NLOPT_GN_MLSL_LDS,
    LN_NELDERMEAD = NLOPT_LN_NELDERMEAD,
    LN_SBPLX = NLOPT_LN_SBPLX
};

enum result {
    FAILURE = NLOPT_FAILURE,
    INVALID_ARGS = NLOPT_INVALID_ARGS,
    OUT_OF_MEMORY = NLOPT_OUT_OF_MEMORY,
    ROUNDOFF_LIMITED = NLOPT_ROUNDOFF_LIMITED,
    FORCED_STOP = NLOPT_FORCED_STOP,
    SUCCESS = NLOPT_SUCCESS,
    STOPVAL_REACHED = NLOPT_STOPVAL_REACHED,
    FTOL_REACHED = NLOPT_FTOL_REACHED,
    XTOL_REACHED = NLOPT_XTOL_REACHED,
    MAXEVAL_REACHED = NLOPT_MAXEVAL_REACHED,
    MAXTIME_REACHED = NLOPT_MAXTIME_REACHED
};

class forced_stop : public std::exception {
public:
    const char *what() const noexcept override { return "nlopt forced stop"; }
};

class opt {
public:
    using vfunc = double (*)(const std::vector<double>&, std::vector<double>&, void*);

    opt()
        : alg_(GN_DIRECT), dim_(0), objective_(nullptr), objective_data_(nullptr),
          maximize_(false), force_stop_(false), ftol_abs_(0.0), ftol_rel_(0.0), stopval_(0.0), maxeval_(0) {}

    opt(algorithm alg, unsigned dim)
        : alg_(alg), dim_(dim), objective_(nullptr), objective_data_(nullptr),
          maximize_(false), force_stop_(false), ftol_abs_(0.0), ftol_rel_(0.0), stopval_(0.0), maxeval_(0) {}

    algorithm get_algorithm() const { return alg_; }

    void set_lower_bounds(const std::vector<double>& lb) { lower_bounds_ = lb; }
    void set_upper_bounds(const std::vector<double>& ub) { upper_bounds_ = ub; }

    void set_local_optimizer(const opt& /*local*/) {}

    void set_ftol_abs(double tol) { ftol_abs_ = tol; }
    void set_ftol_rel(double tol) { ftol_rel_ = tol; }
    void set_stopval(double val) { stopval_ = val; }
    void set_maxeval(int maxeval) { maxeval_ = maxeval; }

    void set_min_objective(vfunc fn, void *data)
    {
        objective_ = fn;
        objective_data_ = data;
        maximize_ = false;
    }

    void set_max_objective(vfunc fn, void *data)
    {
        objective_ = fn;
        objective_data_ = data;
        maximize_ = true;
    }

    void force_stop() { force_stop_ = true; }

    result optimize(std::vector<double>& x, double& value)
    {
        if (force_stop_) {
            force_stop_ = false;
            throw forced_stop();
        }

        if (!objective_) {
            value = 0.0;
            return SUCCESS;
        }

        std::vector<double> grad(x.size(), 0.0);
        double raw = objective_(x, grad, objective_data_);

        if (force_stop_) {
            force_stop_ = false;
            throw forced_stop();
        }

        value = raw;
        (void)maximize_;
        (void)ftol_abs_;
        (void)ftol_rel_;
        (void)stopval_;
        (void)maxeval_;
        return SUCCESS;
    }

private:
    algorithm alg_;
    unsigned dim_;
    vfunc objective_;
    void *objective_data_;
    bool maximize_;
    bool force_stop_;
    std::vector<double> lower_bounds_;
    std::vector<double> upper_bounds_;
    double ftol_abs_;
    double ftol_rel_;
    double stopval_;
    int maxeval_;
};

inline void srand(unsigned long /*seed*/) {}
inline void srand_time(void) {}

} // namespace nlopt

#endif // NLOPT_HPP
