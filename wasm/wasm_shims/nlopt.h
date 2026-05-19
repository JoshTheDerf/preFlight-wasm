#ifndef NLOPT_H
#define NLOPT_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NLOPT_GN_DIRECT = 0,
    NLOPT_GN_ESCH   = 1,
    NLOPT_GN_MLSL   = 2,
    NLOPT_GN_MLSL_LDS = 3,
    NLOPT_LN_NELDERMEAD = 4,
    NLOPT_LN_SBPLX = 5
} nlopt_algorithm;

typedef enum {
    NLOPT_FAILURE = -1,
    NLOPT_INVALID_ARGS = -2,
    NLOPT_OUT_OF_MEMORY = -3,
    NLOPT_ROUNDOFF_LIMITED = -4,
    NLOPT_FORCED_STOP = -5,
    NLOPT_SUCCESS = 1,
    NLOPT_STOPVAL_REACHED = 2,
    NLOPT_FTOL_REACHED = 3,
    NLOPT_XTOL_REACHED = 4,
    NLOPT_MAXEVAL_REACHED = 5,
    NLOPT_MAXTIME_REACHED = 6
} nlopt_result;

typedef double (*nlopt_func)(unsigned n, const double *x, double *grad, void *data);

typedef struct nlopt_opt_s {
    nlopt_algorithm algorithm;
    unsigned n;
    nlopt_func objective;
    void *objective_data;
    int maximize;
    int force_stop;
    double *grad_buffer;
    double *lower_bounds;
    double *upper_bounds;
    int maxeval;
    double ftol_abs;
    double ftol_rel;
    double stopval;
    struct nlopt_opt_s *local_opt;
} *nlopt_opt;

static inline nlopt_opt nlopt_create(nlopt_algorithm algorithm, unsigned n)
{
    struct nlopt_opt_s *opt = (struct nlopt_opt_s *)malloc(sizeof(struct nlopt_opt_s));
    if (!opt)
        return NULL;
    opt->algorithm = algorithm;
    opt->n = n;
    opt->objective = NULL;
    opt->objective_data = NULL;
    opt->maximize = 0;
    opt->force_stop = 0;
    opt->grad_buffer = NULL;
    opt->lower_bounds = NULL;
    opt->upper_bounds = NULL;
    opt->maxeval = 0;
    opt->ftol_abs = 0.0;
    opt->ftol_rel = 0.0;
    opt->stopval = 0.0;
    opt->local_opt = NULL;

    if (n > 0) {
        opt->grad_buffer = (double *)calloc(n, sizeof(double));
        opt->lower_bounds = (double *)calloc(n, sizeof(double));
        opt->upper_bounds = (double *)calloc(n, sizeof(double));
        if (!opt->grad_buffer || !opt->lower_bounds || !opt->upper_bounds) {
            free(opt->grad_buffer);
            free(opt->lower_bounds);
            free(opt->upper_bounds);
            free(opt);
            return NULL;
        }
    }

    return opt;
}

static inline void nlopt_destroy(nlopt_opt opt)
{
    if (!opt)
        return;
    free(opt->grad_buffer);
    free(opt->lower_bounds);
    free(opt->upper_bounds);
    free(opt);
}

static inline nlopt_result nlopt_set_lower_bounds(nlopt_opt opt, const double *lb)
{
    if (!opt || !lb)
        return NLOPT_INVALID_ARGS;
    memcpy(opt->lower_bounds, lb, sizeof(double) * opt->n);
    return NLOPT_SUCCESS;
}

static inline nlopt_result nlopt_set_upper_bounds(nlopt_opt opt, const double *ub)
{
    if (!opt || !ub)
        return NLOPT_INVALID_ARGS;
    memcpy(opt->upper_bounds, ub, sizeof(double) * opt->n);
    return NLOPT_SUCCESS;
}

static inline nlopt_result nlopt_set_ftol_abs(nlopt_opt opt, double tol)
{
    if (!opt)
        return NLOPT_INVALID_ARGS;
    opt->ftol_abs = tol;
    return NLOPT_SUCCESS;
}

static inline nlopt_result nlopt_set_ftol_rel(nlopt_opt opt, double tol)
{
    if (!opt)
        return NLOPT_INVALID_ARGS;
    opt->ftol_rel = tol;
    return NLOPT_SUCCESS;
}

static inline nlopt_result nlopt_set_stopval(nlopt_opt opt, double val)
{
    if (!opt)
        return NLOPT_INVALID_ARGS;
    opt->stopval = val;
    return NLOPT_SUCCESS;
}

static inline nlopt_result nlopt_set_maxeval(nlopt_opt opt, int maxeval)
{
    if (!opt)
        return NLOPT_INVALID_ARGS;
    opt->maxeval = maxeval;
    return NLOPT_SUCCESS;
}

static inline nlopt_result nlopt_set_min_objective(nlopt_opt opt, nlopt_func f, void *data)
{
    if (!opt)
        return NLOPT_INVALID_ARGS;
    opt->objective = f;
    opt->objective_data = data;
    opt->maximize = 0;
    return NLOPT_SUCCESS;
}

static inline nlopt_result nlopt_set_max_objective(nlopt_opt opt, nlopt_func f, void *data)
{
    if (!opt)
        return NLOPT_INVALID_ARGS;
    opt->objective = f;
    opt->objective_data = data;
    opt->maximize = 1;
    return NLOPT_SUCCESS;
}

static inline nlopt_result nlopt_set_local_optimizer(nlopt_opt opt, nlopt_opt local)
{
    if (!opt)
        return NLOPT_INVALID_ARGS;
    opt->local_opt = local;
    return NLOPT_SUCCESS;
}

static inline void nlopt_force_stop(nlopt_opt opt)
{
    if (!opt)
        return;
    opt->force_stop = 1;
}

static inline nlopt_result nlopt_optimize(nlopt_opt opt, double *x, double *minf)
{
    if (!opt || !x)
        return NLOPT_INVALID_ARGS;
    if (opt->force_stop) {
        opt->force_stop = 0;
        return NLOPT_FORCED_STOP;
    }

    if (opt->objective) {
        memset(opt->grad_buffer, 0, sizeof(double) * opt->n);
        double val = opt->objective(opt->n, x, opt->grad_buffer, opt->objective_data);
        if (minf)
            *minf = val;
    } else if (minf) {
        *minf = 0.0;
    }

    if (opt->force_stop) {
        opt->force_stop = 0;
        return NLOPT_FORCED_STOP;
    }

    return NLOPT_SUCCESS;
}

static inline void nlopt_srand(unsigned long seed)
{
    (void)seed;
}

static inline void nlopt_srand_time(void)
{
}

#ifdef __cplusplus
}
#endif

#endif /* NLOPT_H */
