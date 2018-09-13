#include <math.h>
#include <uWS/uWS.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "MPC.h"
#include "json.hpp"

// for convenience
using json = nlohmann::json;
using namespace Eigen;
using namespace std;

//debug
//#define USERDEBUG

#ifdef USERDEBUG
#define Debug(x) cout << x
#else
#define Debug(x)
#endif

// For converting back and forth between radians and degrees.
constexpr double pi() { return M_PI; }
double deg2rad(double x) { return x * pi() / 180; }
double rad2deg(double x) { return x * 180 / pi(); }

// Checks if the SocketIO event has JSON data.
// If there is data the JSON object in string format will be returned,
// else the empty string "" will be returned.
string hasData(string s) {
  auto found_null = s.find("null");
  auto b1 = s.find_first_of("[");
  auto b2 = s.rfind("}]");
  if (found_null != string::npos) {
    return "";
  } else if (b1 != string::npos && b2 != string::npos) {
    return s.substr(b1, b2 - b1 + 2);
  }
  return "";
}

// Evaluate a polynomial.
double polyeval(Eigen::VectorXd coeffs, double x) {
  double result = 0.0;
  for (int i = 0; i < coeffs.size(); i++) {
    result += coeffs[i] * pow(x, i);
  }
  return result;
}

// Fit a polynomial.
// Adapted from
// https://github.com/JuliaMath/Polynomials.jl/blob/master/src/Polynomials.jl#L676-L716
Eigen::VectorXd polyfit(Eigen::VectorXd xvals, Eigen::VectorXd yvals,
                        int order) {
  assert(xvals.size() == yvals.size());
  assert(order >= 1 && order <= xvals.size() - 1);
  Eigen::MatrixXd A(xvals.size(), order + 1);

  for (int i = 0; i < xvals.size(); i++) {
    A(i, 0) = 1.0;
  }

  for (int j = 0; j < xvals.size(); j++) {
    for (int i = 0; i < order; i++) {
      A(j, i + 1) = A(j, i) * xvals(j);
    }
  }

  auto Q = A.householderQr();
  auto result = Q.solve(yvals);
  return result;
}

int main() {
  uWS::Hub h;

  // MPC is initialized here!
  MPC mpc;

  h.onMessage([&mpc](uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
                     uWS::OpCode opCode) {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    string sdata = string(data).substr(0, length);
    Debug( sdata << endl);
    if (sdata.size() > 2 && sdata[0] == '4' && sdata[1] == '2') {
      string s = hasData(sdata);
      if (s != "") {
        auto j = json::parse(s);
        string event = j[0].get<string>();
        if (event == "telemetry") {
          // j[1] is the data JSON object
          //The global x positions of the way points.
          vector<double> ptsx = j[1]["ptsx"];
          //The global y positions of the way points.
          // This corresponds to the z coordinate in Unity since y is the up-down direction.
          vector<double> ptsy = j[1]["ptsy"];

          //The global x position of the vehicle.
          double px = j[1]["x"];
          //The global y position of the vehicle.
          double py = j[1]["y"];
          //The orientation of the vehicle in radians converted from the Unity format to
          // the standard format expected in most mathemetical functions
          double psi = j[1]["psi"];
          //The current velocity in mph.
          double v = j[1]["speed"];
          //The current control input -- delta
          double delta = j[1]["steering_angle"];
          //The current control input -- acceleration
          double a = j[1]["throttle"];

          /*
          * TODO: Calculate steering angle and throttle using MPC.
          *
          * Both are in between [-1, 1].
          *
          */

          //store way points based on the car coordinate system
          VectorXd ptsx_car(ptsx.size());
          VectorXd ptsy_car(ptsy.size());

          //transform way points from the global map coordinate to the vehicle coordinate, including
          //translation of axes, ref https://en.wikipedia.org/wiki/Translation_of_axes
          //rotation of axes, ref https://en.wikipedia.org/wiki/Rotation_of_axes
          for(unsigned int i = 0; i < ptsx.size(); i++){
              double x = ptsx[i] - px;
              double y = ptsy[i] - py;
              ptsx_car[i] = x * cos(psi) + y * sin(psi);
              ptsy_car[i] = - x * sin(psi) + y * cos(psi);
          }

          // fit a 3-rd polynomial to the way points based on the vehicle coordinate
          auto coeffs = polyfit(ptsx_car, ptsy_car, 3);

          // since we have transformed to the vehicle coordinate system, x, y and psi below are all zeros
          double state_x = 0.0;
          double state_y = 0.0;
          double state_psi = 0.0;
          double state_v = v;
          // calculate the cross track error
          // double cte = polyeval(coeffs, x) - y;
          double state_cte = polyeval(coeffs, state_x) - state_y;
          // Due to the sign starting at 0, the orientation error is -f'(x).
          // derivative of coeffs[0] + coeffs[1] * x -> coeffs[1]
          // double epsi = psi - atan(coeffs[1]);
          double state_epsi = state_psi - atan(coeffs[1]);

          //store the state values to vector state
          VectorXd state(6);
          //state << state_x, state_y, state_psi, state_v, state_cte, state_epsi;

          /* CHALLENGE PART : MPC WITH LATENCY
           * Requirements: The student implements Model Predictive Control that handles a 100 millisecond latency.
              Student provides details on how they deal with latency.
           * Action: to deal with the latency, I will project the vehicle's current state 100ms into the future
          // before running the MPC solver method
          */

          // This is the length from front to CoG that has a similar radius.
          const double Lf = 2.67;
          //latency time is 100 ms = 0.1 s
          const double time_latency = 0.1;
          //projected states
          //Note if delta is positive we rotate counter-clockwise, or turn left.
          // In the simulator however, a positive value implies a right turn and
          // a negative value implies a left turn. This is why we replace the (delta)
          // with (-delta) in the following equations
          double proj_x = state_x + state_v * cos(state_psi) * time_latency;
          double proj_y = state_y + state_v * sin(state_psi) * time_latency;
          double proj_psi = state_psi + state_v / Lf * (-delta) * time_latency;
          double proj_v = state_v + a * time_latency;
          double proj_cte = state_cte + state_v * sin(state_epsi) * time_latency;
          double proj_epsi = state_epsi + state_v / Lf * (-delta) * time_latency;

          //store the state values to vector state
          state << proj_x, proj_y, proj_psi, proj_v, proj_cte, proj_epsi;

          //Calculate the control signals via MPC
          auto vars = mpc.Solve(state, coeffs);

          //Get steer and throttle values
          double steer_value = vars[0];
          double throttle_value = vars[1];

          json msgJson;
          // NOTE: Remember to divide by deg2rad(25) before you send the steering value back.
          // Otherwise the values will be in between [-deg2rad(25), deg2rad(25] instead of [-1, 1].
          //The current steering angle in radians.
          msgJson["steering_angle"] = steer_value/(deg2rad(25) * 1.0);
          //The current throttle value [-1, 1].
          msgJson["throttle"] = throttle_value;

          //Display the MPC predicted trajectory 
          vector<double> mpc_x_vals;
          vector<double> mpc_y_vals;

          //.. add (x,y) points to list here, points are in reference to the vehicle's coordinate system
          // the points in the simulator are connected by a Green line
          for (unsigned int i = 2; i < vars.size(); i += 2) {
              mpc_x_vals.push_back(vars[i]);
              mpc_y_vals.push_back(vars[i+1]);
          }

          msgJson["mpc_x"] = mpc_x_vals;
          msgJson["mpc_y"] = mpc_y_vals;

          //Display the waypoints/reference line
          vector<double> next_x_vals;
          vector<double> next_y_vals;

          //.. add (x,y) points to list here, points are in reference to the vehicle's coordinate system
          // the points in the simulator are connected by a Yellow line
          for (double i = 0.0; i < 50.0; i += 2.0){
              next_x_vals.push_back(i);
              next_y_vals.push_back(polyeval(coeffs, i));
          }

          msgJson["next_x"] = next_x_vals;
          msgJson["next_y"] = next_y_vals;

          auto msg = "42[\"steer\"," + msgJson.dump() + "]";
          Debug( msg << endl);
          // Latency
          // The purpose is to mimic real driving conditions where
          // the car does actuate the commands instantly.
          //
          // Feel free to play around with this value but should be to drive
          // around the track with 100ms latency.
          //
          // NOTE: REMEMBER TO SET THIS TO 100 MILLISECONDS BEFORE
          // SUBMITTING.
          this_thread::sleep_for(chrono::milliseconds(100));
          ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
        }
      } else {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }
  });

  // We don't need this since we're not using HTTP but if it's removed the
  // program
  // doesn't compile :-(
  h.onHttpRequest([](uWS::HttpResponse *res, uWS::HttpRequest req, char *data,
                     size_t, size_t) {
    const std::string s = "<h1>Hello world!</h1>";
    if (req.getUrl().valueLength == 1) {
      res->end(s.data(), s.length());
    } else {
      // i guess this should be done more gracefully?
      res->end(nullptr, 0);
    }
  });

  h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
    std::cout << "Connected!!!" << std::endl;
  });

  h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                         char *message, size_t length) {
    ws.close();
    std::cout << "Disconnected" << std::endl;
  });

  int port = 4567;
  if (h.listen(port)) {
    std::cout << "Listening to port " << port << std::endl;
  } else {
    std::cerr << "Failed to listen to port" << std::endl;
    return -1;
  }
  h.run();
}
