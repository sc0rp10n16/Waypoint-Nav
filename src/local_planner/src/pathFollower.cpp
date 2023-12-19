#include <math.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <ros/ros.h>

#include <message_filters/subscriber.h>
#include <message_filters/synchronizer.h>
#include <message_filters/sync_policies/approximate_time.h>

#include <std_msgs/Float32.h>
#include <nav_msgs/Path.h>
#include <nav_msgs/Odometry.h>
#include <geometry_msgs/PointStamped.h>
#include <sensor_msgs/PointCloud2.h>
#include <sensor_msgs/Joy.h>
#include <visualization_msgs/Marker.h>

#include <tf/transform_datatypes.h>
#include <tf/transform_broadcaster.h>

#include <pcl_conversions/pcl_conversions.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/kdtree/kdtree_flann.h>

using namespace std;

const double PI = 3.1415926;

string stateEstimationTopic = "/state_estimation";
string desiredTrajFile;
string executedTrajFile;
bool saveTrajectory = false;
double saveTrajInverval = 0.1;
bool waypointTest = false;
int waypointNum = 6;
double waypointInterval = 1.0;
double waypointYaw = 45.0;
double waypointZ = 2.0;
bool autonomyMode = false;
int pubSkipNum = 1;
int pubSkipCount = 0;
bool trackingCamBackward = false;
double trackingCamXOffset = 0;
double trackingCamYOffset = 0;
double trackingCamZOffset = 0;
double trackingCamScale = 1.0;
double lookAheadScale = 0.2;
double minLookAheadDis = 0.2;
double minSpeed = 0.5;
double maxSpeed = 2.0;
double desiredSpeed = minSpeed;
double accXYGain = 0.05;
double velXYGain = 0.4;
double posXYGain = 0.05;
double stopVelXYGain = 0.2;
double stopPosXYGain = 0.2;
double smoothIncrSpeed = 0.75;
double maxRollPitch = 30.0;
double yawRateScale = 1.0;
double yawGain = 2.0;
double yawBoostScale = 2.0;
double maxRateByYaw = 60.0;
double velZScale = 1.0;
double posZGain = 1.5;
double posZBoostScale = 2.0;
double maxVelByPosZ = 0.5;
double manualSpeedXY = 2.0;
double manualSpeedZ = 1.0;
double manualYawRate = 60.0;
double slowTurnRate = 0.75;
double minSlowTurnCurv = 0.9;
double minSlowTurnInterval = 1.0;
double minStopRotInterval = 1.0;
double stopRotDelayTime = 0;
double stopRotDis = 1.0;
double stopRotYaw1 = 90.0;
double stopRotYaw2 = 10.0;
double stopDis = 0.5;
double slowDis = 2.0;
double joyDeadband = 0.1;
double joyToSpeedDelay = 2.0;
bool shiftGoalAtStart = false;
double goalX = 0;
double goalY = 0;
double goalZ = 1.0;

int trackPathID = 0;
const int stackNum = 200;
int trackPathIDStack[stackNum];
double odomTimeStack[stackNum];
float odomYawStack[stackNum];
int odomSendIDPointer = -1;
int odomRecIDPointer = 0;

bool manualMode = true;
bool autoAdjustMode = false;
int stateInitDelay = 100;

bool pathFound = true;
double stopRotTime = 0;
double slowTurnTime = 0;
double autoModeTime = 0;
int waypointCount = 0;
double waypointTime = 0;

double joyTime = 0;
float joyFwd = 0;
float joyFwdDb = 0;
float joyLeft = 0;
float joyUp = 0;
float joyYaw = 0;

float trackX = 0;
float trackY = 0;
float trackZ = 0;
float trackPitch = 0;
float trackYaw = 0;

float trackRecX = 0;
float trackRecY = 0;
float trackRecZ = 0;

float vehicleX = 0;
float vehicleY = 0;
float vehicleZ = 0;
float vehicleYaw = 0;

float vehicleRecX = 0;
float vehicleRecY = 0;
float vehicleRecZ = 0;

float vehicleVelX = 0;
float vehicleVelY = 0;
float vehicleVelZ = 0;

float vehicleAngRateX = 0;
float vehicleAngRateY = 0;
float vehicleAngRateZ = 0;

visualization_msgs::Marker trackMarker;
nav_msgs::Odometry trackOdom;
nav_msgs::Path trackPath, trackPath2, trackPathShow;
geometry_msgs::TwistStamped control_cmd;
std_msgs::Float32 autoMode;
geometry_msgs::PointStamped waypoint;
tf::StampedTransform odomTrans;

ros::Publisher *pubMarkerPointer;
ros::Publisher *pubOdometryPointer;
ros::Publisher *pubPathPointer;
ros::Publisher *pubControlPointer;
ros::Publisher *pubAutoModePointer;
ros::Publisher *pubWaypointPointer;
tf::TransformBroadcaster *tfBroadcasterPointer;

FILE *desiredTrajFilePtr = NULL;
FILE *executedTrajFilePtr = NULL;

void stateEstimationHandler(const nav_msgs::Odometry::ConstPtr& odom)
{
  if (stateInitDelay >= 0 && shiftGoalAtStart) {
    if (stateInitDelay == 0) {
      goalX += trackingCamScale * odom->pose.pose.position.x;
      goalY += trackingCamScale * odom->pose.pose.position.y;
      goalZ += trackingCamScale * odom->pose.pose.position.z;
    }
    stateInitDelay--;
    return;
  }

  pubSkipCount--;
  if (pubSkipCount >= 0) {
    return;
  } else {
    pubSkipCount = pubSkipNum;
  }

  double odomTime = odom->header.stamp.toSec();

  double roll, pitch, yaw;
  geometry_msgs::Quaternion geoQuat = odom->pose.pose.orientation;
  tf::Matrix3x3(tf::Quaternion(geoQuat.x, geoQuat.y, geoQuat.z, geoQuat.w)).getRPY(roll, pitch, yaw);

  vehicleX = trackingCamScale * odom->pose.pose.position.x;
  vehicleY = trackingCamScale * odom->pose.pose.position.y;
  vehicleZ = trackingCamScale * odom->pose.pose.position.z;
  vehicleVelX = trackingCamScale * odom->twist.twist.linear.x;
  vehicleVelY = trackingCamScale * odom->twist.twist.linear.y;
  vehicleVelZ = trackingCamScale * odom->twist.twist.linear.z;
  vehicleAngRateX = odom->twist.twist.angular.x;
  vehicleAngRateY = odom->twist.twist.angular.y;
  vehicleAngRateZ = odom->twist.twist.angular.z;
  vehicleYaw = yaw;

  if (trackingCamBackward) {
    roll = -roll;
    pitch = -pitch;
    vehicleX = -vehicleX;
    vehicleY = -vehicleY;
    vehicleVelX = -vehicleVelX;
    vehicleVelY = -vehicleVelY;
    vehicleAngRateX = -vehicleAngRateX;
    vehicleAngRateY = -vehicleAngRateY;
  }

  float sinRoll = sin(roll);
  float cosRoll = cos(roll);
  float sinPitch = sin(pitch);
  float cosPitch = cos(pitch);
  float sinYaw = sin(yaw);
  float cosYaw = cos(yaw);

  float pointX1 = trackingCamXOffset;
  float pointY1 = trackingCamYOffset * cosRoll - trackingCamZOffset * sinRoll;
  float pointZ1 = trackingCamYOffset * sinRoll + trackingCamZOffset * cosRoll;

  float pointX2 = pointX1 * cosPitch + pointZ1 * sinPitch;
  float pointY2 = pointY1;
  float pointZ2 = -pointX1 * sinPitch + pointZ1 * cosPitch;

  vehicleX -= pointX2 * cosYaw - pointY2 * sinYaw - trackingCamXOffset;
  vehicleY -= pointX2 * sinYaw + pointY2 * cosYaw - trackingCamYOffset;
  vehicleZ -= pointZ2 - trackingCamZOffset;

  vehicleVelX -= -trackingCamYOffset * vehicleAngRateZ + trackingCamZOffset * vehicleAngRateY;
  vehicleVelY -= -trackingCamZOffset * vehicleAngRateX + trackingCamXOffset * vehicleAngRateZ;
  vehicleVelZ -= -trackingCamXOffset * vehicleAngRateY + trackingCamYOffset * vehicleAngRateX;

  float velX1 = vehicleVelX;
  float velY1 = vehicleVelY * cosRoll - vehicleVelZ * sinRoll;
  float velZ1 = vehicleVelY * sinRoll + vehicleVelZ * cosRoll;

  vehicleVelX = velX1 * cosPitch + velZ1 * sinPitch;
  vehicleVelY = velY1;
  vehicleVelZ = -velX1 * sinPitch + velZ1 * cosPitch;

  float vehicleSpeed = sqrt(vehicleVelX * vehicleVelX + vehicleVelY * vehicleVelY);

  float disToGoalX = goalX - vehicleX;
  float disToGoalY = goalY - vehicleY;
  float disToGoal = sqrt(disToGoalX * disToGoalX + disToGoalY * disToGoalY);
  float dirToGoal = atan2(goalY - vehicleY, goalX - vehicleX) - vehicleYaw;
  if (dirToGoal > PI) dirToGoal -= 2 * PI;
  else if (dirToGoal < -PI) dirToGoal += 2 * PI;

  if (autonomyMode) {
    if (odomTime > stopRotTime + minStopRotInterval && disToGoal > stopRotDis && (fabs(dirToGoal) > stopRotYaw1 * PI / 180.0 || 
        (fabs(dirToGoal) > stopRotYaw2 * PI / 180.0 && vehicleSpeed < minSpeed / 2.0)) && !autoAdjustMode) {
      joyFwd = 0;
      joyLeft = 0;
      joyUp = 0;
      joyYaw = 0;

      stopRotTime = odomTime;
      if (vehicleSpeed < minSpeed / 2.0) stopRotTime -= stopRotDelayTime;

      autoAdjustMode = true;
    }

    if (odomTime > stopRotTime + stopRotDelayTime && autoAdjustMode) {
      if (fabs(dirToGoal) > stopRotYaw2 * PI / 180.0) {
        joyFwd = 0;
        joyLeft = 0;
        joyUp = 0;
        if (dirToGoal < 0) joyYaw = -1.0;
        else joyYaw = 1.0;
      } else {
        joyFwd = 1.0;
        joyLeft = 0;
        joyUp = 0;
        joyYaw = 0;

        autoAdjustMode = false;
      }
    }

    if (odomTime - autoModeTime > 0.0667) {
      if (autoAdjustMode) autoMode.data = -1.0;
      else autoMode.data = desiredSpeed / maxSpeed;

      pubAutoModePointer->publish(autoMode);
      autoModeTime = odomTime;
    }
  } else {
    autoAdjustMode = false;
  }

  if (manualMode || (autonomyMode && autoAdjustMode)) {
    trackX = vehicleX;
    trackY = vehicleY;
    trackZ = vehicleZ;
    trackYaw = vehicleYaw;

    float desiredRoll = -stopVelXYGain * (manualSpeedXY * joyLeft - vehicleVelY) - posXYGain * lookAheadScale * manualSpeedXY * joyLeft;
    if (desiredRoll > maxRollPitch * PI / 180.0) desiredRoll = maxRollPitch * PI / 180.0;
    else if (desiredRoll < -maxRollPitch * PI / 180.0) desiredRoll = -maxRollPitch * PI / 180.0;

    float desiredPitch = stopVelXYGain * (manualSpeedXY * joyFwd - vehicleVelX) + posXYGain * lookAheadScale * manualSpeedXY * joyFwd;
    if (desiredPitch > maxRollPitch * PI / 180.0) desiredPitch = maxRollPitch * PI / 180.0;
    else if (desiredPitch < -maxRollPitch * PI / 180.0) desiredPitch = -maxRollPitch * PI / 180.0;

    control_cmd.twist.linear.x = desiredRoll;
    control_cmd.twist.linear.y = desiredPitch;
    control_cmd.twist.linear.z = manualSpeedZ * joyUp;
    control_cmd.twist.angular.z = manualYawRate * joyYaw * PI / 180.0;
  } else {
    trackX = trackPath.poses[trackPathID].pose.position.x;
    trackY = trackPath.poses[trackPathID].pose.position.y;
    trackZ = trackPath.poses[trackPathID].pose.position.z;

    if (joyFwdDb < joyFwd) joyFwdDb = joyFwd;
    else if (joyFwdDb > joyFwd + joyDeadband) joyFwdDb = joyFwd + joyDeadband;
    if (joyFwd <= joyDeadband) joyFwdDb = 0;

    float joyFwd2 = joyFwdDb;
    if (joyFwd2 < minSpeed / maxSpeed) joyFwd2 = 0;

    float lookAheadDis = lookAheadScale * maxSpeed * joyFwd2;
    if (autonomyMode) lookAheadDis = lookAheadScale * desiredSpeed;
    if (lookAheadDis <= 0) lookAheadDis = 0;
    else if (lookAheadDis < minLookAheadDis) lookAheadDis = minLookAheadDis;

    float disX = trackX - vehicleX;
    float disY = trackY - vehicleY;
    float disZ = trackZ - vehicleZ;
    float dis = sqrt(disX * disX + disY * disY);

    int trackPathLength = trackPath.poses.size();
    while (trackPathID < trackPathLength - 1) {
      float trackNextX = trackPath.poses[trackPathID + 1].pose.position.x;
      float trackNextY = trackPath.poses[trackPathID + 1].pose.position.y;
      float trackNextZ = trackPath.poses[trackPathID + 1].pose.position.z;

      float disNextX = trackNextX - vehicleX;
      float disNextY = trackNextY - vehicleY;
      float disNext = sqrt(disNextX * disNextX + disNextY * disNextY);

      if (fabs(disNext - lookAheadDis) <= fabs(dis - lookAheadDis) || disNext <= dis) {
        trackX = trackNextX;
        trackY = trackNextY;
        trackZ = trackNextZ;

        dis = disNext;
        trackPathID++;
      } else {
        break;
      }
    }

    float curv = 0;
    float slope = 0;
    if (trackPathID > 0) {
      float deltaX = trackX - trackPath.poses[trackPathID - 1].pose.position.x;
      float deltaY = trackY - trackPath.poses[trackPathID - 1].pose.position.y;
      float deltaZ = trackZ - trackPath.poses[trackPathID - 1].pose.position.z;
      trackYaw = atan2(deltaY, deltaX);

      float deltaDis = sqrt(deltaX * deltaX + deltaY * deltaY);
      if (deltaDis > 0.001 && trackPathID < trackPathLength - 1) {
        deltaX = trackPath.poses[trackPathID + 1].pose.position.x - trackPath.poses[trackPathID].pose.position.x;
        deltaY = trackPath.poses[trackPathID + 1].pose.position.y - trackPath.poses[trackPathID].pose.position.y;
        float trackNextYaw = atan2(deltaY, deltaX);

        float deltaYaw = trackNextYaw - trackYaw;
        if (deltaYaw > PI) deltaYaw -= 2 * PI;
        else if (deltaYaw < -PI) deltaYaw += 2 * PI;

        curv = deltaYaw / deltaDis;
        slope = deltaZ / deltaDis;
      }
    }

    float dirToPath = trackYaw - vehicleYaw;
    if (dirToPath > PI) dirToPath -= 2 * PI;
    else if (dirToPath < -PI) dirToPath += 2 * PI;

    float vehicleVelPath = vehicleVelX * cos(dirToPath) + vehicleVelY * sin(dirToPath);
    float desiredSpeed2 = vehicleVelPath + smoothIncrSpeed;
    float velXYGain2 = velXYGain;
    float posXYGain2 = posXYGain;
    float yawRatePath = curv * vehicleVelPath;
    float yawBoostScale2 = yawBoostScale;
    float velZPath = slope * vehicleVelPath;
    float posZBoostScale2 = posZBoostScale;
    float slowTurnRate2 = 1.0;
    if (fabs(curv) > minSlowTurnCurv || odomTime < slowTurnTime + minSlowTurnInterval) {
      slowTurnRate2 = slowTurnRate;
      if (fabs(curv) > minSlowTurnCurv) slowTurnTime = odomTime;
    }
    if (autonomyMode) {
      if (desiredSpeed2 > slowTurnRate2 * desiredSpeed) desiredSpeed2 = slowTurnRate2 * desiredSpeed;
    } else {
      if (desiredSpeed2 > slowTurnRate2 * maxSpeed * joyFwd2) desiredSpeed2 = slowTurnRate2 * maxSpeed * joyFwd2;
    }
    if (desiredSpeed2 < minSpeed) desiredSpeed2 = minSpeed;
    if (joyFwd2 == 0 || !pathFound || (disToGoal < stopDis && autonomyMode && !autoAdjustMode)) {
      desiredSpeed2 = 0;
      velXYGain2 = stopVelXYGain;
      posXYGain2 = stopPosXYGain;
      yawRatePath = 0;
      yawBoostScale2 = 1.0;
      velZPath = 0;
      posZBoostScale2 = 1.0;
    } else if (disToGoal < slowDis && autonomyMode && !autoAdjustMode) {
      float slowSpeed = (maxSpeed * (disToGoal - stopDis) + minSpeed * (slowDis - disToGoal)) / (slowDis - stopDis);
      if (desiredSpeed2 > slowSpeed) desiredSpeed2 = slowSpeed;
    }

    if (fabs(curv) < 0.001) {
      yawRatePath = 0;
      yawBoostScale2 = 1.0;
    }

    float deltaZ = 0;
    if (trackPathID > 0) {
      deltaZ = trackZ - trackPath.poses[trackPathID - 1].pose.position.z;
    }
    if (fabs(deltaZ) < 0.001) {
      velZPath = 0;
      posZBoostScale2 = 1.0;
    }

    disX = trackX - vehicleX;
    disY = trackY - vehicleY;
    disZ = trackZ - vehicleZ;

    float disX2 = disX * cos(vehicleYaw) + disY * sin(vehicleYaw);
    float disY2 = -disX * sin(vehicleYaw) + disY * cos(vehicleYaw);
    float dis2 = sqrt(disX2 * disX2 + disY2 * disY2);

    if (dis2 > lookAheadDis + stopDis) {
      disX2 *= (lookAheadDis + stopDis) / dis2;
      disY2 *= (lookAheadDis + stopDis) / dis2;
    }

    float dirDiff = trackYaw - vehicleYaw;
    if (dirDiff > PI) dirDiff -= 2 * PI;
    else if (dirDiff < -PI) dirDiff += 2 * PI;

    float desiredRoll = 0;
    float desiredPitch = 0;
    if (dis2 > 0.001) {
      desiredRoll = -accXYGain * curv * desiredSpeed2 * desiredSpeed2 * cos(dirDiff)
                   - velXYGain2 * (desiredSpeed2 * disY2 / dis2 - vehicleVelY) - posXYGain2 * disY2;
      if (desiredRoll > maxRollPitch * PI / 180.0) desiredRoll = maxRollPitch * PI / 180.0;
      else if (desiredRoll < -maxRollPitch * PI / 180.0) desiredRoll = -maxRollPitch * PI / 180.0;

      desiredPitch = -accXYGain * curv * desiredSpeed2 * desiredSpeed2 * sin(dirDiff)
                  + velXYGain2 * (desiredSpeed2 * disX2 / dis2 - vehicleVelX) + posXYGain2 * disX2;
      if (desiredPitch > maxRollPitch * PI / 180.0) desiredPitch = maxRollPitch * PI / 180.0;
      else if (desiredPitch < -maxRollPitch * PI / 180.0) desiredPitch = -maxRollPitch * PI / 180.0;
    }

    float desiredYawRate = yawGain * dirDiff;
    if (desiredYawRate > maxRateByYaw * PI / 180.0) desiredYawRate = maxRateByYaw * PI / 180.0;
    else if (desiredYawRate < -maxRateByYaw * PI / 180.0) desiredYawRate = -maxRateByYaw * PI / 180.0;
    desiredYawRate = yawRateScale * yawRatePath + yawBoostScale2 * desiredYawRate;

    float desiredVelZ = posZGain * disZ;
    if (desiredVelZ > maxVelByPosZ) desiredVelZ = maxVelByPosZ;
    else if (desiredVelZ < -maxVelByPosZ) desiredVelZ = -maxVelByPosZ;
    desiredVelZ = velZScale * velZPath + posZBoostScale2 * desiredVelZ;

    control_cmd.twist.linear.x = desiredRoll;
    control_cmd.twist.linear.y = desiredPitch;
    control_cmd.twist.linear.z = desiredVelZ;
    control_cmd.twist.angular.z = desiredYawRate;
  }

  odomSendIDPointer = (odomSendIDPointer + 1) % stackNum;
  odomTimeStack[odomSendIDPointer] = odomTime;
  odomYawStack[odomSendIDPointer] = trackYaw;
  trackPathIDStack[odomSendIDPointer] = trackPathID;

  if (autonomyMode && waypointTest) {
    if (odomTime - waypointTime > waypointInterval) {
      float angle = 0, elev = 0;
      if (waypointCount > 0 && waypointCount < waypointNum - 1) {
        if (waypointCount % 2 == 0) {
          angle = -waypointYaw;
          elev = -waypointZ;
        } else {
          angle = waypointYaw;
          elev = waypointZ;
        }
      }

      if (waypointCount < waypointNum) {
        waypoint.header.stamp = odom->header.stamp;
        waypoint.header.frame_id = "/map";
        waypoint.point.x = 10.0 * cos(vehicleYaw + angle * PI / 180.0) + vehicleX;
        waypoint.point.y = 10.0 * sin(vehicleYaw + angle * PI / 180.0) + vehicleY;
        waypoint.point.z = vehicleZ + elev;
        pubWaypointPointer->publish(waypoint);

        waypointTime = odomTime;
        if (waypointCount == 1 || waypointCount == waypointNum - 2) waypointTime -= waypointInterval / 2.0;
        waypointCount++;
      }
    }
  }

  if (saveTrajectory) {
    float disX = trackX - trackRecX;
    float disY = trackY - trackRecY;
    float disZ = trackZ - trackRecZ;
    float dis = sqrt(disX * disX + disY * disY + disZ * disZ);

    float disX2 = vehicleX - vehicleRecX;
    float disY2 = vehicleY - vehicleRecY;
    float disZ2 = vehicleZ - vehicleRecZ;
    float dis2 = sqrt(disX2 * disX2 + disY2 * disY2 + disZ2 * disZ2);

    if (dis > saveTrajInverval && dis2 > saveTrajInverval) {
      fprintf(desiredTrajFilePtr, "%f %f %f %f %lf\n", trackX, trackY, trackZ, trackYaw, odomTime);
      fprintf(executedTrajFilePtr, "%f %f %f %f %f %f %lf\n", vehicleX, vehicleY, vehicleZ, roll, pitch, yaw, odomTime);

      trackRecX = trackX;
      trackRecY = trackY;
      trackRecZ = trackZ;

      vehicleRecX = vehicleX;
      vehicleRecY = vehicleY;
      vehicleRecZ = vehicleZ;
    }
  }

  control_cmd.header.stamp = odom->header.stamp;
  control_cmd.header.frame_id = "vehicle";
  pubControlPointer->publish(control_cmd);

  trackMarker.header.stamp = odom->header.stamp;
  trackMarker.header.frame_id = "map";
  trackMarker.ns = "track_point";
  trackMarker.id = 0;
  trackMarker.type = visualization_msgs::Marker::SPHERE;
  trackMarker.action = visualization_msgs::Marker::ADD;
  trackMarker.scale.x = 0.2;
  trackMarker.scale.y = 0.2;
  trackMarker.scale.z = 0.2;
  trackMarker.color.a = 1.0;
  trackMarker.color.r = 1.0;
  trackMarker.pose.position.x = trackX;
  trackMarker.pose.position.y = trackY;
  trackMarker.pose.position.z = trackZ;
  pubMarkerPointer->publish(trackMarker);

  geoQuat = tf::createQuaternionMsgFromRollPitchYaw(0, trackPitch, trackYaw);

  trackOdom.header.stamp = odom->header.stamp;
  trackOdom.header.frame_id = "map";
  trackOdom.child_frame_id = "track_point";
  trackOdom.pose.pose.orientation = geoQuat;
  trackOdom.pose.pose.position.x = trackX;
  trackOdom.pose.pose.position.y = trackY;
  trackOdom.pose.pose.position.z = trackZ;
  trackOdom.twist.twist.angular.x = roll;
  trackOdom.twist.twist.angular.y = pitch;
  trackOdom.twist.twist.angular.z = yaw;
  trackOdom.twist.twist.linear.x = vehicleX;
  trackOdom.twist.twist.linear.y = vehicleY;
  trackOdom.twist.twist.linear.z = vehicleZ;
  pubOdometryPointer->publish(trackOdom);

  odomTrans.stamp_ = odom->header.stamp;
  odomTrans.frame_id_ = "map";
  odomTrans.child_frame_id_ = "track_point";
  odomTrans.setRotation(tf::Quaternion(geoQuat.x, geoQuat.y, geoQuat.z, geoQuat.w));
  odomTrans.setOrigin(tf::Vector3(trackX, trackY, trackZ));
  tfBroadcasterPointer->sendTransform(odomTrans);
}

void pathHandler(const nav_msgs::Path::ConstPtr& path)
{
  double pathTime = path->header.stamp.toSec();

  int pathLength = path->poses.size();
  if (pathLength > 1) {
    pathFound = true;
  } else {
    pathLength = 1;
    pathFound = false;
  }

  if (odomSendIDPointer < 0) {
    return;
  }

  while (odomRecIDPointer != (odomSendIDPointer + 1) % stackNum) {
    int odomRecIDPointerNext = (odomRecIDPointer + 1) % stackNum;
    if (fabs(pathTime - odomTimeStack[odomRecIDPointer]) < fabs(pathTime - odomTimeStack[odomRecIDPointerNext])) {
      break;
    }
    odomRecIDPointer = (odomRecIDPointer + 1) % stackNum;
  }

  int trackPathRecID = trackPathIDStack[odomRecIDPointer];
  if (trackPathRecID < 100) {
    trackPath2 = trackPath;
    trackPath.poses.resize(trackPathRecID + pathLength);
    for (int i = 0; i <= trackPathRecID; i++) {
      trackPath.poses[i] = trackPath2.poses[i];
    }
  } else {
    trackPath2.poses.resize(101);
    for (int i = 0; i <= 100; i++) {
      trackPath2.poses[i] = trackPath.poses[trackPathRecID + i - 100];
    }
    trackPath.poses.resize(trackPathRecID + pathLength);
    for (int i = 0; i <= 100; i++) {
      trackPath.poses[trackPathRecID + i - 100] = trackPath2.poses[i];
    }
  }

  if (manualMode || (autonomyMode && autoAdjustMode)) {
    trackPath.poses[trackPathRecID].pose.position.x = trackX;
    trackPath.poses[trackPathRecID].pose.position.y = trackY;
    trackPath.poses[trackPathRecID].pose.position.z = trackZ;
    if (trackPathRecID > 0) {
      trackPath.poses[trackPathRecID - 1].pose.position.x = trackX - 0.1 * cos(trackYaw);
      trackPath.poses[trackPathRecID - 1].pose.position.y = trackY - 0.1 * sin(trackYaw);
      trackPath.poses[trackPathRecID - 1].pose.position.z = trackZ;
    }
    odomYawStack[odomRecIDPointer] = trackYaw;
  }

  trackX = trackPath.poses[trackPathRecID].pose.position.x;
  trackY = trackPath.poses[trackPathRecID].pose.position.y;
  trackZ = trackPath.poses[trackPathRecID].pose.position.z;
  trackYaw = odomYawStack[odomRecIDPointer];

  float sinTrackPitch = sin(trackPitch);
  float cosTrackPitch = cos(trackPitch);
  float sinTrackYaw = sin(trackYaw);
  float cosTrackYaw = cos(trackYaw);

  for (int i = 1; i < pathLength; i++) {
    float trackX2 = cosTrackPitch * path->poses[i].pose.position.x + sinTrackPitch * path->poses[i].pose.position.z;
    float trackY2 = path->poses[i].pose.position.y;
    float trackZ2 = -sinTrackPitch * path->poses[i].pose.position.x + cosTrackPitch * path->poses[i].pose.position.z;

    trackPath.poses[trackPathRecID + i].pose.position.x = cosTrackYaw * trackX2 - sinTrackYaw * trackY2 + trackX;
    trackPath.poses[trackPathRecID + i].pose.position.y = sinTrackYaw * trackX2 + cosTrackYaw * trackY2 + trackY;
    trackPath.poses[trackPathRecID + i].pose.position.z = trackZ2 + trackZ;
  }

  int trackPathLength = trackPath.poses.size();
  if (trackPathLength < 500) {
    trackPathShow = trackPath;
  } else {
    trackPathShow.poses.resize(500);
    for (int i = 0; i < 500; i++) {
      trackPathShow.poses[i] = trackPath.poses[trackPathLength + i - 500];
    }
  }

  trackPathShow.header.stamp = path->header.stamp;
  trackPathShow.header.frame_id = "map";
  pubPathPointer->publish(trackPathShow);
}

void joystickHandler(const sensor_msgs::Joy::ConstPtr& joy)
{
  joyTime = ros::Time::now().toSec();

  if (joy->axes[2] >= -0.1 || joy->axes[5] < -0.1) {
    joyFwd = joy->axes[4];
    if (fabs(joyFwd) < joyDeadband) joyFwd = 0;
    joyLeft = joy->axes[3];
    if (fabs(joyLeft) < joyDeadband) joyLeft = 0;
    joyUp = joy->axes[1];
    if (fabs(joyUp) < joyDeadband) joyUp = 0;
    joyYaw = joy->axes[0];
    if (fabs(joyYaw) < joyDeadband) joyYaw = 0;
  }

  if (joy->axes[5] < -0.1) {
    manualMode = true;
    autonomyMode = false;
    autoAdjustMode = false;
  } else {
    manualMode = false;

    if (joy->axes[2] < -0.1) {
      if (!autonomyMode) joyFwd = 1.0;
      autonomyMode = true;
    } else {
      autonomyMode = false;
      autoAdjustMode = false;
    }
  }

  float joySpeed = joy->axes[4];
  if (fabs(joySpeed) < joyDeadband) joySpeed = 0;

  if (desiredSpeed < maxSpeed * joySpeed) desiredSpeed = maxSpeed * joySpeed;
  else if (desiredSpeed > maxSpeed * (joySpeed + joyDeadband)) desiredSpeed = maxSpeed * (joySpeed + joyDeadband);
  if (desiredSpeed < minSpeed || joySpeed <= joyDeadband) desiredSpeed = minSpeed;
  else if (desiredSpeed > maxSpeed) desiredSpeed = maxSpeed;
}

void goalHandler(const geometry_msgs::PointStamped::ConstPtr& goal)
{
  goalX = goal->point.x;
  goalY = goal->point.y;
  goalZ = goal->point.z;
}

void speedHandler(const std_msgs::Float32::ConstPtr& speed)
{
  double speedTime = ros::Time::now().toSec();

  if (speedTime - joyTime > joyToSpeedDelay) {
    desiredSpeed = maxSpeed * speed->data;
    if (desiredSpeed < minSpeed) desiredSpeed = minSpeed;
    else if (desiredSpeed > maxSpeed) desiredSpeed = maxSpeed;
  }
}

int main(int argc, char** argv)
{
  ros::init(argc, argv, "pathFollower");
  ros::NodeHandle nh;
  ros::NodeHandle nhPrivate = ros::NodeHandle("~");

  nhPrivate.getParam("stateEstimationTopic", stateEstimationTopic);
  nhPrivate.getParam("desiredTrajFile", desiredTrajFile);
  nhPrivate.getParam("executedTrajFile", executedTrajFile);
  nhPrivate.getParam("saveTrajectory", saveTrajectory);
  nhPrivate.getParam("saveTrajInverval", saveTrajInverval);
  nhPrivate.getParam("waypointTest", waypointTest);
  nhPrivate.getParam("waypointNum", waypointNum);
  nhPrivate.getParam("waypointInterval", waypointInterval);
  nhPrivate.getParam("waypointYaw", waypointYaw);
  nhPrivate.getParam("waypointZ", waypointZ);
  nhPrivate.getParam("autonomyMode", autonomyMode);
  nhPrivate.getParam("pubSkipNum", pubSkipNum);
  nhPrivate.getParam("trackingCamBackward", trackingCamBackward);
  nhPrivate.getParam("trackingCamXOffset", trackingCamXOffset);
  nhPrivate.getParam("trackingCamYOffset", trackingCamYOffset);
  nhPrivate.getParam("trackingCamZOffset", trackingCamZOffset);
  nhPrivate.getParam("trackingCamScale", trackingCamScale);
  nhPrivate.getParam("trackPitch", trackPitch);
  nhPrivate.getParam("lookAheadScale", lookAheadScale);
  nhPrivate.getParam("minLookAheadDis", minLookAheadDis);
  nhPrivate.getParam("minSpeed", minSpeed);
  nhPrivate.getParam("maxSpeed", maxSpeed);
  nhPrivate.getParam("accXYGain", accXYGain);
  nhPrivate.getParam("velXYGain", velXYGain);
  nhPrivate.getParam("posZBoostScale", posZBoostScale);
  nhPrivate.getParam("posXYGain", posXYGain);
  nhPrivate.getParam("stopVelXYGain", stopVelXYGain);
  nhPrivate.getParam("stopPosXYGain", stopPosXYGain);
  nhPrivate.getParam("smoothIncrSpeed", smoothIncrSpeed);
  nhPrivate.getParam("maxRollPitch", maxRollPitch);
  nhPrivate.getParam("yawRateScale", yawRateScale);
  nhPrivate.getParam("yawGain", yawGain);
  nhPrivate.getParam("yawBoostScale", yawBoostScale);
  nhPrivate.getParam("maxRateByYaw", maxRateByYaw);
  nhPrivate.getParam("velZScale", velZScale);
  nhPrivate.getParam("posZGain", posZGain);
  nhPrivate.getParam("maxVelByPosZ", maxVelByPosZ);
  nhPrivate.getParam("manualSpeedXY", manualSpeedXY);
  nhPrivate.getParam("manualSpeedZ", manualSpeedZ);
  nhPrivate.getParam("manualYawRate", manualYawRate);
  nhPrivate.getParam("slowTurnRate", slowTurnRate);
  nhPrivate.getParam("minSlowTurnCurv", minSlowTurnCurv);
  nhPrivate.getParam("minSlowTurnInterval", minSlowTurnInterval);
  nhPrivate.getParam("minStopRotInterval", minStopRotInterval);
  nhPrivate.getParam("stopRotDelayTime", stopRotDelayTime);
  nhPrivate.getParam("stopRotDis", stopRotDis);
  nhPrivate.getParam("stopRotYaw1", stopRotYaw1);
  nhPrivate.getParam("stopRotYaw2", stopRotYaw2);
  nhPrivate.getParam("stopDis", stopDis);
  nhPrivate.getParam("slowDis", slowDis);
  nhPrivate.getParam("joyDeadband", joyDeadband);
  nhPrivate.getParam("joyToSpeedDelay", joyToSpeedDelay);
  nhPrivate.getParam("shiftGoalAtStart", shiftGoalAtStart);
  nhPrivate.getParam("goalX", goalX);
  nhPrivate.getParam("goalY", goalY);
  nhPrivate.getParam("goalZ", goalZ);

  desiredSpeed = minSpeed;
  if (autonomyMode) {
    manualMode = false;
    joyFwd = 1.0;
  }

  trackPath.poses.resize(1);
  trackPath.poses[0].pose.position.x = 0;
  trackPath.poses[0].pose.position.y = 0;
  trackPath.poses[0].pose.position.z = 1.0;

  if (saveTrajectory) {
    desiredTrajFilePtr = fopen(desiredTrajFile.c_str(), "w");
    executedTrajFilePtr = fopen(executedTrajFile.c_str(), "w");
  }

  ros::Subscriber subStateEstimation = nh.subscribe<nav_msgs::Odometry> (stateEstimationTopic, 5, stateEstimationHandler);

  ros::Subscriber subPath = nh.subscribe<nav_msgs::Path> ("/path", 5, pathHandler);

  ros::Subscriber subJoystick = nh.subscribe<sensor_msgs::Joy> ("/joy", 5, joystickHandler);

  ros::Subscriber subGoal = nh.subscribe<geometry_msgs::PointStamped> ("/way_point", 5, goalHandler);

  ros::Subscriber subSpeed = nh.subscribe<std_msgs::Float32> ("/speed", 5, speedHandler);

  ros::Publisher pubMarker = nh.advertise<visualization_msgs::Marker> ("/track_point_marker", 5);
  pubMarkerPointer = &pubMarker;

  ros::Publisher pubOdometry = nh.advertise<nav_msgs::Odometry> ("/track_point_odom", 5);
  pubOdometryPointer = &pubOdometry;

  ros::Publisher pubPath = nh.advertise<nav_msgs::Path> ("/track_path", 5);
  pubPathPointer = &pubPath;

  ros::Publisher pubControl = nh.advertise<geometry_msgs::TwistStamped> ("/attitude_control", 5);
  pubControlPointer = &pubControl;

  ros::Publisher pubAutoMode = nh.advertise<std_msgs::Float32> ("/auto_mode", 5);
  pubAutoModePointer = &pubAutoMode;

  ros::Publisher pubWaypoint = nh.advertise<geometry_msgs::PointStamped> ("/way_point", 5);
  pubWaypointPointer = &pubWaypoint;

  tf::TransformBroadcaster tfBroadcaster;
  tfBroadcasterPointer = &tfBroadcaster;

  ros::spin();

  if (saveTrajectory) {
    fclose(desiredTrajFilePtr);
    fclose(executedTrajFilePtr);

    printf("\nTrajectories saved.\n\n");
  }

  return 0;
}
