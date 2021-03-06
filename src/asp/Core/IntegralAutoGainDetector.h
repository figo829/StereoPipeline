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


#ifndef __ASP_CORE_INTEGRAL_AUTO_GAIN_DETECTOR_H__
#define __ASP_CORE_INTEGRAL_AUTO_GAIN_DETECTOR_H__

#include <vw/InterestPoint/InterestData.h>
#include <vw/InterestPoint/IntegralDetector.h>
#include <vw/InterestPoint/IntegralInterestOperator.h>

namespace asp {

  class IntegralAutoGainDetector : public vw::ip::InterestDetectorBase<IntegralAutoGainDetector>,
                                   private boost::noncopyable {

  public:
    static const int IP_DEFAULT_SCALES = 8;

    IntegralAutoGainDetector( size_t max_points = 200 )
      : m_interest(0), m_scales(IP_DEFAULT_SCALES), m_obj_points(max_points) {}

    /// Detect Interest Points in the source image.
    template <class ViewT>
    vw::ip::InterestPointList process_image(vw::ImageViewBase<ViewT> const& image ) const {
      using namespace vw;
      typedef ImageView<typename PixelChannelType<typename ViewT::pixel_type>::type> ImageT;
      typedef ip::ImageInterestData<ImageT,ip::OBALoGInterestOperator> DataT;
      Timer total("\t\tTotal elapsed time", DebugMessage, "interest_point");

      // The input image is a lazy view. We'll rasterize so we're not
      // hitting the cache all of the image.
      ImageT original_image = image.impl();

      // The ImageInterestData structure doesn't really apply to
      // OBALoG. We don't need access to the original image after
      // we've made the integral image. To avoid excessive copying,
      // we're making an empty image to feed that structure.
      ImageT empty_image;

      // Producing Integral Image
      ImageT integral_image;
      {
        vw_out(DebugMessage, "interest_point") << "\tCreating Integral Image ...";
        Timer t("done, elapsed time", DebugMessage, "interest_point");
        integral_image = ip::IntegralImage( original_image );
      }

      // Creating Scales
      std::deque<DataT> interest_data;
      interest_data.push_back( DataT(empty_image, integral_image) );
      interest_data.push_back( DataT(empty_image, integral_image) );

      // Priming scales
      vw::ip::InterestPointList new_points;
      {
        vw_out(DebugMessage, "interest_point") << "\tScale 0 ... ";
        Timer t("done, elapsed time", DebugMessage, "interest_point");
        m_interest( interest_data[0], 0 );
      }
      {
        vw_out(DebugMessage, "interest_point") << "\tScale 1 ... ";
        Timer t("done, elapsed time", DebugMessage, "interest_point");
        m_interest( interest_data[1], 1 );
      }
      // Finally processing scales
      for ( int scale = 2; scale < m_scales; scale++ ) {

        interest_data.push_back( DataT(empty_image, integral_image) );
        {
          vw_out(DebugMessage, "interest_point") << "\tScale " << scale << " ... ";
          Timer t("done, elapsed time", DebugMessage, "interest_point");
          m_interest( interest_data[2], scale );
        }

        ip::InterestPointList scale_points;

        // Detecting interest points in middle
        int32 cols = original_image.cols() - 2;
        int32 rows = original_image.rows() - 2;
        typedef typename DataT::interest_type::pixel_accessor AccessT;

        AccessT l_row = interest_data[0].interest().origin();
        AccessT m_row = interest_data[1].interest().origin();
        AccessT h_row = interest_data[2].interest().origin();
        l_row.advance(1,1); m_row.advance(1,1); h_row.advance(1,1);
        for ( int32 r=0; r < rows; r++ ) {
          AccessT l_col = l_row;
          AccessT m_col = m_row;
          AccessT h_col = h_row;
          for ( int32 c=0; c < cols; c++ ) {
            if ( is_extrema( l_col, m_col, h_col ) ) {
              scale_points.push_back(ip::InterestPoint(c+2,r+2,
                                                       m_interest.float_scale(scale-1),
                                                       *m_col) );
            }
            l_col.next_col();
            m_col.next_col();
            h_col.next_col();
          }
          l_row.next_row();
          m_row.next_row();
          h_row.next_row();
        }

        VW_OUT(DebugMessage, "interest_point") << "\tPrior to thresholding there was: " << scale_points.size() << "\n";

        // Thresholding
        threshold(scale_points, interest_data[1], scale-1);

        // Appending to the greater set
        new_points.insert(new_points.end(),
                          scale_points.begin(),
                          scale_points.end());

        // Deleting lowest
        interest_data.pop_front();
      }

      // Cull down to what we want
      if ( m_obj_points > 0 && new_points.size() > m_obj_points ) { // Cull
        VW_OUT(DebugMessage, "interest_point") << "\tCulling ...\n";
        Timer t("elapsed time", DebugMessage, "interest_point");

        int original_num_points = new_points.size();


        // Sort the interest of the points and pull out the top amount that the user wants
        new_points.sort();
        VW_OUT(DebugMessage, "interest_point") << "     Best IP : " << new_points.front().interest << std::endl;
        VW_OUT(DebugMessage, "interest_point") << "     Worst IP: " << new_points.back().interest << std::endl;
        new_points.resize( m_obj_points );

        VW_OUT(DebugMessage, "interest_point") << "     (removed " << original_num_points - new_points.size() << " interest points, " << new_points.size() << " remaining.)\n";
      } else {
        VW_OUT(DebugMessage, "interest_point") << "     Not enough IP to cull.\n";
      }

      return new_points;
    }

  protected:

    vw::ip::OBALoGInterestOperator m_interest;
    int m_scales;
    size_t m_obj_points;

    template <class AccessT>
    bool inline is_extrema( AccessT const& low,
                            AccessT const& mid,
                            AccessT const& hi ) const {
      AccessT low_o = low;
      AccessT mid_o = mid;
      AccessT hi_o  = hi;

      if ( *mid_o <= *low_o ||
           *mid_o <= *hi_o  ) return false;

      low_o.advance(-1,-1); mid_o.advance(-1,-1);hi_o.advance(-1,-1);
      if ( *mid <= *low_o ||
           *mid <= *mid_o ||
           *mid <= *hi_o ) return false;

      for ( vw::uint8 step = 1; step < 8; step++ ) {
        if ( step == 1 || step == 2 || step == 5 || step == 6 ) {
          low_o.next_col(); mid_o.next_col(); hi_o.next_col();
        } else {
          low_o.prev_row(); mid_o.prev_row(); hi_o.prev_row();
        }
        if ( *mid <= *low_o ||
             *mid <= *mid_o ||
             *mid <= *hi_o ) return false;
      }

      return true;
    }

    template <class DataT>
    inline void threshold( vw::ip::InterestPointList& points,
                           DataT const& img_data,
                           int const& scale ) const {
      vw::ip::InterestPointList::iterator pos = points.begin();
      while (pos != points.end()) {
        if (!m_interest.threshold(*pos,
                                  img_data, scale) )
          pos = points.erase(pos);
        else
          pos++;
      }
    }
  };

}

#endif//__ASP_CORE_INTEGRAL_AUTO_GAIN_DETECTOR_H__
