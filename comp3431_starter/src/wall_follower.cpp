#include <wall_follower.h>


#define CLIP_0_1(X)         ((X) < 0?0:(X)>1?1:(X))

#define PI                  3.1415926

#define SIDE_ANGLE_START    30
#define SIDE_ANGLE_END      135

#define FRONT_ANGLE_LEFT    40
#define FRONT_ANGLE_RIGHT   330

WallFollower::WallFollower():Node("wall_follower") 
{
	RCLCPP_INFO(this->get_logger(), "I AM STARTING NOW");
	scanSub_ = this->create_subscription<sensor_msgs::msg::LaserScan>("scan", 1, std::bind(&WallFollower::callbackScan, this, std::placeholders::_1));

	commandSub_ = this->create_subscription<std_msgs::msg::String>("cmd", 1, std::bind(&WallFollower::callbackControl, this, std::placeholders::_1));

	twistPub_ = this->create_publisher<geometry_msgs::msg::Twist>("cmd_vel", 1);

	paused = true;
}


void WallFollower::callbackScan(const sensor_msgs::msg::LaserScan::SharedPtr scan)
{
	//std::cout << "Hi\n";
	if(paused){
		geometry_msgs::msg::Twist t;
		t.linear.x = t.linear.y = t.linear.z = 0;
		t.angular.x = t.angular.y = t.angular.z = 0;
		twistPub_->publish(t);
		stopped = true;
		return;
	}
	std::string warning_msg;


	float XMaxSide = -INFINITY, XMinFront = INFINITY, angle = scan->angle_min;
	float RangeSidePercentage = 0, RangeFrontPercentage = 0;
	for(auto it = scan->ranges.begin(); it != scan->ranges.end(); ++it, angle += scan->angle_increment)
	{   
        float angle_d = angle * 180 / PI;

		geometry_msgs::msg::Vector3 point;
		point.x = cos(angle) * *it;
		point.y = sin(angle) * *it;
		point.z = 0;
		if (fabs(point.x) <= ROBOT_RADIUS_SIDE && fabs(point.y) <= MAX_SIDE_LIMIT) // point is on the side && NOT too far
		{
			if ((side == LEFT && point.y > 0) || (side == RIGHT && point.y < 0)) // on the chosen side
			{
				// Point is beside the robot
				if (point.x > XMaxSide)
					XMaxSide = point.x; // max value on the heading direction
			}
		}

		// Find min XF of a hit in front of robot (X > 0, abs(Y) <= robot radius, X <= limit)
		if (point.x > 0 && point.x <= MAX_APPROACH_DIST && fabs(point.y) <= ROBOT_RADIUS) 
		{
			// Point is in front of the robot
			if (point.x < XMinFront)
				XMinFront = point.x;
		}
        if ((side==LEFT && angle_d >= SIDE_ANGLE_START && angle_d < SIDE_ANGLE_END)){
            if (*it < 0.75)
                RangeSidePercentage++;
        }
        if ((side==LEFT && (angle_d <= FRONT_ANGLE_LEFT || angle_d > FRONT_ANGLE_RIGHT) )){
            if (*it < 1)
                RangeFrontPercentage++;
        }
    }

    RangeSidePercentage = RangeSidePercentage / (SIDE_ANGLE_END-SIDE_ANGLE_START);
    RangeFrontPercentage = RangeFrontPercentage / (360 - FRONT_ANGLE_RIGHT + FRONT_ANGLE_LEFT);

    RCLCPP_INFO(this->get_logger(), "XMaxSide: %.3f XMinFront: %.3f RangeSidePercentage: %.3f RangeFrontPercentage: %.3f", XMaxSide, XMinFront, RangeSidePercentage, RangeFrontPercentage);
    
    float turn, drive;
    if (RangeFrontPercentage > 0.75){
      turn = -1;
      drive = 0;
    } else if (RangeSidePercentage > 0.85) {
        turn = -0.5;
        drive = 0.1;
    } else if (RangeSidePercentage > 0.75) {
        turn = -0.75;
        drive = 1;
    } else if (XMaxSide == -INFINITY) {
        //RCLCPP_INFO(this->get_logger(), "Could not find wall, I'm looking, please don't get mad!!");
        // No hits beside robot, so turn that direction
        turn = 1;
        drive = 0;
    } else if (XMinFront <= MIN_APPROACH_DIST) {
        //RCLCPP_INFO(this->get_logger(), "Could not find wall, I'm looking, please don't get mad!!");
        // Blocked side and front, so turn other direction
        turn = -1;
        drive = 0;
    } else {
        //RCLCPP_INFO(this->get_logger(), "going straight on!");
        // turn1 = (radius - XS) / 2*radius  // Clipped to range (0..1)
        float turn1f = (ROBOT_RADIUS_SIDE - XMaxSide) / (2 * ROBOT_RADIUS_SIDE);
        float turn1 = CLIP_0_1(turn1f);

        // drive1 = (radius + XS) / 2*radius // Clipped to range (0..1)
        float drive1f = (ROBOT_RADIUS_SIDE + XMaxSide) / (2 * ROBOT_RADIUS_SIDE);
        float drive1 = CLIP_0_1(drive1f);

        // drive2 = (XF - min) / (limit - min)  // Clipped to range (0..1)
        float drive2f = (XMinFront - MIN_APPROACH_DIST) / (MAX_APPROACH_DIST - MIN_APPROACH_DIST);
        float drive2 = CLIP_0_1(drive2f);

        // turn2 = (limit - XF) / (limit - min) // Clipped to range (0..1)
        float turn2f = (MAX_APPROACH_DIST - XMinFront) / (MAX_APPROACH_DIST - MIN_APPROACH_DIST);
        float turn2 = CLIP_0_1(turn2f);

        // turn = turn1 - turn2
        turn = turn1 - turn2 ;//- 0.01;
        

        // drive = drive1 * drive2
        drive = drive1 * drive2;
        //RCLCPP_INFO(this->get_logger(), "turn1f %.2f , turn2f %.2f ", turn1f, turn2f);
        //RCLCPP_INFO(this->get_logger(), "drive1f %.2f , drive2f %.2f\n", drive1f, drive2f);
    }

    if (side == RIGHT) {
        turn *= -1;
    }

    // publish twist
    geometry_msgs::msg::Twist t;
    t.linear.y = t.linear.z = 0;
    t.linear.x = drive * MAX_SPEED;
    t.angular.x = t.angular.y = 0;
    t.angular.z = turn * MAX_TURN;

    RCLCPP_INFO(this->get_logger(), "Publishing velocities %.2f m/s, %.2f r/s\n", t.linear.x, t.angular.z);
    twistPub_->publish(t);
    stopped = false;

} 

void WallFollower::callbackControl(const std_msgs::msg::String::SharedPtr command)
{
	RCLCPP_INFO(this->get_logger(), "Recieved %s message.\n", command->data.c_str());
	
	std::string message = std::string{command->data};
	if(message == "start") {
		//RCLCPP_INFO(this->get_logger(), "Alright, let's get this show on the road!!!");
		paused = false;
	}
	else if(message == "stop") {
		//RCLCPP_INFO(this->get_logger(), "Stopping, don't forget to save that lovely map or it'll be lost forever!!!");
		paused = true;
		system("gnome-terminal -- sh -c '. install/local_setup.bash;ros2 run nav2_map_server map_saver_cli -f map_house1'");
		rclcpp::shutdown();
	}

}
