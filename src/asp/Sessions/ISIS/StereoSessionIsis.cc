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


/// \file StereoSessionIsis.cc
///

// Vision Workbench
#include <vw/FileIO.h>
#include <vw/Math/Functors.h>
#include <vw/Math/Geometry.h>
#include <vw/InterestPoint.h>
#include <vw/Stereo/DisparityMap.h>
#include <vw/Cartography.h>

// Stereo Pipeline
#include <asp/Core/StereoSettings.h>
#include <asp/Core/InterestPointMatching.h>
#include <asp/Sessions/ISIS/StereoSessionIsis.h>
#include <asp/IsisIO/IsisCameraModel.h>
#include <asp/IsisIO/IsisAdjustCameraModel.h>
#include <asp/IsisIO/DiskImageResourceIsis.h>
#include <asp/Sessions/ISIS/PhotometricOutlier.h>

// Boost
#include <boost/filesystem/operations.hpp>
#include <boost/math/special_functions/next.hpp> // boost::float_next
#include <boost/shared_ptr.hpp>
namespace fs = boost::filesystem;

#include <algorithm>

using namespace vw;
using namespace vw::camera;
using namespace asp;

// Allows FileIO to correctly read/write these pixel types
namespace vw {
  template<> struct PixelFormatID<Vector3>   { static const PixelFormatEnum value = VW_PIXEL_GENERIC_3_CHANNEL; };
}

// Process a single ISIS image to find an ideal min max. The reason we
// need to do this, is that ASP is to get image intensity values in
// the range of 0-1. To some extent we are compressing the dynamic
// range, but we try to minimize that.
ImageViewRef< PixelMask<float> >
find_ideal_isis_range(DiskImageView<float> const& image,
                      boost::shared_ptr<DiskImageResourceIsis> isis_rsrc,
                      float nodata_value,
                      std::string const& tag,
                      bool & will_apply_user_nodata,
                      float & isis_lo, float & isis_hi ) {

  will_apply_user_nodata = false;
  isis_lo = isis_rsrc->valid_minimum();
  isis_hi = isis_rsrc->valid_maximum();

  if (!boost::math::isnan(nodata_value) && nodata_value >= isis_lo){
    // The new lower bound is the next floating point number > nodata_value.
    will_apply_user_nodata = true;
    isis_lo = boost::math::float_next(nodata_value);
    if (isis_hi < isis_lo) isis_hi = isis_lo;
  }

  ImageViewRef< PixelMask<float> > masked_image = create_mask(image, isis_lo, isis_hi);

  // Calculating statistics. We subsample the images so statistics
  // only does about a million samples.
  float isis_mean, isis_std;
  {
    vw_out(InfoMessage) << "\t--> Computing statistics for the "+tag+" image\n";
    int stat_scale = int(ceil(sqrt(float(image.cols())*float(image.rows()) / 1000000)));
    ChannelAccumulator<math::CDFAccumulator<float> > accumulator;
    for_each_pixel(subsample(edge_extend(masked_image, ConstantEdgeExtension()),
                             stat_scale ),
                   accumulator );
    isis_lo = accumulator.quantile(0);
    isis_hi = accumulator.quantile(1);
    isis_mean = accumulator.approximate_mean();
    isis_std  = accumulator.approximate_stddev();

    vw_out(InfoMessage) << "\t  "+tag+": [ lo:" << isis_lo << " hi:" << isis_hi
                        << " m: " << isis_mean << " s: " << isis_std <<  "]\n";
  }

  // Normalizing to -+2 sigmas around mean
  if ( stereo_settings().force_use_entire_range == 0 ) {
    vw_out(InfoMessage) << "\t--> Adjusting hi and lo to -+2 sigmas around mean.\n";

    if ( isis_lo < isis_mean - 2*isis_std )
      isis_lo = isis_mean - 2*isis_std;
    if ( isis_hi > isis_mean + 2*isis_std )
      isis_hi = isis_mean + 2*isis_std;

    vw_out(InfoMessage) << "\t    "+tag+" changed: [ lo:"
                        << isis_lo << " hi:" << isis_hi << "]\n";
  }

  return masked_image;
}

// This actually modifies and writes the pre-processed image.
void write_preprocessed_isis_image( BaseOptions const& opt,
                                    bool will_apply_user_nodata,
                                    ImageViewRef< PixelMask <float> > masked_image,
                                    std::string const& out_file,
                                    std::string const& tag,
                                    float isis_lo, float isis_hi,
                                    float out_lo, float out_hi,
                                    Matrix<double> const& matrix,
                                    Vector2i const& crop_size ) {

  // The output no-data value must be < 0 as we scale the images to [0, 1].
  float output_nodata = -32767.0;

  ImageViewRef<float> image_sans_mask = apply_mask(masked_image, isis_lo);

  ImageViewRef<float> processed_image
    = remove_isis_special_pixels(image_sans_mask, isis_lo, isis_hi, out_lo);

  if (will_apply_user_nodata){

    // If the user specifies a no-data value, mask all pixels <= that
    // value. Note: this causes non-trivial erosion at image
    // boundaries where invalid pixels show up if homography is used.

    ImageViewRef<uint8> mask = channel_cast_rescale<uint8>(select_channel(masked_image, 1));

    ImageViewRef< PixelMask<float> > normalized_image =
      normalize(copy_mask(processed_image, create_mask(mask)), out_lo, out_hi, 0.0, 1.0);

    ImageViewRef< PixelMask<float> > applied_image;
    if ( matrix == math::identity_matrix<3>() ) {
      applied_image = crop(edge_extend(normalized_image, ZeroEdgeExtension()),
                           0, 0, crop_size[0], crop_size[1]);
    } else {
      applied_image = transform(normalized_image, HomographyTransform(matrix),
                                crop_size[0], crop_size[1]);
    }

    vw_out() << "\t--> Writing normalized image: " << out_file << "\n";
    block_write_gdal_image( out_file, apply_mask(applied_image, output_nodata), output_nodata, opt,
                            TerminalProgressCallback("asp", "\t  "+tag+":  "));

  }else{

    // Set invalid pixels to the minimum pixel value. Causes less
    // erosion and the results are good.

    ImageViewRef<float> normalized_image =
      normalize(processed_image, out_lo, out_hi, 0.0, 1.0);

    ImageViewRef<float> applied_image;
    if ( matrix == math::identity_matrix<3>() ) {
      applied_image = crop(edge_extend(normalized_image, ZeroEdgeExtension()),
                           0, 0, crop_size[0], crop_size[1]);
    } else {
      applied_image = transform(normalized_image, HomographyTransform(matrix),
                                crop_size[0], crop_size[1]);
    }

    vw_out() << "\t--> Writing normalized image: " << out_file << "\n";
    block_write_gdal_image( out_file, applied_image, output_nodata, opt,
                            TerminalProgressCallback("asp", "\t  "+tag+":  "));


  }

}

void
asp::StereoSessionIsis::pre_preprocessing_hook(std::string const& left_input_file,
                                               std::string const& right_input_file,
                                               std::string & left_output_file,
                                               std::string & right_output_file) {
  left_output_file = m_out_prefix + "-L.tif";
  right_output_file = m_out_prefix + "-R.tif";

  if ( fs::exists(left_output_file) && fs::exists(right_output_file) ) {
    try {
      vw_log().console_log().rule_set().add_rule(-1,"fileio");
      DiskImageView<PixelGray<float32> > out_left(left_output_file);
      DiskImageView<PixelGray<float32> > out_right(right_output_file);
      vw_out(InfoMessage) << "\t--> Using cached normalized input images.\n";
      vw_settings().reload_config();
      return;
    } catch (vw::ArgumentErr const& e) {
      // This throws on a corrupted file.
      vw_settings().reload_config();
    } catch (vw::IOErr const& e) {
      vw_settings().reload_config();
    }
  }

  boost::shared_ptr<DiskImageResource> left_rsrc( DiskImageResource::open(left_input_file) ),
    right_rsrc( DiskImageResource::open(right_input_file) );

  float left_nodata_value, right_nodata_value;
  get_nodata_values(left_rsrc, right_rsrc, left_nodata_value, right_nodata_value);

  // Load the unmodified images
  DiskImageView<float> left_disk_image( left_rsrc ), right_disk_image( right_rsrc );

  // Mask the pixels outside of the isis range and <= nodata.
  float left_lo, left_hi, right_lo, right_hi;

  boost::shared_ptr<DiskImageResourceIsis>
    left_isis_rsrc(new DiskImageResourceIsis(left_input_file)),
    right_isis_rsrc(new DiskImageResourceIsis(right_input_file));


  // These variables will be true if we reduce the valid range for ISIS images
  // using the nodata value provided by the user.
  bool will_apply_user_nodata_left = false, will_apply_user_nodata_right = false;

  ImageViewRef< PixelMask <float> > left_masked_image
    = find_ideal_isis_range(left_disk_image, left_isis_rsrc, left_nodata_value, "left",
                            will_apply_user_nodata_left, left_lo, left_hi);
  ImageViewRef< PixelMask <float> > right_masked_image
    = find_ideal_isis_range(right_disk_image, right_isis_rsrc, right_nodata_value, "right",
                            will_apply_user_nodata_right, right_lo, right_hi);

  // Working out alignment
  float lo = std::min(left_lo, right_lo);  // Finding global
  float hi = std::max(left_hi, right_hi);
  Matrix<double> align_matrix(3,3);
  align_matrix.set_identity();
  if ( stereo_settings().alignment_method == "homography" ) {
    std::string match_filename
      = ip::match_filename(m_out_prefix, left_input_file, right_input_file);

    if (!fs::exists(match_filename)) {
      boost::shared_ptr<camera::CameraModel> left_cam, right_cam;
      camera_models( left_cam, right_cam );

      boost::shared_ptr<IsisCameraModel> isis_cam =
        boost::dynamic_pointer_cast<IsisCameraModel>(left_cam);
      VW_ASSERT( isis_cam.get() != NULL,
               ArgumentErr() << "StereoSessionISIS: Invalid left camera.\n" );
      Vector3 radii = isis_cam->target_radii();
      cartography::Datum datum("","","", (radii[0] + radii[1]) / 2, radii[2], 0);

      bool inlier =
        ip_matching_w_alignment( left_cam.get(), right_cam.get(),
                                 left_disk_image, right_disk_image,
                                 datum, match_filename,
                                 left_nodata_value, right_nodata_value);
      if ( !inlier ) {
        fs::remove( match_filename );
        vw_throw( IOErr() << "Unable to match left and right images." );
      }
    }else{
      vw_out() << "\t--> Using cached match file: " << match_filename << "\n";
    }

    std::vector<ip::InterestPoint> left_ip, right_ip;
    ip::read_binary_match_file( match_filename, left_ip, right_ip  );
    align_matrix = homography_fit(right_ip, left_ip, bounding_box(DiskImageView<float>(left_input_file)) );

    write_matrix( m_out_prefix + "-align.exr", align_matrix );
    vw_out() << "\t--> Aligning right image to left using homography:\n"
             << "\t      " << align_matrix << "\n";
  } else if ( stereo_settings().alignment_method == "epipolar" ) {
    vw_throw( NoImplErr() << "StereoSessionISIS does not support epipolar rectification" );
  }

  // Getting left image size
  Vector2i left_size = file_image_size( left_input_file ),
    right_size = file_image_size( right_input_file );

  // Apply alignment and normalization
  bool will_apply_user_nodata = ( will_apply_user_nodata_left || will_apply_user_nodata_right);

  if (stereo_settings().individually_normalize == 0 ) {
    vw_out() << "\t--> Normalizing globally to: ["<<lo<<" "<<hi<<"]\n";
    write_preprocessed_isis_image( m_options, will_apply_user_nodata,
                                   left_masked_image, left_output_file, "left",
                                   left_lo, left_hi, lo, hi,
                                   math::identity_matrix<3>(), left_size );
    if ( stereo_settings().alignment_method == "none" )
      write_preprocessed_isis_image( m_options, will_apply_user_nodata,
                                     right_masked_image, right_output_file, "right",
                                     right_lo, right_hi, lo, hi,
                                     math::identity_matrix<3>(), right_size );
    else
      write_preprocessed_isis_image( m_options, will_apply_user_nodata,
                                     right_masked_image, right_output_file, "right",
                                     right_lo, right_hi, lo, hi,
                                     align_matrix, left_size );
  } else {
    vw_out() << "\t--> Individually normalizing.\n";
    write_preprocessed_isis_image( m_options, will_apply_user_nodata,
                                   left_masked_image, left_output_file, "left",
                                   left_lo, left_hi, left_lo, left_hi,
                                   math::identity_matrix<3>(), left_size );
    if ( stereo_settings().alignment_method == "none" )
      write_preprocessed_isis_image( m_options, will_apply_user_nodata,
                                     right_masked_image, right_output_file, "right",
                                     right_lo, right_hi, right_lo, right_hi,
                                     math::identity_matrix<3>(), right_size );
    else
      write_preprocessed_isis_image( m_options, will_apply_user_nodata,
                                     right_masked_image, right_output_file, "right",
                                     right_lo, right_hi, right_lo, right_hi,
                                     align_matrix, left_size );
  }
}

inline std::string write_shadow_mask( BaseOptions const& opt,
                                      std::string const& output_prefix,
                                      std::string const& input_image,
                                      std::string const& mask_postfix ) {
  // This thresholds at -25000 as the input sub4s for Apollo that I've
  // processed have a range somewhere between -32000 and +32000. -ZMM
  DiskImageView<PixelGray<float> > disk_image( input_image );
  DiskImageView<uint8> disk_mask( output_prefix + mask_postfix );
  ImageViewRef<uint8> mask =
    apply_mask(intersect_mask(create_mask(disk_mask),
                              create_mask(threshold(disk_image,-25000,0,1.0))));
  std::string output_mask =
    output_prefix+mask_postfix.substr(0,mask_postfix.size()-4)+"Debug.tif";

  block_write_gdal_image( output_mask, mask, opt,
                          TerminalProgressCallback("asp","\t  Shadow:") );
  return output_mask;
}

// Stage 2: Correlation
//
// Pre file is a pair of grayscale images.  ( ImageView<PixelGray<float> > )
// Post file is a disparity map.            ( ImageView<PixelMask<Vector2f> > )
void
asp::StereoSessionIsis::pre_filtering_hook(std::string const& input_file,
                                           std::string & output_file) {
  output_file = input_file;

  if (stereo_settings().mask_flatfield) {
    // ****************************************************
    // The following code is for Apollo Metric Camera ONLY!
    // (use at your own risk)
    // ****************************************************
    vw_out() << "\t--> Masking pixels that are less than 0.0.  (NOTE: Use this option with Apollo Metric Camera frames only!)\n";
    output_file = m_out_prefix + "-R-masked.exr";

    std::string shadowLmask_name =
      write_shadow_mask( m_options, m_out_prefix, m_left_image_file,
                         "-lMask.tif" );
    std::string shadowRmask_name =
      write_shadow_mask( m_options, m_out_prefix, m_right_image_file,
                         "-rMask.tif" );

    DiskImageView<uint8> shadowLmask( shadowLmask_name );
    DiskImageView<uint8> shadowRmask( shadowRmask_name );

    DiskImageView<PixelMask<Vector2f> > disparity_disk_image(input_file);
    ImageViewRef<PixelMask<Vector2f> > disparity_map =
      stereo::disparity_mask(disparity_disk_image,
                             shadowLmask, shadowRmask );

    DiskImageResourceOpenEXR disparity_map_rsrc(output_file, disparity_map.format() );
    Vector2i block_size(std::min<size_t>(vw_settings().default_tile_size(),
                                         disparity_map.cols()),
                        std::min<size_t>(vw_settings().default_tile_size(),
                                         disparity_map.rows()));
    disparity_map_rsrc.set_block_write_size(block_size);
    block_write_image( disparity_map_rsrc, disparity_map,
                       TerminalProgressCallback( "asp", "\t--> Saving Mask :") );
  }
}

// Reverse any pre-alignment that was done to the disparity.
ImageViewRef<PixelMask<Vector2f> >
asp::StereoSessionIsis::pre_pointcloud_hook(std::string const& input_file) {

  std::string dust_result = input_file;
  if ( stereo_settings().mask_flatfield ) {
    // ****************************************************
    // The following code is for Apollo Metric Camera ONLY!
    // (use at your own risk)
    // ****************************************************
    vw_out() << "\t--> Masking pixels that appear to be dust.  (NOTE: Use this option with Apollo Metric Camera frames only!)\n";
    photometric_outlier_rejection( m_options, m_out_prefix, input_file,
                                   dust_result, stereo_settings().corr_kernel[0] );
  }

  DiskImageView<PixelMask<Vector2f> > disparity_map(dust_result);

  ImageViewRef<PixelMask<Vector2f> > result;
  if ( stereo_settings().alignment_method == "homography" ) {
    // We used a homography to line up the images, so we must undo the
    // homography transform in the disparity_map before triangulation
    // happens.
    Matrix<double> align_matrix;
    read_matrix(align_matrix, m_out_prefix + "-align.exr");
    vw_out(DebugMessage,"asp") << "Alignment Matrix: " << align_matrix << "\n";

    // Remove pixels that are outside the bounds of the secondary image.
    DiskImageView<float> right_disk_image(m_right_image_file);
    result =
      stereo::disparity_range_mask(stereo::transform_disparities(disparity_map,
                                                                 HomographyTransform(align_matrix)),
                                   Vector2f(0,0),
                                   Vector2f( right_disk_image.cols(),
                                             right_disk_image.rows() ) );
  } else {
    result = disparity_map;
  }

  return result;
}

boost::shared_ptr<vw::camera::CameraModel>
asp::StereoSessionIsis::camera_model(std::string const& image_file,
                                     std::string const& camera_file) {

  if (boost::ends_with(boost::to_lower_copy(camera_file), ".isis_adjust")){
    // Creating Equations for the files
    std::ifstream input( camera_file.c_str() );
    boost::shared_ptr<asp::BaseEquation> posF = read_equation( input );
    boost::shared_ptr<asp::BaseEquation> poseF = read_equation( input );
    input.close();

    // Finally creating camera model
    return boost::shared_ptr<camera::CameraModel>(new IsisAdjustCameraModel( image_file, posF, poseF ));

  } else {
    return boost::shared_ptr<camera::CameraModel>(new IsisCameraModel(image_file));
  }

}
