//////////////////////////////////////////////////////////////////////////////
//  seperate velodyne point cloud in to 16 groups based on the vertical angle to the sensor
//  for each group, use continuity to filter the obsticles and highly rough terrain.
//  for verticle sets, use the shape of all 16 points to detect obsticles. 
//  input:  velodyne_points
//  output: velodyne_points with colors
//          red: obstalces
//          greed: availiable area with roughness
//  in Feature_sets: x is the radius to robot base
//                   y is the countinuity 
//                   z is the cross section
/////////////////////////////////////////////////////////////////////////////

#include "ros/ros.h"

#include "functions/continuity_filter.h"
#include "functions/cross_section_filter.h"
#include "functions/histogram_filter.h"
#include "functions/normal_filter.h"
#include "functions/xi_functions.h"

#include <tf/transform_listener.h>


#include <pcl/sample_consensus/method_types.h>
#include <pcl/sample_consensus/model_types.h>
#include <pcl/segmentation/sac_segmentation.h>
#include <pcl/ModelCoefficients.h>​

ros::Publisher  pub_continuity, pub_cross, pub_ground;
Filter_Continuity       filter_continutiy;
Filter_Cross_Section    filter_crosssection;
Filter_Histogram        filter_histogram;
Filter_Normal           filter_normal;

tf::TransformListener* tfListener = NULL;
float sensor_height = 1.0;
int point_num_h = 720;

void publish(ros::Publisher pub, pcl::PointCloud<pcl::PointXYZRGB> cloud)
{
    sensor_msgs::PointCloud2 sensormsg_cloud_all;
    pcl::toROSMsg(cloud, sensormsg_cloud_all);

    pub.publish(sensormsg_cloud_all);
}

void seperate_velodyne_cloud(pcl::PointCloud<pcl::PointXYZRGB> cloud
                                           , pcl::PointCloud<pcl::PointXYZRGB> *velodyne_sets
                                           , Feature **feature_sets)
{
    
    float toDegree = 180/M_PI;
    
    for(int i = 0; i<cloud.points.size(); i++)
    {
        float x = cloud.points[i].x;
        float y = cloud.points[i].y;
        
        if(x < 0)
            continue;
            
        float r = sqrt(x*x + y*y);
        float h = cloud.points[i].z;
        
        if(r > 20)
            continue;
        
        double angle_v = atan2(h, r) * toDegree + 15.0;
        double angle_h = (atan2(y, x) + M_PI) * toDegree;
        
        int index_v = angle_v/2.0;
        int index_h = angle_h/0.5;
        float mod = angle_v - (index_v * 2.0);
        if(mod > 1)
            index_v += 1;
        
        // if( radius_sets[index_v].points[index_h].x != 0 )
        // {
        //     cloud.points[i].x = (cloud.points[i].x + velodyne_sets[index_v].points[index_h].x)/2;
        //     cloud.points[i].x = (cloud.points[i].y + velodyne_sets[index_v].points[index_h].y)/2;
        //     cloud.points[i].x = (cloud.points[i].z + velodyne_sets[index_v].points[index_h].z)/2;
            
        //     r = (radius_sets[index_v].points[index_h].x + r)/2;
        // }
        
        //velodyne_sets[index_v].points.push_back(cloud.points[i]);
        velodyne_sets[index_v].points[index_h] = cloud.points[i];
        feature_sets[index_v][index_h].radius = r;
        
        // if(index_v == 1)
        //     cout << "x: " << cloud.points[i].x << "  y: " << cloud.points[i].y << "  dist: " << feature_sets[index_v][index_h].radius << endl;
        //cout << "dist: " << velodyne_sets[index_v].points[index_h].r << endl;
    }
}


void seperate_velodyne_cloud(pcl::PointCloud<pcl::PointXYZRGB> cloud 
                           , pcl::PointCloud<pcl::PointXYZRGB> cloud_transformed
                           , pcl::PointCloud<pcl::PointXYZRGB> *velodyne_sets
                           , Feature **feature_sets)
{
    
    float toDegree = 180/M_PI;
    
    for(int i = 0; i<cloud.points.size(); i++)
    {
        float x = cloud.points[i].x;
        float y = cloud.points[i].y;
        
        if(x < 0)
            continue;
           
        float r = sqrt(x*x + y*y);
        float h = cloud.points[i].z;
                
        if(r > 20)
            continue;
        
        float x_trans = cloud_transformed.points[i].x;
        float y_trans = cloud_transformed.points[i].y;
        float r_trans = sqrt(x_trans*x_trans + y_trans*y_trans);
        
        double angle_v = atan2(h, r) * toDegree + 15.0;
        double angle_h = (atan2(y, x) + M_PI) * toDegree;
        
        int index_v = angle_v/2.0;
        int index_h = angle_h/0.5;
        float mod = angle_v - (index_v * 2.0);
        if(mod > 1)
            index_v += 1;
        
        velodyne_sets[index_v].points[index_h] = cloud_transformed.points[i];
        feature_sets[index_v][index_h].radius = r_trans;
    }
}

void callback_velodyne(const sensor_msgs::PointCloud2ConstPtr &cloud_in)
{
    sensor_msgs::PointCloud2 cloud_base;

    tf::StampedTransform velodyne_to_base;

    tfListener->waitForTransform("/base_link", cloud_in->header.frame_id, ros::Time::now(), ros::Duration(0.0));
    tfListener->lookupTransform("/base_link", cloud_in->header.frame_id, ros::Time(0), velodyne_to_base);

    Eigen::Matrix4f eigen_transform;
    pcl_ros::transformAsMatrix (velodyne_to_base, eigen_transform);
    pcl_ros::transformPointCloud (eigen_transform, *cloud_in, cloud_base);

    cloud_base.header.frame_id = "base_link";

    // initialize velodyne group space
    pcl::PointCloud<pcl::PointXYZRGB> velodyne_sets[16];
    Feature **feature_sets = new Feature*[16];
    //pcl::PointCloud<pcl::PointXYZRGB> feature_sets[16];
    for(int i = 0; i<16; i++)
    {
        velodyne_sets[i].points.resize(720);
        feature_sets[i] = new Feature[720];
        //feature_sets[i].points.resize(720);
    }
    
    for(int i = 0; i< 16; i++)
    for(int j = 0; j < 720; j++)
    {
        feature_sets[i][j].radius = 0;
        feature_sets[i][j].continuity_prob = 0;
        feature_sets[i][j].cross_section_prob = 0;
        feature_sets[i][j].sum = 0;
    }
    
    pcl::PointCloud<pcl::PointXYZ> pcl_cloud, pcl_cloud_base;
    pcl::fromROSMsg(*cloud_in, pcl_cloud);
    
    pcl::PointCloud<pcl::PointXYZRGB> cloud, cloud_base_rgb, cloud_inlier;
    copyPointCloud(pcl_cloud, cloud); 
    
    pcl::fromROSMsg(cloud_base, pcl_cloud_base);
    copyPointCloud(pcl_cloud_base, cloud_base_rgb); 
    cloud_base_rgb.header.frame_id = "base_link";
    // publish(pub_continuity, cloud_base_rgb);
     
    //seperate_velodyne_cloud(cloud, velodyne_sets, feature_sets); 
    seperate_velodyne_cloud(cloud, cloud_base_rgb, velodyne_sets, feature_sets); 
     
    pcl::PointCloud<pcl::PointXYZRGB> result;
    // pcl::PointCloud<pcl::PointXYZRGB> result_cross;
    
    // ///////////////////////////////// Ransac ///////////////////////////////////
    // pcl::PointCloud<pcl::PointXYZRGB>::Ptr  input_cloud       (new pcl::PointCloud<pcl::PointXYZRGB>(cloud_base_rgb));
    // pcl::PointCloud<pcl::PointXYZRGB>  temp_inlier;
    // pcl::ModelCoefficients::Ptr coefficients (new pcl::ModelCoefficients);
    // pcl::PointIndices::Ptr inliers (new pcl::PointIndices);
    // // Create the segmentation object
    // pcl::SACSegmentation<pcl::PointXYZRGB> seg;
    // // Optional
    // seg.setOptimizeCoefficients (true);
    // // Mandatory
    // seg.setModelType (pcl::SACMODEL_PLANE);
    // seg.setMethodType (pcl::SAC_RANSAC);
    // seg.setDistanceThreshold (0.5);
    
    // seg.setInputCloud (input_cloud);
    // seg.segment (*inliers, *coefficients);
    
    // copyPointCloud(cloud_base_rgb, *inliers, temp_inlier);
    // cout << coefficients->values[0] << " " << coefficients->values[1] << " " << coefficients->values[2] << " " << coefficients->values[3] << endl;

    // temp_inlier.header.frame_id =  "base_link";
    // //result = filter_histogram.get_hist_from_cloud(temp_inlier);
    
    
    // copyPointCloud(cloud, *inliers, cloud_inlier);
    // seperate_velodyne_cloud(cloud_inlier, temp_inlier, velodyne_sets, feature_sets); 
    
    //////////////////////////////////////////////////////////////////////////////////////////////////
    feature_sets = filter_continutiy.filtering_all_sets(velodyne_sets, feature_sets);
    // feature_sets = filter_crosssection.filtering_all_sets(velodyne_sets, feature_sets);
    //feature_sets = filter_normal.filtering_all_sets(velodyne_sets, feature_sets);
    
    result       = filter_continutiy.color_all_sets(velodyne_sets, feature_sets);
    // result_cross = filter_crosssection.color_all_sets(velodyne_sets, feature_sets);
    //result          = filter_normal.color_all_sets(velodyne_sets, feature_sets);
    
    //feature_sets = filter_histogram.filtering_all_sets(velodyne_sets, feature_sets);
    
    //cout << filter_continutiy.max_continuity << endl;
    //////////////////////////////////////////////////////////////////////////////////////////////////
    
    //  if(coefficients->values[2] > 0.97)
    result.header.frame_id =  "base_link";
    publish(pub_ground, result);
    //publish(pub_cross, result_cross);
  
  
    //cout <<"pbulished   " << velodyne_sets[0].points.size() << endl << ros::Time::now() << endl;
}

int main(int argc, char** argv){
    ros::init(argc, argv, "pointshape_based_processor");

    ros::NodeHandle node;
    ros::Rate rate(10.0);

    ros::Subscriber sub_assenbled = node.subscribe<sensor_msgs::PointCloud2>("/velodyne_points_left", 10, callback_velodyne);
    pub_continuity = node.advertise<sensor_msgs::PointCloud2>("/continuity_filtered", 1);
    pub_cross      = node.advertise<sensor_msgs::PointCloud2>("/cross_section_filtered", 1);
    pub_ground     = node.advertise<sensor_msgs::PointCloud2>("/velodyne_points_ground", 1);

    // ros::Subscriber sub_odom = node.subscribe<geometry_msgs::PoseStamped>("/slam_out_pose", 1, callback_odom);
    // ros::Subscriber sub_odom_icp = node.subscribe<nav_msgs::Odometry >("/icp_odom", 1, callback_odom_icp);


    tfListener = new (tf::TransformListener);

    while (node.ok())
    {

        ros::spinOnce();
        rate.sleep();
    }
    return 0;
};
