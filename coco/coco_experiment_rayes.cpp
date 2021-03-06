/**
 * An example of benchmarking random search on a COCO suite. A grid search optimizer is also
 * implemented and can be used instead of random search.
 *
 * Set the global parameter BUDGET_MULTIPLIER to suit your needs.
 */
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <iostream>
#include <string>
#include <sstream>

#include <Eigen/Dense>

#include <es/rayes/RayEs.h>

#include <es/core/util.h>

#include "coco.h"

#define max(a,b) ((a) > (b) ? (a) : (b))

class BudgetExhaustedException : public std::exception {
};

/**
 * The maximal budget for evaluations done by an optimization algorithm equals dimension * BUDGET_MULTIPLIER.
 * Increase the budget multiplier value gradually to see how it affects the runtime.
 */
static const size_t BUDGET_MULTIPLIER = 100000;

/**
 * The maximal number of independent restarts allowed for an algorithm that restarts itself.
 */
static const size_t INDEPENDENT_RESTARTS = 0;

/**
 * The random seed. Change if needed.
 */
static const uint32_t RANDOM_SEED = 0xdeadbeef;

/**
 * A function type for evaluation functions, where the first argument is the vector to be evaluated and the
 * second argument the vector to which the evaluation result is stored.
 */
typedef void (*evaluate_function_t)(const double *x, double *y);

/**
 * A pointer to the problem to be optimized (needed in order to simplify the interface between the optimization
 * algorithm and the COCO platform).
 */
static coco_problem_t *PROBLEM;

/**
 * Calls coco_evaluate_function() to evaluate the objective function
 * of the problem at the point x and stores the result in the vector y
 */
static void evaluate_function(const double *x, double *y) {
  coco_evaluate_function(PROBLEM, x, y);
}

/**
 * Calls coco_evaluate_constraint() to evaluate the constraints
 * of the problem at the point x and stores the result in the vector y
 */
static void evaluate_constraint(const double *x, double *y) {
  coco_evaluate_constraint(PROBLEM, x, y);
}

/* Declarations of all functions implemented in this file (so that their order is not important): */
void example_experiment(const char *suite_name,
                        const char *observer_name,
                        coco_random_state_t *random_generator,
                        const std::string &name,
                        int firstFunction,
                        int lastFunction,
                        int dimension);

void my_random_search(evaluate_function_t evaluate_func,
                      evaluate_function_t evaluate_cons,
                      const size_t dimension,
                      const size_t number_of_objectives,
                      const size_t number_of_constraints,
                      const double *lower_bounds,
                      const double *upper_bounds,
                      const size_t max_budget,
                      coco_random_state_t *random_generator);

void my_grid_search(evaluate_function_t evaluate,
                    const size_t dimension,
                    const size_t number_of_objectives,
                    const double *lower_bounds,
                    const double *upper_bounds,
                    const size_t max_budget);

void my_search(evaluate_function_t evaluate,
               evaluate_function_t evaluate_cons,
               const size_t dimension,
               const size_t number_of_objectives,
               const size_t number_of_constraints,
               const double *lower_bounds,
               const double *upper_bounds,
               const size_t max_budget);

/* Structure and functions needed for timing the experiment */
typedef struct {
	size_t number_of_dimensions;
	size_t current_idx;
	char **output;
	size_t previous_dimension;
	size_t cumulative_evaluations;
	time_t start_time;
	time_t overall_start_time;
} timing_data_t;
static timing_data_t *timing_data_initialize(coco_suite_t *suite);
static void timing_data_time_problem(timing_data_t *timing_data, coco_problem_t *problem);
static void timing_data_finalize(timing_data_t *timing_data);

/**
 * The main method initializes the random number generator and calls the example experiment on the
 * bi-objective suite.
 */
int main(int /*argc*/, char *[]/*argv[]*/) {

  // if (argc != 5) {
  //     std::cout << "Usage: " << argv[0] << " alg-name first-func-id last-func-id dimension" << std::endl;
  //     return EXIT_FAILURE;
  // }

  // std::string name(argv[1]);
  // std::istringstream streamFirstFunction(argv[2]);
  // int firstFunction = 0;
  // streamFirstFunction >> firstFunction;
  // std::istringstream streamLastFunction(argv[3]);
  // int lastFunction = 0;
  // streamLastFunction >> lastFunction;
  // std::istringstream streamDimension(argv[4]);
  // int dimension = 0;
  // streamDimension >> dimension;
  std::string name = "rayes";
  int firstFunction = 1;
  int lastFunction = 48;
  int dimension = -1; // all dimensions

  coco_random_state_t *random_generator = coco_random_new(RANDOM_SEED);

  /* Change the log level to "warning" to get less output */
  coco_set_log_level("info");

  printf("Running the example experiment... (might take time, be patient)\n");
  fflush(stdout);

  example_experiment("bbob-constrained", "bbob", random_generator, name, firstFunction, lastFunction, dimension);

  /* Uncomment the line below to run the same example experiment on the bbob suite */
  /* example_experiment("bbob-biobj", "bbob-biobj", random_generator); */

  /* Uncomment the line below to run the same example experiment on the bbob suite */
  /* example_experiment("bbob", "bbob", random_generator); */

  printf("Done!\n");
  fflush(stdout);

  coco_random_free(random_generator);

  return 0;
}

/**
 * A simple example of benchmarking random search on a suite with instances from 2016 that can serve also as
 * a timing experiment.
 *
 * @param suite_name Name of the suite (use "bbob" for the single-objective,
 * "bbob-constrained" for the constrained problems suite and "bbob-biobj" for the
 * bi-objective suite).
 * @param observer_name Name of the observer (use "bbob" for the single-objective,
 * "bbob-constrained" for the constrained problems observer and "bbob-biobj" for the
 * bi-objective observer).
 * @param random_generator The random number generator.
 */
void example_experiment(const char *suite_name,
                        const char *observer_name,
                        coco_random_state_t */*random_generator*/,
                        const std::string &name,
                        int firstFunction,
                        int lastFunction,
                        int dimension) {

  size_t run;
  coco_suite_t *suite;
  coco_observer_t *observer;
  timing_data_t *timing_data;

  /* Set some options for the observer. See documentation for other options. */
  char *observer_options =
      coco_strdupf("result_folder: %s_on_%s_f%02d_%02d "
                   "algorithm_name: %s "
                   "algorithm_info: \"Evolutionary search algorithm\"", name.c_str(), suite_name, firstFunction, lastFunction, name.c_str());

  /* Initialize the suite and observer */
  if (dimension == -1) {
      suite = coco_suite(suite_name, "instances: 1-15", "dimensions: 2,3,5,10,20,40");
  } else {
      std::ostringstream streamDimensions;
      streamDimensions << "dimensions: " << dimension;
      std::string dimensionsString = streamDimensions.str();
      suite = coco_suite(suite_name, "", dimensionsString.c_str());
  }
  observer = coco_observer(observer_name, observer_options);
  coco_free_memory(observer_options);

  const int nInstances = 15;
  const int nTargetProblemBegin = firstFunction;
  const int nTargetProblemEnd = lastFunction;
  size_t prevDimension = 0;

  int cnt = 1;

  /* Initialize timing */
  timing_data = timing_data_initialize(suite);

  /* Iterate over all problems in the suite */
  while ((PROBLEM = coco_suite_get_next_problem(suite, observer)) != NULL) {
    
    size_t dimension = coco_problem_get_dimension(PROBLEM);

    if (dimension != prevDimension) {
      cnt = 1;
    }
    prevDimension = dimension;

    bool good = false;
    if ((((nTargetProblemBegin - 1) * nInstances) < cnt) &&
        (cnt < (nTargetProblemEnd * nInstances + 1))) {
        good = true;
    }
    if (!good) {
      cnt += 1;
      continue;
    }

    /* Run the algorithm at least once */
    for (run = 1; run <= 1 + INDEPENDENT_RESTARTS; run++) {

      size_t evaluations_done;
      
      evaluations_done = coco_problem_get_evaluations(PROBLEM) + 
            coco_problem_get_evaluations_constraints(PROBLEM);

      long evaluations_remaining = (long) (dimension * BUDGET_MULTIPLIER) - (long) evaluations_done;

      /* Break the loop if the target was hit or there are no more remaining evaluations */
      if ((coco_problem_final_target_hit(PROBLEM) && 
           coco_problem_get_number_of_constraints(PROBLEM) == 0)
           || (evaluations_remaining <= 0))
        break;

      /* Call the optimization algorithm for the remaining number of evaluations */
      my_search(evaluate_function,
                evaluate_constraint,
                dimension,
                coco_problem_get_number_of_objectives(PROBLEM),
                coco_problem_get_number_of_constraints(PROBLEM),
                coco_problem_get_smallest_values_of_interest(PROBLEM),
                coco_problem_get_largest_values_of_interest(PROBLEM),
                (size_t) evaluations_remaining);
      
      /* Break the loop if the algorithm performed no evaluations or an unexpected thing happened */
      if (coco_problem_get_evaluations(PROBLEM) == evaluations_done) {
        printf("WARNING: Budget has not been exhausted (%lu/%lu evaluations done)!\n",
        		(unsigned long) evaluations_done, (unsigned long) dimension * BUDGET_MULTIPLIER);
        break;
      }
      else if (coco_problem_get_evaluations(PROBLEM) + coco_problem_get_evaluations_constraints(PROBLEM) < evaluations_done)
        coco_error("Something unexpected happened - function evaluations were decreased!");
    }

    /* Keep track of time */
    timing_data_time_problem(timing_data, PROBLEM);

    cnt += 1;
  }

  printf("\n***** End of suite *****\n");
  
  /* Output and finalize the timing data */
  timing_data_finalize(timing_data);

  coco_observer_free(observer);
  coco_suite_free(suite);

}

/**
 * A random search algorithm that can be used for single- as well as multi-objective optimization.
 *
 * @param evaluate_function The function used to evaluate the objective function.
 * @param evaluate_constraint The function used to evaluate the constraints.
 * @param dimension The number of variables.
 * @param number_of_objectives The number of objectives.
 * @param number_of_constraints The number of constraints.
 * @param lower_bounds The lower bounds of the region of interested (a vector containing dimension values).
 * @param upper_bounds The upper bounds of the region of interested (a vector containing dimension values).
 * @param max_budget The maximal number of evaluations.
 * @param random_generator Pointer to a random number generator able to produce uniformly and normally
 * distributed random numbers.
 */
void my_random_search(evaluate_function_t evaluate_func,
                      evaluate_function_t evaluate_cons,
                      const size_t dimension,
                      const size_t number_of_objectives,
                      const size_t number_of_constraints,
                      const double *lower_bounds,
                      const double *upper_bounds,
                      const size_t max_budget,
                      coco_random_state_t *random_generator) {

  double *x = coco_allocate_vector(dimension);
  double *functions_values = coco_allocate_vector(number_of_objectives);
  double *constraints_values = NULL;
  double range;
  size_t i, j;
  
  if (number_of_constraints > 0 )
    constraints_values = coco_allocate_vector(number_of_constraints);

  for (i = 0; i < max_budget; ++i) {

    /* Construct x as a random point between the lower and upper bounds */
    for (j = 0; j < dimension; ++j) {
      range = upper_bounds[j] - lower_bounds[j];
      x[j] = lower_bounds[j] + coco_random_uniform(random_generator) * range;
    }
    /* Call COCO's evaluate function where all the logging is performed */
    evaluate_func(x, functions_values);
    
    if (number_of_constraints > 0 )
      evaluate_cons(x, constraints_values);

  }

  coco_free_memory(x);
  coco_free_memory(functions_values);
  if (number_of_constraints > 0 )
    coco_free_memory(constraints_values);
}

/**
 * A grid search optimizer that can be used for single- as well as multi-objective optimization.
 *
 * @param evaluate The evaluation function used to evaluate the solutions.
 * @param dimension The number of variables.
 * @param number_of_objectives The number of objectives.
 * @param lower_bounds The lower bounds of the region of interested (a vector containing dimension values).
 * @param upper_bounds The upper bounds of the region of interested (a vector containing dimension values).
 * @param max_budget The maximal number of evaluations.
 *
 * If max_budget is not enough to cover even the smallest possible grid, only the first max_budget
 * nodes of the grid are evaluated.
 */
void my_grid_search(evaluate_function_t evaluate,
                    const size_t dimension,
                    const size_t number_of_objectives,
                    const double *lower_bounds,
                    const double *upper_bounds,
                    const size_t max_budget) {

  double *x = coco_allocate_vector(dimension);
  double *y = coco_allocate_vector(number_of_objectives);
  long *nodes = (long *) coco_allocate_memory(sizeof(long) * dimension);
  double *grid_step = coco_allocate_vector(dimension);
  size_t i, j;
  size_t evaluations = 0;
  long max_nodes = (long) floor(pow((double) max_budget, 1.0 / (double) dimension)) - 1;

  /* Take care of the borderline case */
  if (max_nodes < 1) max_nodes = 1;

  /* Initialization */
  for (j = 0; j < dimension; j++) {
    nodes[j] = 0;
    grid_step[j] = (upper_bounds[j] - lower_bounds[j]) / (double) max_nodes;
  }

  while (evaluations < max_budget) {

    /* Construct x and evaluate it */
    for (j = 0; j < dimension; j++) {
      x[j] = lower_bounds[j] + grid_step[j] * (double) nodes[j];
    }

    /* Call the evaluate function to evaluate x on the current problem (this is where all the COCO logging
     * is performed) */
    evaluate(x, y);
    evaluations++;

    /* Inside the grid, move to the next node */
    if (nodes[0] < max_nodes) {
      nodes[0]++;
    }

    /* At an outside node of the grid, move to the next level */
    else if (max_nodes > 0) {
      for (j = 1; j < dimension; j++) {
        if (nodes[j] < max_nodes) {
          nodes[j]++;
          for (i = 0; i < j; i++)
            nodes[i] = 0;
          break;
        }
      }

      /* At the end of the grid, exit */
      if ((j == dimension) && (nodes[j - 1] == max_nodes))
        break;
    }
  }

  coco_free_memory(x);
  coco_free_memory(y);
  coco_free_memory(nodes);
  coco_free_memory(grid_step);
}

void my_search(evaluate_function_t evaluate_func,
               evaluate_function_t evaluate_cons,
               const size_t dimension,
               const size_t number_of_objectives,
               const size_t number_of_constraints,
               const double *lower_bounds,
               const double *upper_bounds,
               const size_t max_budget) {

    assert(number_of_objectives == 1);
    assert(number_of_constraints > 0);

    double *x = coco_allocate_vector(dimension);
    double *functions_values = coco_allocate_vector(number_of_objectives);
    double *constraints_values = coco_allocate_vector(number_of_constraints);

    /* evaluate once such that logger writes something in case we do not
       evaluate in the try catch below due to an exception for example */
    coco_problem_get_initial_solution(PROBLEM, x);
    evaluate_cons(x, constraints_values);
    evaluate_func(x, functions_values);

    auto evalConsWrapper = [=](const Eigen::VectorXd &xVec) {
        if (coco_problem_get_evaluations(PROBLEM) +
            coco_problem_get_evaluations_constraints(PROBLEM) > max_budget) {
            throw BudgetExhaustedException();
        }
        for (size_t i = 0; i < dimension; ++i) {
            x[i] = xVec(i);
        }
        evaluate_cons(x, constraints_values);
        Eigen::VectorXd y(number_of_constraints);
        for (size_t i = 0; i < number_of_constraints; ++i) {
            y(i) = constraints_values[i];
        }
        return y;
    };

    auto evalFuncWrapper = [=](const Eigen::VectorXd &xVec) {
        if (coco_problem_get_evaluations(PROBLEM) +
            coco_problem_get_evaluations_constraints(PROBLEM) > max_budget) {
            throw BudgetExhaustedException();
        }
        for (size_t i = 0; i < dimension; ++i) {
            x[i] = xVec(i);
        }
        evaluate_func(x, functions_values);
        return functions_values[0];
    };

    // Eigen::VectorXd z = Eigen::MatrixXd::Zero(dimension, 1);
    // double sigma = 1.0;
    Eigen::VectorXd lbnds(dimension);
    for (size_t i = 0; i < dimension; ++i) {
        lbnds(i) = lower_bounds[i];
    }
    Eigen::VectorXd ubnds(dimension);
    for (size_t i = 0; i < dimension; ++i) {
        ubnds(i) = upper_bounds[i];
    }

    try {
        double *values = coco_allocate_vector(dimension);
        coco_problem_get_initial_solution(PROBLEM, values);
        Eigen::VectorXd rayInit(dimension);
        for (size_t i = 0; i < dimension; ++i) {
            rayInit(i) = values[i];
        }
        coco_free_memory(values);
        es::rayes::RayEs solver(
                                evalFuncWrapper,
                                evalConsWrapper,
                                lbnds,
                                ubnds,
                                rayInit,
                                es::rayes::LineSearchAlg::Modified);
        es::rayes::Info info = solver.run();
        std::cout << "Termination criterion: "
                  << es::core::toString(info.getTerminationCriterion())
                  << "."
                  << std::endl;
    } catch (BudgetExhaustedException &) {
        // std::cout << "budget exhausted" << std::endl;
    } catch (std::exception &e) {
        std::cout << "unexpected error: " << e.what() << std::endl;
    }

    coco_free_memory(x);
    coco_free_memory(functions_values);
    coco_free_memory(constraints_values);
}

/**
 * Allocates memory for the timing_data_t object and initializes it.
 */
static timing_data_t *timing_data_initialize(coco_suite_t *suite) {

	timing_data_t *timing_data = (timing_data_t *) coco_allocate_memory(sizeof(*timing_data));
	size_t function_idx, dimension_idx, instance_idx, i;

	/* Find out the number of all dimensions */
	coco_suite_decode_problem_index(suite, coco_suite_get_number_of_problems(suite) - 1, &function_idx,
			&dimension_idx, &instance_idx);
	timing_data->number_of_dimensions = dimension_idx + 1;
	timing_data->current_idx = 0;
	timing_data->output = (char **) coco_allocate_memory(timing_data->number_of_dimensions * sizeof(char *));
	for (i = 0; i < timing_data->number_of_dimensions; i++) {
		timing_data->output[i] = NULL;
	}
	timing_data->previous_dimension = 0;
	timing_data->cumulative_evaluations = 0;
	time(&timing_data->start_time);
	time(&timing_data->overall_start_time);

	return timing_data;
}

/**
 * Keeps track of the total number of evaluations and elapsed time. Produces an output string when the
 * current problem is of a different dimension than the previous one or when NULL.
 */
static void timing_data_time_problem(timing_data_t *timing_data, coco_problem_t *problem) {

	double elapsed_seconds = 0;

	if ((problem == NULL) || (timing_data->previous_dimension != coco_problem_get_dimension(problem))) {

		/* Output existing timing information */
		if (timing_data->cumulative_evaluations > 0) {
			time_t now;
			time(&now);
			elapsed_seconds = difftime(now, timing_data->start_time) / (double) timing_data->cumulative_evaluations;
			timing_data->output[timing_data->current_idx++] = coco_strdupf("d=%lu done in %.2e seconds/evaluation\n",
					timing_data->previous_dimension, elapsed_seconds);
		}

		if (problem != NULL) {
			/* Re-initialize the timing_data */
			timing_data->previous_dimension = coco_problem_get_dimension(problem);
			timing_data->cumulative_evaluations = coco_problem_get_evaluations(problem);
			time(&timing_data->start_time);
		}

	} else {
		timing_data->cumulative_evaluations += coco_problem_get_evaluations(problem);
	}
}

/**
 * Outputs and finalizes the given timing data.
 */
static void timing_data_finalize(timing_data_t *timing_data) {

	/* Record the last problem */
	timing_data_time_problem(timing_data, NULL);

  if (timing_data) {
  	size_t i;
  	double elapsed_seconds;
		time_t now;
		int hours, minutes, seconds;

		time(&now);
		elapsed_seconds = difftime(now, timing_data->overall_start_time);

  	printf("\n");
  	for (i = 0; i < timing_data->number_of_dimensions; i++) {
    	if (timing_data->output[i]) {
				printf("%s", timing_data->output[i]);
				coco_free_memory(timing_data->output[i]);
    	}
    }
  	hours = (int) elapsed_seconds / 3600;
  	minutes = ((int) elapsed_seconds % 3600) / 60;
  	seconds = (int)elapsed_seconds - (hours * 3600) - (minutes * 60);
  	printf("Total elapsed time: %dh%02dm%02ds\n", hours, minutes, seconds);

    coco_free_memory(timing_data->output);
    coco_free_memory(timing_data);
  }
}
