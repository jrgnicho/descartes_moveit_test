#include <ros/ros.h>
#include <gtest/gtest.h>
#include <pluginlib/class_loader.h>

// MoveIt!
#include <moveit/kinematics_base/kinematics_base.h>
#include <moveit/robot_model/robot_model.h>
#include <moveit/robot_state/robot_state.h>
#include <moveit/rdf_loader/rdf_loader.h>
#include <urdf/model.h>
#include <srdfdom/model.h>

#define IK_NEAR 1e-4
#define IK_NEAR_TRANSLATE 1e-5

typedef pluginlib::ClassLoader<kinematics::KinematicsBase> KinematicsLoader;

const std::string PLUGIN_NAME_PARAM = "ik_plugin_name";
const std::string GROUP_PARAM  = "group";
const std::string TIP_LINK_PARAM = "tip_link";
const std::string ROOT_LINK_PARAM = "root_link";
const std::string ROBOT_DESCRIPTION_PARAM = "robot_description";
const std::string JOINT_NAMES_PARAM = "joint_names";
const unsigned int NUM_FK_TEST = 100;
const unsigned int NUM_FK_TEST_CB = 100;
const unsigned int NUM_IK_TEST = 100;
const double DEFAULT_SEARCH_DISCRETIZATION = 0.01f;

class IKFastTest
{
public:

  bool initialize()
  {
    ros::NodeHandle ph("~");
    std::string plugin_name;

    // loading plugin
    kinematics_loader_.reset(new KinematicsLoader("moveit_core", "kinematics::KinematicsBase"));
    if(ph.getParam(PLUGIN_NAME_PARAM,plugin_name))
    {
      try
      {
        ROS_INFO_STREAM("Loading "<<plugin_name);
        kinematics_solver_ = kinematics_loader_->createInstance(plugin_name);
      }
      catch(pluginlib::PluginlibException& e)
      {
        ROS_ERROR_STREAM("Plugin failed to load: "<<e.what());
        EXPECT_TRUE(false);
        return false;
      }

    }
    else
    {
      ROS_ERROR_STREAM("The plugin "<<plugin_name<" was not found");
      EXPECT_TRUE(false);
      return false;
    }

    // initializing plugin
    if(ph.getParam(GROUP_PARAM,group_name_) && ph.getParam(TIP_LINK_PARAM,tip_link_) &&
        ph.getParam(ROOT_LINK_PARAM,root_link_) && ph.getParam(JOINT_NAMES_PARAM,joints_))
    {
      if(kinematics_solver_->initialize(ROBOT_DESCRIPTION_PARAM,group_name_,root_link_,tip_link_,DEFAULT_SEARCH_DISCRETIZATION))
      {
        ROS_INFO_STREAM("Kinematics Solver plugin initialzed");
      }
      else
      {
        ROS_ERROR_STREAM("Kinematics Solver failed to initialize");
        EXPECT_TRUE(false);
        return false;
      }
    }
    else
    {
      ROS_ERROR_STREAM("Kinematics Solver parameters failed to load");
      EXPECT_TRUE(false);
      return false;
    }

    return true;
  }

  void searchIKCallback(const geometry_msgs::Pose &ik_pose,
                            const std::vector<double> &joint_state,
                            moveit_msgs::MoveItErrorCodes &error_code)
  {
    std::vector<std::string> link_names;
    link_names.push_back(tip_link_);
    std::vector<geometry_msgs::Pose> solutions;
    solutions.resize(1);
    if(!kinematics_solver_->getPositionFK(link_names,joint_state,solutions))
    {
      error_code.val = error_code.PLANNING_FAILED;
      return;
    }

    EXPECT_GT(solutions[0].position.z,0.0f);
    if(solutions[0].position.z > 0.0)
      error_code.val = error_code.SUCCESS;
    else
      error_code.val = error_code.PLANNING_FAILED;
  };


public:

  kinematics::KinematicsBasePtr kinematics_solver_;
  boost::shared_ptr<KinematicsLoader> kinematics_loader_;
  std::string root_link_;
  std::string tip_link_;
  std::string group_name_;
  std::vector<std::string> joints_;
};

IKFastTest ikfast_test;

TEST(IKFastPlugin, initialize)
{
  EXPECT_TRUE(ikfast_test.initialize());

  // Test getting chain information
  EXPECT_TRUE(ikfast_test.root_link_ == ikfast_test.kinematics_solver_->getBaseFrame());
  EXPECT_TRUE(ikfast_test.tip_link_ == ikfast_test.kinematics_solver_->getTipFrame());
  std::vector<std::string> joint_names = ikfast_test.kinematics_solver_->getJointNames();
  EXPECT_EQ((int)joint_names.size(), ikfast_test.joints_.size());

  for(unsigned int i = 0;i < joint_names.size();i++)
  {
    if(joint_names[i]!=ikfast_test.joints_[i])
    {
      ROS_ERROR_STREAM("Joint names differ");
      EXPECT_TRUE(false);
      break;
    }
  }
}


TEST(IKFastPlugin, getFK)
{
  // loading robot model
  rdf_loader::RDFLoader rdf_loader(ROBOT_DESCRIPTION_PARAM);
  robot_model::RobotModelPtr kinematic_model;
  const boost::shared_ptr<srdf::Model> &srdf = rdf_loader.getSRDF();
  const boost::shared_ptr<urdf::ModelInterface>& urdf_model = rdf_loader.getURDF();
  kinematic_model.reset(new robot_model::RobotModel(urdf_model, srdf));

  // fk solution setup
  robot_model::JointModelGroup* joint_model_group = kinematic_model->getJointModelGroup(ikfast_test.kinematics_solver_->getGroupName());
  std::vector<double> seed, fk_values, solution;
  moveit_msgs::MoveItErrorCodes error_code;
  solution.resize(ikfast_test.kinematics_solver_->getJointNames().size(), 0.0);
  std::vector<std::string> fk_names;
  fk_names.push_back(ikfast_test.kinematics_solver_->getTipFrame());
  robot_state::RobotState kinematic_state(kinematic_model);

  bool succeeded;
  for(unsigned int i=0; i < NUM_FK_TEST; ++i)
  {
    seed.resize(ikfast_test.kinematics_solver_->getJointNames().size(), 0.0);
    fk_values.resize(ikfast_test.kinematics_solver_->getJointNames().size(), 0.0);
    kinematic_state.setToRandomPositions(joint_model_group);
    kinematic_state.copyJointGroupPositions(joint_model_group, fk_values);
    std::vector<geometry_msgs::Pose> poses;
    poses.resize(1);
    succeeded = ikfast_test.kinematics_solver_->getPositionFK(fk_names, fk_values, poses);
    ASSERT_TRUE(succeeded);
    ASSERT_TRUE(poses.size() == 1);

  }
}


TEST(IKFastPlugin, searchIK)
{
  rdf_loader::RDFLoader rdf_loader_(ROBOT_DESCRIPTION_PARAM);
  robot_model::RobotModelPtr kinematic_model;
  const boost::shared_ptr<srdf::Model> &srdf_model = rdf_loader_.getSRDF();
  const boost::shared_ptr<urdf::ModelInterface>& urdf_model = rdf_loader_.getURDF();
  kinematic_model.reset(new robot_model::RobotModel(urdf_model, srdf_model));
  robot_model::JointModelGroup* joint_model_group = kinematic_model->getJointModelGroup(ikfast_test.kinematics_solver_->getGroupName());

  //Test inverse kinematics
  std::vector<double> seed, fk_values, solution;
  double timeout = 5.0;
  moveit_msgs::MoveItErrorCodes error_code;
  solution.resize(ikfast_test.kinematics_solver_->getJointNames().size(), 0.0);
  std::vector<std::string> fk_names;
  fk_names.push_back(ikfast_test.kinematics_solver_->getTipFrame());
  robot_state::RobotState kinematic_state(kinematic_model);

  unsigned int success = 0;
  ros::WallTime start_time = ros::WallTime::now();
  for(unsigned int i=0; i < NUM_IK_TEST; ++i)
  {
    seed.resize(ikfast_test.kinematics_solver_->getJointNames().size(), 0.0);
    fk_values.resize(ikfast_test.kinematics_solver_->getJointNames().size(), 0.0);
    kinematic_state.setToRandomPositions(joint_model_group);
    kinematic_state.copyJointGroupPositions(joint_model_group, fk_values);
    std::vector<geometry_msgs::Pose> poses;
    poses.resize(1);

    bool result_fk = ikfast_test.kinematics_solver_->getPositionFK(fk_names, fk_values, poses);
    ASSERT_TRUE(result_fk);
    solution.clear();
    EXPECT_TRUE(ikfast_test.kinematics_solver_->searchPositionIK(poses[0], seed, timeout, solution, error_code));
    bool result = error_code.val == error_code.SUCCESS;

    ROS_DEBUG("Pose: %f %f %f",poses[0].position.x, poses[0].position.y, poses[0].position.z);
    ROS_DEBUG("Orient: %f %f %f %f",poses[0].orientation.x, poses[0].orientation.y, poses[0].orientation.z, poses[0].orientation.w);

    if(result)
    {
      EXPECT_TRUE(ikfast_test.kinematics_solver_->getPositionIK(poses[0], solution, solution, error_code));
      result = error_code.val == error_code.SUCCESS;
    }

    if(result)
    {
      success++;
    }
    else
    {
      ROS_ERROR_STREAM("searchPositionIK failed on test "<<i+1);
      continue;
    }

    std::vector<geometry_msgs::Pose> new_poses;
    new_poses.resize(1);
    result_fk = ikfast_test.kinematics_solver_->getPositionFK(fk_names, solution, new_poses);
    EXPECT_NEAR(poses[0].position.x, new_poses[0].position.x, IK_NEAR);
    EXPECT_NEAR(poses[0].position.y, new_poses[0].position.y, IK_NEAR);
    EXPECT_NEAR(poses[0].position.z, new_poses[0].position.z, IK_NEAR);
    EXPECT_NEAR(poses[0].orientation.x, new_poses[0].orientation.x, IK_NEAR);
    EXPECT_NEAR(poses[0].orientation.y, new_poses[0].orientation.y, IK_NEAR);
    EXPECT_NEAR(poses[0].orientation.z, new_poses[0].orientation.z, IK_NEAR);
    EXPECT_NEAR(poses[0].orientation.w, new_poses[0].orientation.w, IK_NEAR);
  }


  ROS_INFO_STREAM("Success Rate: "<<(double)success/NUM_IK_TEST);
  EXPECT_GT(success , 0.99 * NUM_IK_TEST);
  ROS_INFO_STREAM("Elapsed time: "<< (ros::WallTime::now()-start_time).toSec());
}

TEST(IKFastPlugin, searchIKWithCallback)
{
  rdf_loader::RDFLoader rdf_loader_(ROBOT_DESCRIPTION_PARAM);
  robot_model::RobotModelPtr kinematic_model;
  const boost::shared_ptr<srdf::Model> &srdf_model = rdf_loader_.getSRDF();
  const boost::shared_ptr<urdf::ModelInterface>& urdf_model = rdf_loader_.getURDF();
  kinematic_model.reset(new robot_model::RobotModel(urdf_model, srdf_model));
  robot_model::JointModelGroup* joint_model_group = kinematic_model->getJointModelGroup(ikfast_test.kinematics_solver_->getGroupName());

  //Test inverse kinematics
  std::vector<double> seed, fk_values, solution;
  double timeout = 5.0;
  moveit_msgs::MoveItErrorCodes error_code;
  solution.resize(ikfast_test.kinematics_solver_->getJointNames().size(), 0.0);
  std::vector<std::string> fk_names;
  fk_names.push_back(ikfast_test.kinematics_solver_->getTipFrame());
  robot_state::RobotState kinematic_state(kinematic_model);

  unsigned int success = 0;
  ros::WallTime start_time = ros::WallTime::now();
  for(unsigned int i=0; i < NUM_IK_TEST; ++i)
  {
    seed.resize(ikfast_test.kinematics_solver_->getJointNames().size(), 0.0);
    fk_values.resize(ikfast_test.kinematics_solver_->getJointNames().size(), 0.0);
    kinematic_state.setToRandomPositions(joint_model_group);
    kinematic_state.copyJointGroupPositions(joint_model_group, fk_values);
    std::vector<geometry_msgs::Pose> poses;
    poses.resize(1);

    bool result_fk = ikfast_test.kinematics_solver_->getPositionFK(fk_names, fk_values, poses);
    ASSERT_TRUE(result_fk);

    // check height
    if(poses[0].position.z <= 0.0f)
    {
      continue;
    }

    solution.clear();
    EXPECT_TRUE(ikfast_test.kinematics_solver_->searchPositionIK(poses[0], fk_values, timeout, solution,
                                                                 boost::bind(&IKFastTest::searchIKCallback,&ikfast_test,
                                                                             _1,_2,_3),error_code));
    bool result = error_code.val == error_code.SUCCESS;

    ROS_DEBUG("Pose: %f %f %f",poses[0].position.x, poses[0].position.y, poses[0].position.z);
    ROS_DEBUG("Orient: %f %f %f %f",poses[0].orientation.x, poses[0].orientation.y, poses[0].orientation.z, poses[0].orientation.w);

    if(result)
    {
      EXPECT_TRUE(ikfast_test.kinematics_solver_->getPositionIK(poses[0], solution, solution, error_code));
      result = error_code.val == error_code.SUCCESS;
    }

    if(result)
    {
      success++;
    }
    else
    {
      ROS_ERROR_STREAM("searchPositionIK failed on test "<<i+1);
      continue;
    }

    std::vector<geometry_msgs::Pose> new_poses;
    new_poses.resize(1);
    result_fk = ikfast_test.kinematics_solver_->getPositionFK(fk_names, solution, new_poses);
    EXPECT_NEAR(poses[0].position.x, new_poses[0].position.x, IK_NEAR);
    EXPECT_NEAR(poses[0].position.y, new_poses[0].position.y, IK_NEAR);
    EXPECT_NEAR(poses[0].position.z, new_poses[0].position.z, IK_NEAR);
    EXPECT_NEAR(poses[0].orientation.x, new_poses[0].orientation.x, IK_NEAR);
    EXPECT_NEAR(poses[0].orientation.y, new_poses[0].orientation.y, IK_NEAR);
    EXPECT_NEAR(poses[0].orientation.z, new_poses[0].orientation.z, IK_NEAR);
    EXPECT_NEAR(poses[0].orientation.w, new_poses[0].orientation.w, IK_NEAR);
  }


  ROS_INFO_STREAM("Success Rate: "<<(double)success/NUM_IK_TEST);
  EXPECT_GT(success , 0.99 * NUM_IK_TEST);
  ROS_INFO_STREAM("Elapsed time: "<< (ros::WallTime::now()-start_time).toSec());
}


TEST(IKFastPlugin, getIK)
{
  rdf_loader::RDFLoader rdf_loader_(ROBOT_DESCRIPTION_PARAM);
  robot_model::RobotModelPtr kinematic_model;
  const boost::shared_ptr<srdf::Model> &srdf_model = rdf_loader_.getSRDF();
  const boost::shared_ptr<urdf::ModelInterface>& urdf_model = rdf_loader_.getURDF();
  kinematic_model.reset(new robot_model::RobotModel(urdf_model, srdf_model));
  robot_model::JointModelGroup* joint_model_group = kinematic_model->getJointModelGroup(ikfast_test.kinematics_solver_->getGroupName());

  //Test inverse kinematics
  std::vector<double> seed, fk_values, solution;
  double timeout = 5.0;
  moveit_msgs::MoveItErrorCodes error_code;
  solution.resize(ikfast_test.kinematics_solver_->getJointNames().size(), 0.0);
  std::vector<std::string> fk_names;
  fk_names.push_back(ikfast_test.kinematics_solver_->getTipFrame());
  robot_state::RobotState kinematic_state(kinematic_model);

  unsigned int success = 0;
  ros::WallTime start_time = ros::WallTime::now();
  for(unsigned int i=0; i < NUM_IK_TEST; ++i)
  {
    seed.resize(ikfast_test.kinematics_solver_->getJointNames().size(), 0.0);
    fk_values.resize(ikfast_test.kinematics_solver_->getJointNames().size(), 0.0);
    kinematic_state.setToRandomPositions(joint_model_group);
    kinematic_state.copyJointGroupPositions(joint_model_group, fk_values);
    std::vector<geometry_msgs::Pose> poses;
    poses.resize(1);

    bool result_fk = ikfast_test.kinematics_solver_->getPositionFK(fk_names, fk_values, poses);
    ASSERT_TRUE(result_fk);
    solution.clear();

    EXPECT_TRUE(ikfast_test.kinematics_solver_->getPositionIK(poses[0], fk_values, solution, error_code));
    ROS_DEBUG("Pose: %f %f %f",poses[0].position.x, poses[0].position.y, poses[0].position.z);
    ROS_DEBUG("Orient: %f %f %f %f",poses[0].orientation.x, poses[0].orientation.y, poses[0].orientation.z, poses[0].orientation.w);

    if(error_code.val == error_code.SUCCESS)
    {
      success++;
    }
    else
    {
      ROS_ERROR_STREAM("getPositionIK failed on test "<<i+1);
      continue;
    }

    std::vector<geometry_msgs::Pose> new_poses;
    new_poses.resize(1);
    result_fk = ikfast_test.kinematics_solver_->getPositionFK(fk_names, solution, new_poses);
    EXPECT_NEAR(poses[0].position.x, new_poses[0].position.x, IK_NEAR);
    EXPECT_NEAR(poses[0].position.y, new_poses[0].position.y, IK_NEAR);
    EXPECT_NEAR(poses[0].position.z, new_poses[0].position.z, IK_NEAR);
    EXPECT_NEAR(poses[0].orientation.x, new_poses[0].orientation.x, IK_NEAR);
    EXPECT_NEAR(poses[0].orientation.y, new_poses[0].orientation.y, IK_NEAR);
    EXPECT_NEAR(poses[0].orientation.z, new_poses[0].orientation.z, IK_NEAR);
    EXPECT_NEAR(poses[0].orientation.w, new_poses[0].orientation.w, IK_NEAR);
  }


  ROS_INFO_STREAM("Success Rate: "<<(double)success/NUM_IK_TEST);
  EXPECT_GT(success , 0.99 * NUM_IK_TEST);
  ROS_INFO_STREAM("Elapsed time: "<< (ros::WallTime::now()-start_time).toSec());
}

TEST(IKFastPlugin, getIKMultipleSolutions)
{
  rdf_loader::RDFLoader rdf_loader_(ROBOT_DESCRIPTION_PARAM);
  robot_model::RobotModelPtr kinematic_model;
  const boost::shared_ptr<srdf::Model> &srdf_model = rdf_loader_.getSRDF();
  const boost::shared_ptr<urdf::ModelInterface>& urdf_model = rdf_loader_.getURDF();
  kinematic_model.reset(new robot_model::RobotModel(urdf_model, srdf_model));
  robot_model::JointModelGroup* joint_model_group = kinematic_model->getJointModelGroup(ikfast_test.kinematics_solver_->getGroupName());

  //Test inverse kinematics
  std::vector<double> seed, fk_values;
  std::vector< std::vector<double> > solutions;
  double timeout = 5.0;
  kinematics::KinematicsQueryOptions options;
  kinematics::KinematicsResult result;

  std::vector<std::string> fk_names;
  fk_names.push_back(ikfast_test.kinematics_solver_->getTipFrame());
  robot_state::RobotState kinematic_state(kinematic_model);

  unsigned int success = 0;
  ros::WallTime start_time = ros::WallTime::now();
  for(unsigned int i=0; i < NUM_IK_TEST; ++i)
  {
    seed.resize(ikfast_test.kinematics_solver_->getJointNames().size(), 0.0);
    fk_values.resize(ikfast_test.kinematics_solver_->getJointNames().size(), 0.0);
    kinematic_state.setToRandomPositions(joint_model_group);
    kinematic_state.copyJointGroupPositions(joint_model_group, fk_values);
    std::vector<geometry_msgs::Pose> poses;
    poses.resize(1);

    ASSERT_TRUE(ikfast_test.kinematics_solver_->getPositionFK(fk_names, fk_values, poses));

    solutions.clear();
    EXPECT_TRUE(ikfast_test.kinematics_solver_->getPositionIK(poses, solutions, result,options));
    ROS_DEBUG("Pose: %f %f %f",poses[0].position.x, poses[0].position.y, poses[0].position.z);
    ROS_DEBUG("Orient: %f %f %f %f",poses[0].orientation.x, poses[0].orientation.y, poses[0].orientation.z, poses[0].orientation.w);

    if(result.kinematic_error == kinematics::KinematicErrors::OK)
    {
      EXPECT_GT(solutions.size(),0);
      success = solutions.empty() ? success : success + 1;
    }
    else
    {
      ROS_ERROR_STREAM("getPositionIK failed on test "<<i+1);
      continue;
    }

    std::vector<geometry_msgs::Pose> new_poses;
    new_poses.resize(1);

    for(unsigned int i = 0; i < solutions.size();i++)
    {
      std::vector<double>& solution = solutions[i];
      EXPECT_TRUE(ikfast_test.kinematics_solver_->getPositionFK(fk_names, solution, new_poses));
      EXPECT_NEAR(poses[0].position.x, new_poses[0].position.x, IK_NEAR);
      EXPECT_NEAR(poses[0].position.y, new_poses[0].position.y, IK_NEAR);
      EXPECT_NEAR(poses[0].position.z, new_poses[0].position.z, IK_NEAR);
      EXPECT_NEAR(poses[0].orientation.x, new_poses[0].orientation.x, IK_NEAR);
      EXPECT_NEAR(poses[0].orientation.y, new_poses[0].orientation.y, IK_NEAR);
      EXPECT_NEAR(poses[0].orientation.z, new_poses[0].orientation.z, IK_NEAR);
      EXPECT_NEAR(poses[0].orientation.w, new_poses[0].orientation.w, IK_NEAR);
    }


  }


  ROS_INFO_STREAM("Success Rate: "<<(double)success/NUM_IK_TEST);
  EXPECT_GT(success , 0.99 * NUM_IK_TEST);
  ROS_INFO_STREAM("Elapsed time: "<< (ros::WallTime::now()-start_time).toSec());
}




int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc, argv);
  ros::init (argc, argv, "ikfast_plugin_test");
  return RUN_ALL_TESTS();
}