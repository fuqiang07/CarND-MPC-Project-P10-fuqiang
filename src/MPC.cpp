#include "MPC.h"
#include <cppad/cppad.hpp>
#include <cppad/ipopt/solve.hpp>
#include <iostream>
#include "Eigen-3.3/Eigen/Core"

using CppAD::AD;
using namespace std;
//using namespace Eigen;

//debug
//#define USERDEBUG

#ifdef USERDEBUG
#define Debug(x) cout << x
#else
#define Debug(x)
#endif

// TODO: Set the timestep length and duration
/*Tuning process:
 * 1. [Setting] T = N * dt = 1 s, and set dt = 0.1s = 100 ms (same as latency), then N = T / dt = 10.
 *    [Result]: very good. but to view the effects, I change the settings for more fun
 * 2. [Setting] T = N * dt = 2 s, and set dt = 0.1s = 100 ms (same as latency), then N = T / dt = 20.
 *    [Result]: becomes worse at sharp turns (less margin, nearly hit the sidewalk).
 *            Moreover, trajectories predicted by MPC can not fit the desired trajectory at sharp turns.
 *    [Analysis]: the kinematic model is not real dynamic model of vehicles. It cannot model the vehicles very well
 *            at sharp turns. Hence we should not predict a long time. Here, T = 1s is better than T = 2s.
 * 3. [Setting] T = N * dt = 1 s, and set dt = 0.051s = 50 ms, then N = T / dt = 20.
 *    [Result]: the result is very bad. the vehicle is even not stable, like a drunk driver. Finally, the vehicle
 *            run out of the lane at the first sharp turn.
 *    [Analysis]: I cannot figure out the reason. From the lessons, the prediction should be more precise with
 *            smaller dt. I just doubt that the dt = 50 ms not equal to latency 100ms.To verify it, I can try
 *            1) set N = 10, dt = 0.05. If the result is very good, then it is not related to latency time.
 *            2) revise the latency to 50ms
 * 4. [Setting] T = N * dt = 0.5 s, and set dt = 0.05s = 50 ms, then N = T / dt = 10.
 *    [Result]: the result is a little bit better than the previous one. It can stay within the lane but
 *            the angle changes too fast, still like a drunk driver.
 * 5. [Setting] T = N * dt = 1 s, and set dt = 0.05s = 50 ms, then N = T / dt = 20. latency = 50ms
 *    [Result]: the result become better. But the vehicle become unstable at sharp turns and finally runs out of lane.
 * 6. [Setting] T = N * dt = 0.5 s, and set dt = 0.05s = 50 ms, then N = T / dt = 10. latency = 50ms
 *    [Result]: the result is very good. similar to the first setting with latency = 100ms.
 *    [Analysis]: Based on the above few experiments, I conclude that dt should be set the same as latency time.
 *            This would be helpful to deal with the problem brought by latency.
 *    */
size_t N = 10;
double dt = 0.1;

// This value assumes the model presented in the classroom is used.
//
// It was obtained by measuring the radius formed by running the vehicle in the
// simulator around in a circle with a constant steering angle and velocity on a
// flat terrain.
//
// Lf was tuned until the the radius formed by the simulating the model
// presented in the classroom matched the previous radius.
//
// This is the length from front to CoG that has a similar radius.
const double Lf = 2.67;

// Both the reference cross track and orientation errors are 0.
// The reference velocity is set to 40 mph.
double ref_v = 70;

// The solver takes all the state variables and actuator
// variables in a singular vector. Thus, we should to establish
// when one variable starts and another ends to make our lifes easier.
size_t x_start = 0;
size_t y_start = x_start + N;
size_t psi_start = y_start + N;
size_t v_start = psi_start + N;
size_t cte_start = v_start + N;
size_t epsi_start = cte_start + N;
size_t delta_start = epsi_start + N;
size_t a_start = delta_start + N - 1;

class FG_eval {
 public:
  // Fitted polynomial coefficients
  Eigen::VectorXd coeffs;
  FG_eval(Eigen::VectorXd coeffs) { this->coeffs = coeffs; }

  typedef CPPAD_TESTVECTOR(AD<double>) ADvector;
  // `fg` is a vector containing the cost and constraints.
  // `vars` is a vector containing the variable values (state & actuators).
  void operator()(ADvector& fg, const ADvector& vars) {
    // TODO: implement MPC
    // `fg` a vector of the cost constraints, `vars` is a vector of variable values (state & actuators)
    // NOTE: You'll probably go back and forth between this function and
    // the Solver function below.

    // The cost is stored is the first element of `fg`.
    // Any additions to the cost should be added to `fg[0]`.
    fg[0] = 0;

    //define weights for cost functions
    //tuning these weights
    //1. the cte and orientation error is the most important, hence we set them to 1000
    //1.1 the above values are increased to 2000 to avoid abnormal MPC prediction in sharp turn
    //2. the steering should not be changed so sharp and the jerk should not be large,
    // considering safety and comfort of passengers, hence they are  set to 100
    //3. we want limit the steering angle and acceleration, whose weights are set to 10
    //4. the constant velocity is the least important, hence we just set it to 1
    const double weight_cte = 2000.0;  //weight for cross track error
    const double weight_epsi = 2000.0;  //weight for orientation error
    const double weight_v = 1.0;  //weight for velocity
    const double weight_delta = 10.0;  //weight for steering angle
    const double weight_a = 10.0;  //weight for acceleration
    const double weight_delta_diff = 100.0;  //weight for delta differentiate
    const double weight_a_diff = 100.0;  //weight for jerk

    // The part of the cost based on the reference state.
    for (unsigned int t = 0; t < N; t++) {
      fg[0] += weight_cte * CppAD::pow(vars[cte_start + t], 2);
      fg[0] += weight_epsi * CppAD::pow(vars[epsi_start + t], 2);
      fg[0] += weight_v * CppAD::pow(vars[v_start + t] - ref_v, 2);
    }

    // Minimize the use of actuators.
    for (unsigned int t = 0; t < N - 1; t++) {
      fg[0] += weight_delta * CppAD::pow(vars[delta_start + t], 2);
      fg[0] += weight_a * CppAD::pow(vars[a_start + t], 2);
    }

    // Minimize the value gap between sequential actuations.
    for (unsigned int t = 0; t < N - 2; t++) {
      fg[0] += weight_delta_diff * CppAD::pow(vars[delta_start + t + 1] - vars[delta_start + t], 2);
      fg[0] += weight_a_diff * CppAD::pow(vars[a_start + t + 1] - vars[a_start + t], 2);
    }

    //
    // Setup Constraints
    //
    // NOTE: In this section you'll setup the model constraints.

    // Initial constraints
    //
    // We add 1 to each of the starting indices due to cost being located at
    // index 0 of `fg`.
    // This bumps up the position of all the other values.
    fg[1 + x_start] = vars[x_start];
    fg[1 + y_start] = vars[y_start];
    fg[1 + psi_start] = vars[psi_start];
    fg[1 + v_start] = vars[v_start];
    fg[1 + cte_start] = vars[cte_start];
    fg[1 + epsi_start] = vars[epsi_start];

    // The rest of the constraints
    for (unsigned int t = 1; t < N; t++) {
      // The state at time t+1 .
      AD<double> x1 = vars[x_start + t];
      AD<double> y1 = vars[y_start + t];
      AD<double> psi1 = vars[psi_start + t];
      AD<double> v1 = vars[v_start + t];
      AD<double> cte1 = vars[cte_start + t];
      AD<double> epsi1 = vars[epsi_start + t];

      // The state at time t.
      AD<double> x0 = vars[x_start + t - 1];
      AD<double> y0 = vars[y_start + t - 1];
      AD<double> psi0 = vars[psi_start + t - 1];
      AD<double> v0 = vars[v_start + t - 1];
      AD<double> cte0 = vars[cte_start + t - 1];
      AD<double> epsi0 = vars[epsi_start + t - 1];

      // Only consider the actuation at time t.
      AD<double> delta0 = vars[delta_start + t - 1];
      AD<double> a0 = vars[a_start + t - 1];

      // note that we are using the 3rd order polynomial now.
      // f = k0 + k1*x + k2*x^2 + k3*x^3
      // psi_des = atan (f') = atan (k1 + 2*k2*x + 3*k3*x^2)
      AD<double> f0 = coeffs[0] + coeffs[1] * x0 + coeffs[2] * pow(x0, 2) + coeffs[3] * pow(x0, 3);
      AD<double> psides0 = CppAD::atan(coeffs[1] + 2 * coeffs[2] * x0 + 3 * coeffs[3] * pow(x0, 2));

      // Here's `x` to get you started.
      // The idea here is to constraint this value to be 0.
      //
      // Recall the equations for the model:
      // x_[t+1] = x[t] + v[t] * cos(psi[t]) * dt
      // y_[t+1] = y[t] + v[t] * sin(psi[t]) * dt
      // psi_[t+1] = psi[t] + v[t] / Lf * delta[t] * dt
      // v_[t+1] = v[t] + a[t] * dt
      // cte[t+1] = f(x[t]) - y[t] + v[t] * sin(epsi[t]) * dt
      // epsi[t+1] = psi[t] - psides[t] + v[t] * delta[t] / Lf * dt

      //Note if delta is positive we rotate counter-clockwise, or turn left.
      // In the simulator however, a positive value implies a right turn and
      // a negative value implies a left turn. This is why we update the (delta0)
      // as (-delta0) in the following equations
      fg[1 + x_start + t] = x1 - (x0 + v0 * CppAD::cos(psi0) * dt);
      fg[1 + y_start + t] = y1 - (y0 + v0 * CppAD::sin(psi0) * dt);
      fg[1 + psi_start + t] = psi1 - (psi0 + v0 * (-delta0) / Lf * dt);
      fg[1 + v_start + t] = v1 - (v0 + a0 * dt);
      fg[1 + cte_start + t] =
              cte1 - ((f0 - y0) + (v0 * CppAD::sin(epsi0) * dt));
      fg[1 + epsi_start + t] =
              epsi1 - ((psi0 - psides0) + v0 * (-delta0) / Lf * dt);
    }

  }
};

//
// MPC class definition implementation.
//
MPC::MPC() {}
MPC::~MPC() {}

vector<double> MPC::Solve(Eigen::VectorXd state, Eigen::VectorXd coeffs) {
  bool ok = true;
  //size_t i;
  typedef CPPAD_TESTVECTOR(double) Dvector;

  double x = state[0];
  double y = state[1];
  double psi = state[2];
  double v = state[3];
  double cte = state[4];
  double epsi = state[5];

  // TODO: Set the number of model variables (includes both states and inputs).
  // For example: If the state is a 4 element vector, the actuators is a 2
  // element vector and there are 10 timesteps. The number of variables is:
  //
  // 4 * 10 + 2 * 9
  //the number of states x, y, psi, v, cte, epsi
  const int n_state = 6;
  //the number of control inputs delta (steering angle) and a (acceleration)
  const int n_actuator = 2;
  size_t n_vars = n_state * N + n_actuator * (N-1);
  // TODO: Set the number of constraints
  size_t n_constraints = n_state * N;

  // Initial value of the independent variables.
  // SHOULD BE 0 besides initial state.
  Dvector vars(n_vars);
  for (unsigned int i = 0; i < n_vars; i++) {
    vars[i] = 0;
  }
  // Set the initial variable values
  vars[x_start] = x;
  vars[y_start] = y;
  vars[psi_start] = psi;
  vars[v_start] = v;
  vars[cte_start] = cte;
  vars[epsi_start] = epsi;

  // Lower and upper limits for x
  Dvector vars_lowerbound(n_vars);
  Dvector vars_upperbound(n_vars);
  // TODO: Set lower and upper limits for variables.
  // Set all non-actuators upper and lower limits
  // to the max negative and positive values.
  for (unsigned int i = 0; i < delta_start; i++) {
    vars_lowerbound[i] = -1.0e19;
    vars_upperbound[i] = 1.0e19;
  }

  // The upper and lower limits of delta are set to -25 and 25
  // degrees (values in radians).
  // NOTE: Feel free to change this to something else.
  for (unsigned int i = delta_start; i < a_start; i++) {
    vars_lowerbound[i] = -0.436332;
    vars_upperbound[i] = 0.436332;
  }

  // Acceleration/decceleration upper and lower limits.
  // NOTE: Feel free to change this to something else.
  for (unsigned int i = a_start; i < n_vars; i++) {
    vars_lowerbound[i] = -1.0;
    vars_upperbound[i] = 1.0;
  }

  // Lower and upper limits for the constraints
  // Should be 0 besides initial state.
  Dvector constraints_lowerbound(n_constraints);
  Dvector constraints_upperbound(n_constraints);
  for (unsigned int i = 0; i < n_constraints; i++) {
    constraints_lowerbound[i] = 0;
    constraints_upperbound[i] = 0;
  }
  constraints_lowerbound[x_start] = x;
  constraints_lowerbound[y_start] = y;
  constraints_lowerbound[psi_start] = psi;
  constraints_lowerbound[v_start] = v;
  constraints_lowerbound[cte_start] = cte;
  constraints_lowerbound[epsi_start] = epsi;

  constraints_upperbound[x_start] = x;
  constraints_upperbound[y_start] = y;
  constraints_upperbound[psi_start] = psi;
  constraints_upperbound[v_start] = v;
  constraints_upperbound[cte_start] = cte;
  constraints_upperbound[epsi_start] = epsi;

  // object that computes objective and constraints
  FG_eval fg_eval(coeffs);

  //
  // NOTE: You don't have to worry about these options
  //
  // options for IPOPT solver
  std::string options;
  // Uncomment this if you'd like more print information
  options += "Integer print_level  0\n";
  // NOTE: Setting sparse to true allows the solver to take advantage
  // of sparse routines, this makes the computation MUCH FASTER. If you
  // can uncomment 1 of these and see if it makes a difference or not but
  // if you uncomment both the computation time should go up in orders of
  // magnitude.
  options += "Sparse  true        forward\n";
  options += "Sparse  true        reverse\n";
  // NOTE: Currently the solver has a maximum time limit of 0.5 seconds.
  // Change this as you see fit.
  options += "Numeric max_cpu_time          0.5\n";

  // place to return solution
  CppAD::ipopt::solve_result<Dvector> solution;

  // solve the problem
  CppAD::ipopt::solve<Dvector, FG_eval>(
      options, vars, vars_lowerbound, vars_upperbound, constraints_lowerbound,
      constraints_upperbound, fg_eval, solution);

  // Check some of the solution values
  ok &= solution.status == CppAD::ipopt::solve_result<Dvector>::success;

  // Cost
  //auto cost = solution.obj_value;
  //std::cout << "Cost " << cost << std::endl;

  // TODO: Return the first actuator values. The variables can be accessed with
  // `solution.x[i]`.
  //
  // {...} is shorthand for creating a vector, so auto x1 = {1.0,2.0}
  // creates a 2 element double vector.
  /*
  return {solution.x[x_start + 1],   solution.x[y_start + 1],
          solution.x[psi_start + 1], solution.x[v_start + 1],
          solution.x[cte_start + 1], solution.x[epsi_start + 1],
          solution.x[delta_start],   solution.x[a_start]};
          */

  vector<double> result;

  //store the control signals: steering angle -- delta and acceleration -- a
  result.push_back(solution.x[delta_start]);
  result.push_back(solution.x[a_start]);

  //store the predicted trajectory: x and y points, which we need to plot the green line in the simulator
  for (unsigned int i = 0; i < N; i++) {
    result.push_back(solution.x[x_start + i]);
    result.push_back(solution.x[y_start + i]);
  }

  return result;
}
