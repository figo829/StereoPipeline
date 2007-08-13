#include "StereoSession.h"
#include "StereoSessionPinhole.h"
#include "HRSC/StereoSessionHRSC.h"
#include "MOC/StereoSessionMOC.h"
#include "apollo/StereoSessionApolloMetric.h"
#include "clementine/StereoSessionClementine.h"
#include "MRO/StereoSessionCTX.h"

#include <vw/Core/Exception.h>

#include <map>

// This creates an anonymous namespace where the lookup table for
// stereo sessions lives.
namespace {
  typedef std::map<std::string,StereoSession::construct_func> ConstructMapType;
  ConstructMapType *stereo_session_construct_map = 0;
}


void StereoSession::register_session_type( std::string const& id,
                                           StereoSession::construct_func func) {
  if( ! stereo_session_construct_map ) stereo_session_construct_map = new ConstructMapType();
  stereo_session_construct_map->insert( std::make_pair( id, func ) );
}

static void register_default_session_types() {
  static bool already = false;
  if( already ) return;
  already = true;
  StereoSession::register_session_type( "pinhole", &StereoSessionPinhole::construct);
  StereoSession::register_session_type( "hrsc", &StereoSessionHRSC::construct);
  StereoSession::register_session_type( "moc", &StereoSessionMOC::construct);
  StereoSession::register_session_type( "metric", &StereoSessionApolloMetric::construct);
  StereoSession::register_session_type( "clementine", &StereoSessionClementine::construct);
  StereoSession::register_session_type( "ctx", &StereoSessionCTX::construct);
}

StereoSession* StereoSession::create( std::string const& session_type ) {
  register_default_session_types();
  if( stereo_session_construct_map ) {
    ConstructMapType::const_iterator i = stereo_session_construct_map->find( session_type );
    if( i != stereo_session_construct_map->end() ) 
      return i->second();
  }
  vw_throw( vw::NoImplErr() << "Unsuppported stereo session type: " << session_type );
  return 0; // never reached
}
