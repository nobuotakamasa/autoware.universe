// Copyright 2021 Tier IV, Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef SCAN_GROUND_FILTER__NODE_HPP_
#define SCAN_GROUND_FILTER__NODE_HPP_

#include "autoware/pointcloud_preprocessor/filter.hpp"
#include "autoware/pointcloud_preprocessor/transform_info.hpp"
#include "autoware_vehicle_info_utils/vehicle_info.hpp"

#include <sensor_msgs/msg/point_cloud2.hpp>

#include <pcl/filters/extract_indices.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl_conversions/pcl_conversions.h>
#include <tf2/transform_datatypes.h>

#ifdef ROS_DISTRO_GALACTIC
#include <tf2_eigen/tf2_eigen.h>
#else
#include <tf2_eigen/tf2_eigen.hpp>
#endif

#include <tf2_ros/transform_listener.h>

#include <memory>
#include <string>
#include <vector>

class ScanGroundFilterTest;

namespace autoware::ground_segmentation
{
using autoware::vehicle_info_utils::VehicleInfo;

class ScanGroundFilterComponent : public autoware::pointcloud_preprocessor::Filter
{
private:
  // classified point label
  // (0: not classified, 1: ground, 2: not ground, 3: follow previous point,
  //  4: unkown(currently not used), 5: virtual ground)
  enum class PointLabel : uint16_t {
    INIT = 0,
    GROUND,
    NON_GROUND,
    POINT_FOLLOW,
    UNKNOWN,
    VIRTUAL_GROUND,
    OUT_OF_RANGE
  };

  struct PointData
  {
    float radius;  // cylindrical coords on XY Plane
    PointLabel point_state{PointLabel::INIT};
    uint16_t grid_id;   // id of grid in vertical
    size_t orig_index;  // index of this point in the source pointcloud
  };
  using PointCloudVector = std::vector<PointData>;

  struct GridCenter
  {
    float radius;
    float avg_height;
    float max_height;
    uint16_t grid_id;
  };

  struct PointsCentroid
  {
    float radius_sum;
    float height_sum;
    float radius_avg;
    float height_avg;
    float height_max;
    float height_min;
    uint32_t point_num;
    uint16_t grid_id;
    pcl::PointIndices pcl_indices;
    std::vector<float> height_list;

    PointsCentroid()
    : radius_sum(0.0f), height_sum(0.0f), radius_avg(0.0f), height_avg(0.0f), point_num(0)
    {
    }

    void initialize()
    {
      radius_sum = 0.0f;
      height_sum = 0.0f;
      radius_avg = 0.0f;
      height_avg = 0.0f;
      height_max = 0.0f;
      height_min = 10.0f;
      point_num = 0;
      grid_id = 0;
      pcl_indices.indices.clear();
      height_list.clear();
    }

    void addPoint(const float radius, const float height)
    {
      radius_sum += radius;
      height_sum += height;
      ++point_num;
      radius_avg = radius_sum / point_num;
      height_avg = height_sum / point_num;
      height_max = height_max < height ? height : height_max;
      height_min = height_min > height ? height : height_min;
    }
    void addPoint(const float radius, const float height, const uint index)
    {
      pcl_indices.indices.push_back(index);
      height_list.push_back(height);
      addPoint(radius, height);
    }

    float getAverageSlope() { return std::atan2(height_avg, radius_avg); }

    float getAverageHeight() { return height_avg; }

    float getAverageRadius() { return radius_avg; }

    float getMaxHeight() { return height_max; }

    float getMinHeight() { return height_min; }

    uint16_t getGridId() { return grid_id; }

    pcl::PointIndices getIndices() { return pcl_indices; }
    std::vector<float> getHeightList() { return height_list; }

    pcl::PointIndices & getIndicesRef() { return pcl_indices; }
    std::vector<float> & getHeightListRef() { return height_list; }
  };

  void filter(
    const PointCloud2ConstPtr & input, const IndicesPtr & indices, PointCloud2 & output) override;

  // TODO(taisa1): Temporary Implementation: Remove this interface when all the filter nodes
  // conform to new API
  virtual void faster_filter(
    const PointCloud2ConstPtr & input, const IndicesPtr & indices, PointCloud2 & output,
    const autoware::pointcloud_preprocessor::TransformInfo & transform_info);

  tf2_ros::Buffer tf_buffer_{get_clock()};
  tf2_ros::TransformListener tf_listener_{tf_buffer_};

  int x_offset_;
  int y_offset_;
  int z_offset_;
  int intensity_offset_;
  int intensity_type_;
  bool offset_initialized_;

  void set_field_offsets(const PointCloud2ConstPtr & input);

  void get_point_from_global_offset(
    const PointCloud2ConstPtr & input, pcl::PointXYZ & point, size_t global_offset);

  const uint16_t gnd_grid_continual_thresh_ = 3;
  bool elevation_grid_mode_;
  float non_ground_height_threshold_;
  float grid_size_rad_;
  float tan_grid_size_rad_;
  float grid_size_m_;
  float low_priority_region_x_;
  uint16_t gnd_grid_buffer_size_;
  float grid_mode_switch_grid_id_;
  float grid_mode_switch_angle_rad_;
  float virtual_lidar_z_;
  float detection_range_z_max_;
  float center_pcl_shift_;             // virtual center of pcl to center mass
  float grid_mode_switch_radius_;      // non linear grid size switching distance
  double global_slope_max_angle_rad_;  // radians
  double local_slope_max_angle_rad_;   // radians
  double global_slope_max_ratio_;
  double local_slope_max_ratio_;
  double radial_divider_angle_rad_;         // distance in rads between dividers
  double split_points_distance_tolerance_;  // distance in meters between concentric divisions
  double split_points_distance_tolerance_square_;
  double                     // minimum height threshold regardless the slope,
    split_height_distance_;  // useful for close points
  bool use_virtual_ground_point_;
  bool use_recheck_ground_cluster_;  // to enable recheck ground cluster
  bool use_lowest_point_;  // to select lowest point for reference in recheck ground cluster,
                           // otherwise select middle point
  size_t radial_dividers_num_;
  VehicleInfo vehicle_info_;

  /*!
   * Output transformed PointCloud from in_cloud_ptr->header.frame_id to in_target_frame
   * @param[in] in_target_frame Coordinate system to perform transform
   * @param[in] in_cloud_ptr PointCloud to perform transform
   * @param[out] out_cloud_ptr Resulting transformed PointCloud
   * @retval true transform succeeded
   * @retval false transform failed
   */

  /*!
   * Convert sensor_msgs::msg::PointCloud2 to sorted PointCloudVector
   * @param[in] in_cloud Input Point Cloud to be organized in radial segments
   * @param[out] out_radial_ordered_points Vector of Points Clouds,
   *     each element will contain the points ordered
   */
  void convertPointcloud(
    const PointCloud2ConstPtr & in_cloud,
    std::vector<PointCloudVector> & out_radial_ordered_points);
  void convertPointcloudGridScan(
    const PointCloud2ConstPtr & in_cloud,
    std::vector<PointCloudVector> & out_radial_ordered_points);
  /*!
   * Output ground center of front wheels as the virtual ground point
   * @param[out] point Virtual ground origin point
   */
  void calcVirtualGroundOrigin(pcl::PointXYZ & point);

  float calcGridSize(const PointData & p);

  /*!
   * Classifies Points in the PointCloud as Ground and Not Ground
   * @param in_radial_ordered_clouds Vector of an Ordered PointsCloud
   *     ordered by radial distance from the origin
   * @param out_no_ground_indices Returns the indices of the points
   *     classified as not ground in the original PointCloud
   */

  void initializeFirstGndGrids(
    const float h, const float r, const uint16_t id, std::vector<GridCenter> & gnd_grids);

  void checkContinuousGndGrid(
    PointData & p, const pcl::PointXYZ & p_orig_point,
    const std::vector<GridCenter> & gnd_grids_list);
  void checkDiscontinuousGndGrid(
    PointData & p, const pcl::PointXYZ & p_orig_point,
    const std::vector<GridCenter> & gnd_grids_list);
  void checkBreakGndGrid(
    PointData & p, const pcl::PointXYZ & p_orig_point,
    const std::vector<GridCenter> & gnd_grids_list);
  void classifyPointCloud(
    const PointCloud2ConstPtr & in_cloud, std::vector<PointCloudVector> & in_radial_ordered_clouds,
    pcl::PointIndices & out_no_ground_indices);
  void classifyPointCloudGridScan(
    const PointCloud2ConstPtr & in_cloud, std::vector<PointCloudVector> & in_radial_ordered_clouds,
    pcl::PointIndices & out_no_ground_indices);
  /*!
   * Re-classifies point of ground cluster based on their height
   * @param gnd_cluster Input ground cluster for re-checking
   * @param non_ground_threshold Height threshold for ground and non-ground points classification
   * @param non_ground_indices Output non-ground PointCloud indices
   */
  void recheckGroundCluster(
    PointsCentroid & gnd_cluster, const float non_ground_threshold, const bool use_lowest_point,
    pcl::PointIndices & non_ground_indices);
  /*!
   * Returns the resulting complementary PointCloud, one with the points kept
   * and the other removed as indicated in the indices
   * @param in_cloud_ptr Input PointCloud to which the extraction will be performed
   * @param in_indices Indices of the points to be both removed and kept
   * @param out_object_cloud Resulting PointCloud with the indices kept
   */
  void extractObjectPoints(
    const PointCloud2ConstPtr & in_cloud_ptr, const pcl::PointIndices & in_indices,
    PointCloud2 & out_object_cloud);

  /** \brief Parameter service callback result : needed to be hold */
  OnSetParametersCallbackHandle::SharedPtr set_param_res_;

  /** \brief Parameter service callback */
  rcl_interfaces::msg::SetParametersResult onParameter(const std::vector<rclcpp::Parameter> & p);

  // debugger
  std::unique_ptr<autoware::universe_utils::StopWatch<std::chrono::milliseconds>> stop_watch_ptr_{
    nullptr};
  std::unique_ptr<autoware::universe_utils::DebugPublisher> debug_publisher_ptr_{nullptr};

public:
  EIGEN_MAKE_ALIGNED_OPERATOR_NEW
  explicit ScanGroundFilterComponent(const rclcpp::NodeOptions & options);

  // for test
  friend ScanGroundFilterTest;
};
}  // namespace autoware::ground_segmentation

#endif  // SCAN_GROUND_FILTER__NODE_HPP_
