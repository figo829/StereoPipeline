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


/// \file IsisInterfaceFrame.h
///
/// Frame Camera Interface with ISIS
///
#ifndef __ASP_ISIS_INTERFACE_FRAME_H__
#define __ASP_ISIS_INTERFACE_FRAME_H__

// ASP
#include <asp/IsisIO/IsisInterface.h>

// Isis
#include <CameraDetectorMap.h>
#include <CameraDistortionMap.h>
#include <CameraFocalPlaneMap.h>
#include <AlphaCube.h>

namespace asp {
namespace isis {

  class IsisInterfaceFrame : public IsisInterface {

  public:
    IsisInterfaceFrame( std::string const& filename );

    virtual std::string type()  { return "Frame"; }

    // Standard Methods
    //-------------------------------------------------

    virtual vw::Vector2
      point_to_pixel( vw::Vector3 const& point ) const;
    virtual vw::Vector3
      pixel_to_vector( vw::Vector2 const& pix ) const;
    virtual vw::Vector3
      camera_center( vw::Vector2 const& pix = vw::Vector2(1,1) ) const;
    virtual vw::Quat
      camera_pose( vw::Vector2 const& pix = vw::Vector2(1,1) ) const;

  protected:

    // Custom Variables
    Isis::CameraDistortionMap *m_distortmap;
    Isis::CameraFocalPlaneMap *m_focalmap;
    Isis::CameraDetectorMap   *m_detectmap;
    mutable Isis::AlphaCube   m_alphacube;

    vw::Vector3 m_center;
    vw::Quat m_pose;
  };

}}

#endif//__ASP_ISIS_INTERFACE_FRAME_H__
