/*
 * Copyright (C) 2019 Open Source Robotics Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <vector>

#include <gz/plugin/Register.hh>
#include <gz/transport/Node.hh>

#include <gz/common/Profiler.hh>

#include <sdf/Element.hh>

#include "gz/sim/components/DetachableJoint.hh"
#include "gz/sim/components/Link.hh"
#include "gz/sim/components/Model.hh"
#include "gz/sim/components/Name.hh"
#include "gz/sim/components/ParentEntity.hh"
#include "gz/sim/components/Pose.hh"
#include "gz/sim/Model.hh"
#include "gz/sim/Util.hh"

#include "DetachableJoint.hh"

using namespace gz;
using namespace sim;
using namespace systems;

/////////////////////////////////////////////////
void DetachableJoint::Configure(const Entity &_entity,
               const std::shared_ptr<const sdf::Element> &_sdf,
               EntityComponentManager &_ecm,
               EventManager &/*_eventMgr*/)
{
  this->model = Model(_entity);
  if (!this->model.Valid(_ecm))
  {
    gzerr << "DetachableJoint should be attached to a model entity. "
           << "Failed to initialize." << std::endl;
    return;
  }

  if (_sdf->HasElement("parent_link"))
  {
    auto parentLinkName = _sdf->Get<std::string>("parent_link");
    this->parentLinkEntity = this->model.LinkByName(_ecm, parentLinkName);
    if (kNullEntity == this->parentLinkEntity)
    {
      gzerr << "Link with name " << parentLinkName
             << " not found in model " << this->model.Name(_ecm)
             << ". Make sure the parameter 'parent_link' has the "
             << "correct value. Failed to initialize.\n";
      return;
    }
  }
  else
  {
    gzerr << "'parent_link' is a required parameter for DetachableJoint. "
              "Failed to initialize.\n";
    return;
  }

  if (_sdf->HasElement("child_model"))
  {
    this->childModelName = _sdf->Get<std::string>("child_model");
  }
  else
  {
    gzerr << "'child_model' is a required parameter for DetachableJoint."
              "Failed to initialize.\n";
    return;
  }

  if (_sdf->HasElement("child_link"))
  {
    this->childLinkName = _sdf->Get<std::string>("child_link");
  }
  else
  {
    gzerr << "'child_link' is a required parameter for DetachableJoint."
              "Failed to initialize.\n";
    return;
  }

  // Setup detach topic
  std::vector<std::string> topics;
  if (_sdf->HasElement("topic"))
  {
    topics.push_back(_sdf->Get<std::string>("topic"));
  }
  topics.push_back("/model/" + this->model.Name(_ecm) +
      "/detachable_joint/detach");
  this->topic = validTopic(topics);

  this->suppressChildWarning =
      _sdf->Get<bool>("suppress_child_warning", this->suppressChildWarning)
          .first;

  this->validConfig = true;
}

//////////////////////////////////////////////////
void DetachableJoint::PreUpdate(
  const UpdateInfo &/*_info*/,
  EntityComponentManager &_ecm)
{
  GZ_PROFILE("DetachableJoint::PreUpdate");
  if (this->validConfig && !this->initialized)
  {
    // Look for the child model and link
    Entity modelEntity{kNullEntity};

    if ("__model__" == this->childModelName)
    {
      modelEntity = this->model.Entity();
    }
    else
    {
      modelEntity = _ecm.EntityByComponents(
          components::Model(), components::Name(this->childModelName));
    }
    if (kNullEntity != modelEntity)
    {
      this->childLinkEntity = _ecm.EntityByComponents(
          components::Link(), components::ParentEntity(modelEntity),
          components::Name(this->childLinkName));

      if (kNullEntity != this->childLinkEntity)
      {
        // Attach the models
        // We do this by creating a detachable joint entity.
        this->detachableJointEntity = _ecm.CreateEntity();

        _ecm.CreateComponent(
            this->detachableJointEntity,
            components::DetachableJoint({this->parentLinkEntity,
                                         this->childLinkEntity, "fixed"}));

        this->node.Subscribe(
            this->topic, &DetachableJoint::OnDetachRequest, this);

        gzmsg << "DetachableJoint subscribing to messages on "
               << "[" << this->topic << "]" << std::endl;

        this->initialized = true;
      }
      else
      {
        gzwarn << "Child Link " << this->childLinkName
                << " could not be found.\n";
      }
    }
    else if (!this->suppressChildWarning)
    {
      gzwarn << "Child Model " << this->childModelName
              << " could not be found.\n";
    }
  }

  if (this->initialized)
  {
    if (this->detachRequested && (kNullEntity != this->detachableJointEntity))
    {
      // Detach the models
      gzdbg << "Removing entity: " << this->detachableJointEntity << std::endl;
      _ecm.RequestRemoveEntity(this->detachableJointEntity);
      this->detachableJointEntity = kNullEntity;
      this->detachRequested = false;
    }
  }
}

//////////////////////////////////////////////////
void DetachableJoint::OnDetachRequest(const msgs::Empty &)
{
  this->detachRequested = true;
}

GZ_ADD_PLUGIN(DetachableJoint,
                    System,
                    DetachableJoint::ISystemConfigure,
                    DetachableJoint::ISystemPreUpdate)

GZ_ADD_PLUGIN_ALIAS(DetachableJoint,
  "gz::sim::systems::DetachableJoint")

// TODO(CH3): Deprecated, remove on version 8
GZ_ADD_PLUGIN_ALIAS(DetachableJoint,
  "ignition::gazebo::systems::DetachableJoint")
