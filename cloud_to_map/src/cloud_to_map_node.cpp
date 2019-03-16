#include <iostream>
#include <fstream>
#include <stdint.h>
#include <math.h>
#include <ros/ros.h>
#include <pcl_ros/point_cloud.h>
#include <nav_msgs/OccupancyGrid.h>
#include <pcl_conversions/pcl_conversions.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/common/common_headers.h>
#include <pcl/io/pcd_io.h>
#include <pcl/point_types.h>
#include <pcl/features/normal_3d.h>
#include <pcl/console/parse.h>
#include <dynamic_reconfigure/server.h>
#include <cloud_to_map/cloud_to_map_nodeConfig.h>  //autogenerated for the cloud_to_map package by the dynamic_reconfigure package.

/* Define the two point cloud types used in this code */
typedef pcl::PointCloud<pcl::PointXYZRGB> PointCloud;
typedef pcl::PointCloud<pcl::Normal> NormalCloud;

/* Global */
PointCloud::ConstPtr currentPC;
bool newPointCloud = false;
bool reconfig = false;

// -----------------------------------------------
// -----Define a structure to hold parameters-----
// -----------------------------------------------
struct Param 
{
  std::string frame;
  double searchRadius;
  double deviation;
  int buffer;
  double loopRate;
  double cellResolution;
};

// -----------------------------------
// -----Define default parameters-----
//If the value cannot be retrieved from the server, default_val is used instead.
// -----------------------------------
Param param;
boost::mutex mutex;  //多线程
void loadDefaults(ros::NodeHandle& nh) 
{
  nh.param<std::string>("frame", param.frame, "world");
  nh.param("search_radius", param.searchRadius, 0.05);
  nh.param("deviation", param.deviation, 0.78539816339);
  nh.param("buffer", param.buffer, 5);
  nh.param("loop_rate", param.loopRate, 10.0);
  nh.param("cell_resolution", param.cellResolution, 0.05);
}


// ------------------------------------------
// -----Callback for Dynamic Reconfigure-----调用动态参数配置
// ------------------------------------------
void callbackReconfig(cloud_to_map::cloud_to_map_nodeConfig &config, uint32_t level) 
{
  ROS_INFO("\n\nReconfigure Request: \nframe:%s \ndeviation:%f \nloop_rate:%f \ncell_resolution:%f \nsearch_radius:%f", 
                  config.frame.c_str(), config.deviation,config.loop_rate,config.cell_resolution, config.search_radius);
  boost::unique_lock<boost::mutex>(mutex);
  param.frame = config.frame.c_str();
  param.searchRadius = config.search_radius;
  param.deviation = config.deviation;
  param.buffer = config.buffer;
  param.loopRate = config.loop_rate;
  param.cellResolution = config.cell_resolution;
  reconfig = true;
}

// ------------------------------------------------------
// -----Update current PointCloud if msg is received-----
// ------------------------------------------------------
void callback(const PointCloud::ConstPtr& msg) 
{
  boost::unique_lock<boost::mutex>(mutex); //unique_lock是write lock。被锁后不允许其他线程执行被shared_lock或unique_lock的代码。
                                           //在写操作时，一般用这个，可以同时限制unique_lock的写和share_lock的读。
  //ROS_INFO("callback is finshed \n");
  currentPC = msg;
  newPointCloud = true;
}

// ----------------------------------------------------------------
// -----Calculate surface normals with a search radius of 0.05-----
// ----------------------------------------------------------------
void calcSurfaceNormals(PointCloud::ConstPtr& cloud, pcl::PointCloud<pcl::Normal>::Ptr normals) 
{
  pcl::NormalEstimation<pcl::PointXYZRGB, pcl::Normal> ne;
  ne.setInputCloud(cloud);
  pcl::search::KdTree<pcl::PointXYZRGB>::Ptr tree(new pcl::search::KdTree<pcl::PointXYZRGB>());
  ne.setSearchMethod(tree);
  ne.setRadiusSearch(param.searchRadius);
  ne.compute(*normals);
}

// ---------------------------------------
// -----Initialize Occupancy Grid Msg-----
// ---------------------------------------
void initGrid(nav_msgs::OccupancyGridPtr grid) 
{
  grid->header.seq = 1;
  grid->header.frame_id = param.frame;  //参考frame
  grid->info.origin.position.z = 0;
  grid->info.origin.orientation.w = 1;
  grid->info.origin.orientation.x = 0;
  grid->info.origin.orientation.y = 0;
  grid->info.origin.orientation.z = 0;
  std::cout<<"initGrid finshed!\n"<< std::endl;
/*  nav_msgs/OccupancyGrid
# This represents a 2-D grid map, in which each cell represents the probability of occupancy.
Header header 

#MetaData for the map
MapMetaData info

# The map data, in row-major order, starting with (0,0).  Occupancy
# probabilities are in the range [0,100].  Unknown is -1.
int8[] data

Header header
    uint32 seq
    time stamp
    string frame_id
MapMetaData info
    time map_load_time
    float32 resolution
    uint32 width
    uint32 height
    geometry_msgs/Pose origin
        geometry_msgs/Point position
            float64 x
            float64 y
            float64 z
        geometry_msgs/Quaternion orientation
            float64 x
            float64 y
            float64 z
            float64 w
int8[] data  */
}

// ------------------------------------------
// -----Calculate size of Occupancy Grid-----
// ------------------------------------------
void calcSize(double *xMax, double *yMax, double *xMin, double *yMin) 
{
  for (size_t i = 0; i < currentPC->size(); i++) 
  {
    double x = currentPC->points[i].x;
    double y = currentPC->points[i].y;
    if (*xMax < x) {
      *xMax = x;
    }
    if (*xMin > x) {
      *xMin = x;
    }
    if (*yMax < y) {
      *yMax = y;
    }
    if (*yMin > y) {
      *yMin = y;
    }
  }
}

// ---------------------------------------
// -----Populate 填充 map with cost values-----
// ---------------------------------------
void populateMap(NormalCloud::Ptr cloud_normals, std::vector<int> &countGrid, double xMin, double yMin,
                                                      double cellResolution, int xCells, int yCells) 
{
  double deviation = param.deviation;

  for (size_t i = 0; i < currentPC->size(); i++) //遍历每个点
  {
    double x = currentPC->points[i].x;
    double y = currentPC->points[i].y;
    double z = cloud_normals->points[i].normal_z;

    int xCell, yCell;
  //TODO implement cutoff height
    xCell = (int) ((x - xMin) / cellResolution); //取整后默认按照cellResolution将点分配到ｃｅｌｌ
    yCell = (int) ((y - yMin) / cellResolution);
    if ((yCell * xCells + xCell) > (xCells * yCells)) 
      std::cout << "x: " << x << ", y: " << y << ", xCell: " << xCell << ", yCell: " << yCell<< "\n";

    double normal_value = acos(fabs(z));//值域　０－－ｐｈｉ   地面fabs(z)应该是１　acos是０，最小值
    if (normal_value > deviation)       //根据acos函数的递减性质，非地面点的值应该都比地面点大。可以设置deviation值，决定障碍物点的阈值
      countGrid[yCell * xCells + xCell]++; //统计一个ｃｅｌｌ中垂直方向满足条件的点数
    //else 
      //countGrid[yCell * xCells + xCell]--; 
  }
}

// ---------------------------------
// -----Generate Occupancy Grid-----
// ---------------------------------
void genOccupancyGrid(std::vector<signed char> &ocGrid, std::vector<int> &countGrid, int size) 
{
  int buf = param.buffer;
  for (int i = 0; i < size; i++)  //size:xCells * yCells
  {
 if (countGrid[i] < buf && countGrid[i]>0) 
      ocGrid[i] = 0;
    else if (countGrid[i] > buf) 
      ocGrid[i] = 100;
    else if (countGrid[i] == 0) 
      ocGrid[i] = 0; // TODO Should be -1      
  }
  //std::cout<<" genOccupancyGrid finshed!"<< std::endl;

}
// -----------------------------------
// -----Update Occupancy Grid Msg-----
// -----------------------------------
void updateGrid(nav_msgs::OccupancyGridPtr grid, double cellRes, int xCells, int yCells,
                               double originX, double originY, std::vector<signed char> *ocGrid) 
{
  grid->header.seq++;
  grid->header.stamp.sec = ros::Time::now().sec;
  grid->header.stamp.nsec = ros::Time::now().nsec;
  grid->info.map_load_time = ros::Time::now();
  grid->info.resolution = cellRes;
  grid->info.width = xCells;
  grid->info.height = yCells;
  grid->info.origin.position.x = originX;  //minx
  grid->info.origin.position.y = originY;  //miny
  grid->data = *ocGrid;
  std::cout<<"updateGrid finshed!!"<< std::endl;

}

int main(int argc, char** argv) 
{
  /* Initialize ROS */
  ros::init(argc, argv, "cloud_to_map_node");
  ros::NodeHandle nh;
  ros::Subscriber sub = nh.subscribe<PointCloud>("/passthrough", 1, callback); //订阅的是点云，不是消息？？？？？
  //ros::Subscriber subLaserCloud=nh.subscribe<sensor_msgs::PointCloud2>("/velodyne_points",2,laserCLoudHandler); 这才是熟悉的
  ros::Publisher pub = nh.advertise<nav_msgs::OccupancyGrid>("/map", 1);

  /* Initialize Dynamic Reconfigure 　ＲＯＳ动态参数配置*/
  dynamic_reconfigure::Server<cloud_to_map::cloud_to_map_nodeConfig> server;
  dynamic_reconfigure::Server<cloud_to_map::cloud_to_map_nodeConfig>::CallbackType f;
  f = boost::bind(&callbackReconfig, _1, _2);
  //std::cout<<"*************************\n";  //test
  server.setCallback(f);

  /* Initialize Grid */
  nav_msgs::OccupancyGridPtr grid(new nav_msgs::OccupancyGrid);
  initGrid(grid);
  /* Finish initializing ROS */
  mutex.lock();
  ros::Rate loop_rate(param.loopRate);
  mutex.unlock();

  /* Wait for first point cloud */
  while(ros::ok() && newPointCloud == false)
  {
    ros::spinOnce();//ROS消息回调处理函数。它俩通常会出现在ROS的主循环中，程序需要不断调用ros::spin() 或 ros::spinOnce()，
                     //两者区别在于前者调用后不会再返回，也就是你的主程序到这儿就不往下执行了，而后者在调用后还可以继续执行之后的程序。
                     //所接到的消息并不是立刻就被处理，而是必须要等到ros::spin()或ros::spinOnce()执行的时候才被调用，
    loop_rate.sleep();
  }

  /* Begin processing point clouds */
  while (ros::ok()) 
  {
    ros::spinOnce();//用后还可以继续执行之后的程序。
    boost::unique_lock<boost::mutex> lck(mutex);
    if (newPointCloud || reconfig) 
    {
      /* Update configuration status */
      reconfig = false;  //调用动态参数配置时　＝true
      newPointCloud = false;  //在回调函数=true

      /* Record start time */
      uint32_t sec = ros::Time::now().sec; //秒
      uint32_t nsec = ros::Time::now().nsec;//纳秒

      /* Calculate Surface Normals */
      NormalCloud::Ptr cloud_normals(new NormalCloud);
      calcSurfaceNormals(currentPC, cloud_normals);

      /* Figure out size of matrix needed to store data. */
      double xMax = 0, yMax = 0, xMin = 0, yMin = 0;
      calcSize(&xMax, &yMax, &xMin, &yMin);  //Calculate size of Occupancy Grid
      //std::cout <<"size of matrix needed to store data:"<<"xMax: " << xMax << ", yMax: " << yMax << ", xMin: " << xMin << ", yMin: "<< yMin << "\n";

      /* Determine resolution of grid (m/cell) */
      double cellResolution = param.cellResolution;
      int xCells = ((int) ((xMax - xMin) / cellResolution)) + 1;
      int yCells = ((int) ((yMax - yMin) / cellResolution)) + 1;
      std::cout <<"the number of cells: " <<xCells*yCells<<"\n";

      /* Populate Map */
      std::vector<int> countGrid(yCells * xCells);
      populateMap(cloud_normals, countGrid, xMin, yMin, cellResolution, xCells, yCells);

      /* Generate OccupancyGrid Data Vector */
      std::vector<signed char> ocGrid(yCells * xCells);  //存储每个ｃｅｌｌ的值　　０或者１００
      genOccupancyGrid(ocGrid, countGrid, xCells * yCells);

      /* Update OccupancyGrid Object */
      updateGrid(grid, cellResolution, xCells, yCells, xMin, yMin, &ocGrid);

      /* Release lock */
      lck.unlock();

      /* Record end time */
      uint32_t esec = ros::Time::now().sec;
      uint32_t ensec = ros::Time::now().nsec;
      std::cout << "Seconds: " << esec - sec << "  NSeconds: " << ensec - nsec << "\n\n";

      /* Publish Occupancy Grid */
      pub.publish(grid);
    }
    loop_rate.sleep();
  }
}
