// __BEGIN_LICENSE__
//  Copyright (c) 2009-2012, United States Government as represented by the
//  Administrator of the National Aeronautics and Space Administration. All
//  rights reserved.
//
//  The NGT platform is licensed under the Apache License, Version 2.0 (the
//  "License"); you may not use this file except in compliance with the
//  License. You may obtain a copy of the License at
//  http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.
// __END_LICENSE__


/// \file StereoSessionRPC.cc
///

// Ames Stereo Pipeline
#include <asp/Core/StereoSettings.h>
#include <asp/Core/InterestPointMatching.h>
#include <asp/Sessions/RPC/StereoSessionRPC.h>
#include <asp/Sessions/DG/XML.h>
#include <asp/Sessions/RPC/RPCModel.h>

// Vision Workbench
#include <vw/Camera/CameraModel.h>

using namespace vw;
using namespace asp;

namespace asp {

  // Provide our camera model
  boost::shared_ptr<camera::CameraModel>
  StereoSessionRPC::camera_model(std::string const& image_file,
                                 std::string const& camera_file ) {
    return boost::shared_ptr<camera::CameraModel>(StereoSessionDG::read_rpc_model(image_file, camera_file));
  }

} // namespace asp
