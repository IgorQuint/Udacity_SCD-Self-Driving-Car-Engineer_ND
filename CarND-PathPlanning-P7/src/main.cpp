#include <uWS/uWS.h>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "helpers.h"
#include "json.hpp"
// include spline header for interpolation
#include "spline.h"


// for convenience
using nlohmann::json;
using std::string;
using std::vector;
using std::abs;


int main() {
  uWS::Hub h;

  // Load up map values for waypoint's x,y,s and d normalized normal vectors
  vector<double> map_waypoints_x;
  vector<double> map_waypoints_y;
  vector<double> map_waypoints_s;
  vector<double> map_waypoints_dx;
  vector<double> map_waypoints_dy;

  // Waypoint map to read from
  string map_file_ = "../data/highway_map.csv";
  // The max s value before wrapping around the track back to 0
  double max_s = 6945.554;


  
  std::ifstream in_map_(map_file_.c_str(), std::ifstream::in);

  string line;
  while (getline(in_map_, line)) {
    std::istringstream iss(line);
    double x;
    double y;
    float s;
    float d_x;
    float d_y;
    iss >> x;
    iss >> y;
    iss >> s;
    iss >> d_x;
    iss >> d_y;
    map_waypoints_x.push_back(x);
    map_waypoints_y.push_back(y);
    map_waypoints_s.push_back(s);
    map_waypoints_dx.push_back(d_x);
    map_waypoints_dy.push_back(d_y);
  }

  	  //initial values
  int initlane = 1;
  double ref_velocity = 0.0;
  double max_velocity = 48.9;
  
  // (initial) lane, reference velocity and max velocity passed within lamda
  h.onMessage([&map_waypoints_x,&map_waypoints_y,&map_waypoints_s,
               &map_waypoints_dx,&map_waypoints_dy,&initlane,&ref_velocity,&max_velocity]
              (uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
               uWS::OpCode opCode) {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    if (length && length > 2 && data[0] == '4' && data[1] == '2') {

      auto s = hasData(data);

      if (s != "") {
        auto j = json::parse(s);
        
        string event = j[0].get<string>();
        
        if (event == "telemetry") {
          // j[1] is the data JSON object
          
          // Main car's localization Data
          double car_x = j[1]["x"];
          double car_y = j[1]["y"];
          double car_s = j[1]["s"];
          double car_d = j[1]["d"];
          double car_yaw = j[1]["yaw"];
          double car_speed = j[1]["speed"];

          // Previous path data given to the Planner
          auto previous_path_x = j[1]["previous_path_x"];
          auto previous_path_y = j[1]["previous_path_y"];
          // Previous path's end s and d values 
          double end_path_s = j[1]["end_path_s"];
          double end_path_d = j[1]["end_path_d"];

          // Sensor Fusion Data
          auto sensor_fusion = j[1]["sensor_fusion"];

          json msgJson;


          // Define waypoint x and y points
          vector<double> next_x_vals;
          vector<double> next_y_vals;
          vector<double> wpx;
          vector<double> wpy;

                  int prev_size = previous_path_x.size();
                  if (prev_size > 0) {
                      car_s = end_path_s;
                  }

                  bool too_close = false;
                  // boolean vector with open/closed status per lane  
                  vector<bool> OpenLanes = {true, true, true};
                  OpenLanes[initlane] = false;

                  for (int i = 0; i < sensor_fusion.size(); i++) {
                      float d = sensor_fusion[i][6];
                      double vx = sensor_fusion[i][3];
                      double vy = sensor_fusion[i][4];
                      double check_speed = sqrt(vx*vx+vy*vy);
                      double check_car_s = sensor_fusion[i][5];
                      double check_car_lane = lanenum(d);

                      check_car_s += (double) prev_size * 0.02 * check_speed;
                    // if car in same lane within 30m, set bool to true
                      if ((check_car_lane == initlane) && (check_car_s > car_s) && ((check_car_s-car_s) < 25.0)) {
                          too_close = true;
                      }
                    // if car in other lane 15m ahead?
                      if ((check_car_lane != initlane) && (check_car_s > car_s) && (check_car_s-car_s) < 25.0) {
                          OpenLanes[check_car_lane] = false;
                        
                   // if car behind within 15m?
                    } else if ((check_car_lane != initlane) && (check_car_s < car_s) && (car_s-check_car_s) < 25.0) {
                          OpenLanes[check_car_lane] = false;
                      }

                  }

                  // If car in front is too close, start decellerating with 5ms2 or change langes 
                  if (too_close) {
                   // 5ms2
                      ref_velocity -= .224;
                      for (int i = 0; i < 3; i++) {
                          if (OpenLanes[i] && abs(i - initlane) <= 1) {
                              initlane = i;
                          }
                      }
                    // if not too close, accelerate to max velocity in increments (just under 5ms2)
                  } else if (ref_velocity < max_velocity) {
                    // see Q&A video
                      ref_velocity += 0.20;
                  }

                  double ref_x = car_x;
                  double ref_y = car_y;
                  double ref_yaw = deg2rad(car_yaw);

                  if (prev_size < 2) {
                      double prev_car_x = car_x - cos(car_yaw);
                      double prev_car_y = car_y - sin(car_yaw);

                      wpx.push_back(prev_car_x);
                      wpx.push_back(car_x);

                      wpy.push_back(prev_car_y);
                      wpy.push_back(car_y);
                  } else {
                      ref_x = previous_path_x[prev_size-1];
                      ref_y = previous_path_y[prev_size-1];

                      double ref_x_prev = previous_path_x[prev_size-2];
                      double ref_y_prev = previous_path_y[prev_size-2];
                      ref_yaw = atan2(ref_y-ref_y_prev, ref_x-ref_x_prev);

                      wpx.push_back(ref_x_prev);
                      wpx.push_back(ref_x);

                      wpy.push_back(ref_y_prev);
                      wpy.push_back(ref_y);
                  }

                  vector<double> next_wp0 = getXY(car_s+30,(2+4*initlane),map_waypoints_s,map_waypoints_x,map_waypoints_y);
                  vector<double> next_wp1 = getXY(car_s+60,(2+4*initlane),map_waypoints_s,map_waypoints_x,map_waypoints_y);
                  vector<double> next_wp2 = getXY(car_s+90,(2+4*initlane),map_waypoints_s,map_waypoints_x,map_waypoints_y);

                  wpx.push_back(next_wp0[0]);
                  wpx.push_back(next_wp1[0]);
                  wpx.push_back(next_wp2[0]);

                  wpy.push_back(next_wp0[1]);
                  wpy.push_back(next_wp1[1]);
                  wpy.push_back(next_wp2[1]);

                  for (int i = 0; i < wpx.size(); i++) {
                      double increment_x = wpx[i] - ref_x;
                      double increment_y = wpy[i] - ref_y;

                      wpx[i] = (increment_x * cos(0-ref_yaw) - increment_y * sin(0-ref_yaw));
                      wpy[i] = (increment_x * sin(0-ref_yaw) + increment_y * cos(0-ref_yaw));
                  }
                  // Create spline
                  tk::spline s;
          
                  // Set waypoints to spline
                  s.set_points(wpx, wpy);


                  for (int i = 0; i < previous_path_x.size(); i++) {
                      next_x_vals.push_back(previous_path_x[i]);
                      next_y_vals.push_back(previous_path_y[i]);
                  }
          
                  // Calculate break up spline points
                  double target_x = 30.0;
                  double target_y = s(target_x);
                  double target_dist = sqrt((target_x) * (target_x) + (target_y) * (target_y));
          
                  double x_add_on = 0;

                  for (int i = 1; i <= 50-previous_path_x.size(); i++) {
                      double N = (target_dist/(0.02*ref_velocity/2.24));
                      double x_point = x_add_on+(target_x)/N;
                      double y_point = s(x_point);

                      x_add_on = x_point;
                      double x_ref = x_point;
                      double y_ref = y_point;

                      x_point = (x_ref*cos(ref_yaw)-y_ref*sin(ref_yaw));
                      y_point = (x_ref*sin(ref_yaw)+y_ref*cos(ref_yaw));
                      x_point += ref_x;
                      y_point += ref_y;

                      next_x_vals.push_back(x_point);
                      next_y_vals.push_back(y_point);
                  }

          msgJson["next_x"] = next_x_vals;
          msgJson["next_y"] = next_y_vals;

          auto msg = "42[\"control\","+ msgJson.dump()+"]";

          ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
        }  // end "telemetry" if
      } else {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }  // end websocket if
  }); // end h.onMessage

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