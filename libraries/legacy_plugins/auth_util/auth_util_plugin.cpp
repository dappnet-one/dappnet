

#include <dpn/plugins/auth_util/auth_util_api.hpp>
#include <dpn/plugins/auth_util/auth_util_plugin.hpp>

#include <string>

namespace dpn { namespace plugin { namespace auth_util {

auth_util_plugin::auth_util_plugin( application* app ) : plugin( app ) {}
auth_util_plugin::~auth_util_plugin() {}

std::string auth_util_plugin::plugin_name()const
{
   return "auth_util";
}

void auth_util_plugin::plugin_initialize( const boost::program_options::variables_map& options )
{
}

void auth_util_plugin::plugin_startup()
{
   app().register_api_factory< auth_util_api >( "auth_util_api" );
}

void auth_util_plugin::plugin_shutdown()
{
}

} } } // dpn::plugin::auth_util

DPN_DEFINE_PLUGIN( auth_util, dpn::plugin::auth_util::auth_util_plugin )
