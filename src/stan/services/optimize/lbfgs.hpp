#ifndef STAN_SERVICES_OPTIMIZE_LBFGS_HPP
#define STAN_SERVICES_OPTIMIZE_LBFGS_HPP

#include <stan/callbacks/interrupt.hpp>
#include <stan/callbacks/logger.hpp>
#include <stan/callbacks/writer.hpp>
#include <stan/io/var_context.hpp>
#include <stan/optimization/bfgs.hpp>
#include <stan/services/error_codes.hpp>
#include <stan/services/util/initialize.hpp>
#include <stan/services/util/create_rng.hpp>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>

namespace stan {
namespace services {
namespace optimize {

/**
 * Runs the L-BFGS algorithm for a model.
 *
 * @tparam Model A model implementation
 * @param[in] model Input model to test (with data already instantiated)
 * @param[in] init var context for initialization
 * @param[in] random_seed random seed for the random number generator
 * @param[in] chain chain id to advance the pseudo random number generator
 * @param[in] init_radius radius to initialize
 * @param[in] history_size amount of history to keep for L-BFGS
 * @param[in] init_alpha line search step size for first iteration
 * @param[in] tol_obj convergence tolerance on absolute changes in
 *   objective function value
 * @param[in] tol_rel_obj convergence tolerance on relative changes
 *   in objective function value
 * @param[in] tol_grad convergence tolerance on the norm of the gradient
 * @param[in] tol_rel_grad convergence tolerance on the relative norm of
 *   the gradient
 * @param[in] tol_param convergence tolerance on changes in parameter
 *   value
 * @param[in] num_iterations maximum number of iterations
 * @param[in] save_iterations indicates whether all the iterations should
 *   be saved to the parameter_writer
 * @param[in] refresh how often to write output to logger
 * @param[in,out] interrupt callback to be called every iteration
 * @param[in,out] logger Logger for messages
 * @param[in,out] init_writer Writer callback for unconstrained inits
 * @param[in,out] parameter_writer output for parameter values
 * @return error_codes::OK if successful
 */
template <class Model>
int lbfgs(Model& model, const stan::io::var_context& init,
          unsigned int random_seed, unsigned int chain, double init_radius,
          int history_size, double init_alpha, double tol_obj,
          double tol_rel_obj, double tol_grad, double tol_rel_grad,
          double tol_param, int num_iterations, bool save_iterations,
	  int laplace_draws, double laplace_add_diag,
          int refresh, callbacks::interrupt& interrupt,
          callbacks::logger& logger, callbacks::writer& init_writer,
          callbacks::writer& parameter_writer) {
  boost::ecuyer1988 rng = util::create_rng(random_seed, chain);

  std::vector<int> disc_vector;
  std::vector<double> cont_vector = util::initialize<false>(
      model, init, rng, init_radius, false, logger, init_writer);

  std::stringstream lbfgs_ss;
  typedef stan::optimization::BFGSLineSearch<Model,
                                             stan::optimization::LBFGSUpdate<> >
      Optimizer;
  Optimizer lbfgs(model, cont_vector, disc_vector, &lbfgs_ss);
  lbfgs.get_qnupdate().set_history_size(history_size);
  lbfgs._ls_opts.alpha0 = init_alpha;
  lbfgs._conv_opts.tolAbsF = tol_obj;
  lbfgs._conv_opts.tolRelF = tol_rel_obj;
  lbfgs._conv_opts.tolAbsGrad = tol_grad;
  lbfgs._conv_opts.tolRelGrad = tol_rel_grad;
  lbfgs._conv_opts.tolAbsX = tol_param;
  lbfgs._conv_opts.maxIts = num_iterations;

  double lp = lbfgs.logp();

  std::stringstream initial_msg;
  initial_msg << "Initial log joint probability = " << lp;
  logger.info(initial_msg);

  std::vector<std::string> names;
  names.push_back("lp__");
  model.constrained_param_names(names, true, true);
  parameter_writer(names);

  if (save_iterations) {
    std::vector<double> values;
    std::stringstream msg;
    model.write_array(rng, cont_vector, disc_vector, values, true, true, &msg);
    if (msg.str().length() > 0)
      logger.info(msg);

    values.insert(values.begin(), lp);
    parameter_writer(values);
  }
  int ret = 0;

  while (ret == 0) {
    interrupt();
    if (refresh > 0
        && (lbfgs.iter_num() == 0 || ((lbfgs.iter_num() + 1) % refresh == 0)))
      logger.info(
          "    Iter"
          "      log prob"
          "        ||dx||"
          "      ||grad||"
          "       alpha"
          "      alpha0"
          "  # evals"
          "  Notes ");

    ret = lbfgs.step();
    lp = lbfgs.logp();
    lbfgs.params_r(cont_vector);

    if (refresh > 0
        && (ret != 0 || !lbfgs.note().empty() || lbfgs.iter_num() == 0
            || ((lbfgs.iter_num() + 1) % refresh == 0))) {
      std::stringstream msg;
      msg << " " << std::setw(7) << lbfgs.iter_num() << " ";
      msg << " " << std::setw(12) << std::setprecision(6) << lp << " ";
      msg << " " << std::setw(12) << std::setprecision(6)
          << lbfgs.prev_step_size() << " ";
      msg << " " << std::setw(12) << std::setprecision(6)
          << lbfgs.curr_g().norm() << " ";
      msg << " " << std::setw(10) << std::setprecision(4) << lbfgs.alpha()
          << " ";
      msg << " " << std::setw(10) << std::setprecision(4) << lbfgs.alpha0()
          << " ";
      msg << " " << std::setw(7) << lbfgs.grad_evals() << " ";
      msg << " " << lbfgs.note() << " ";
      logger.info(msg);
    }

    if (lbfgs_ss.str().length() > 0) {
      logger.info(lbfgs_ss);
      lbfgs_ss.str("");
    }

    if (save_iterations) {
      std::vector<double> values;
      std::stringstream msg;
      model.write_array(rng, cont_vector, disc_vector, values, true, true,
                        &msg);
      if (msg.str().length() > 0)
        logger.info(msg);

      values.insert(values.begin(), lp);
      parameter_writer(values);
    }
  }

  if (!save_iterations) {
    std::vector<double> values;
    std::stringstream msg;
    model.write_array(rng, cont_vector, disc_vector, values, true, true, &msg);
    if (msg.str().length() > 0)
      logger.info(msg);

    values.insert(values.begin(), lp);
    parameter_writer(values);
  }

  if(laplace_draws > 0) {
    Eigen::Map<Eigen::VectorXd> cont_eigen_vector(cont_vector.data(), cont_vector.size());
  
    auto f = [&](const Eigen::VectorXd& theta) {
      std::stringstream msg;
      Eigen::VectorXd theta_ = theta;
      double nlp = -model.log_prob(theta_, &msg);
      if (msg.str().length() > 0)
	logger.info(msg);
      return nlp;
    };

    double nlp;
    Eigen::VectorXd grad_nlp;
    Eigen::MatrixXd hess_nlp;
  
    stan::math::finite_diff_hessian_auto(f, cont_eigen_vector, nlp, grad_nlp, hess_nlp);

    Eigen::MatrixXd hess_nlp_U = (hess_nlp + Eigen::MatrixXd::Identity(hess_nlp.rows(), hess_nlp.cols()) * laplace_add_diag).eval().llt().matrixU();
  
    boost::variate_generator<decltype(rng)&, boost::normal_distribution<> >
      rand_dense_gaus(rng, boost::normal_distribution<>());

    parameter_writer("Draws from Laplace Approximation:");

    std::vector<std::string> laplace_names;
    laplace_names.push_back("lp__");
    laplace_names.push_back("log_p");
    laplace_names.push_back("log_g");
    model.constrained_param_names(laplace_names, true, true);
    parameter_writer(laplace_names);

    for(size_t n = 0; n < laplace_draws; ++n) {
      std::vector<double> laplace_cont_vector(cont_eigen_vector.size());
      Eigen::VectorXd x(cont_eigen_vector.size());
      std::vector<double> values;
      std::stringstream msg;

      for (size_t i = 0; i < x.size(); ++i)
	x(i) = rand_dense_gaus();

      Eigen::VectorXd laplace_cont_eigen_vector = hess_nlp_U.triangularView<Eigen::Upper>().solve(x).eval() + cont_eigen_vector;

      for (size_t i = 0; i < laplace_cont_vector.size(); ++i)
	laplace_cont_vector[i] = laplace_cont_eigen_vector(i);

      // log_g : Log probability of approximation in unconstrained space dropping constants
      double log_g = -0.5 * x.transpose() * x;

      // log_p : Log probability in the unconstrained space
      double log_p = model.template log_prob<false, true>(laplace_cont_eigen_vector, &msg);
      if (msg.str().length() > 0)
        logger.info(msg);

      msg.str("");
      model.write_array(rng, laplace_cont_vector, disc_vector, values, true, true, &msg);
      if (msg.str().length() > 0)
	logger.info(msg);

      values.insert(values.begin(), { 0, log_p, log_g });

      parameter_writer(values);
    }
  }

  int return_code;
  if (ret >= 0) {
    logger.info("Optimization terminated normally: ");
    return_code = error_codes::OK;
  } else {
    logger.info("Optimization terminated with error: ");
    return_code = error_codes::SOFTWARE;
  }
  logger.info("  " + lbfgs.get_code_string(ret));

  return return_code;
}

}  // namespace optimize
}  // namespace services
}  // namespace stan
#endif
