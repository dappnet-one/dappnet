#pragma once
#include <dpn/chain/dpn_fwd.hpp>
#include <appbase/application.hpp>

#include <dpn/plugins/chain/chain_plugin.hpp>
#include <dpn/plugins/block_data_export/block_data_export_plugin.hpp>

namespace dpn { namespace plugins { namespace stats_export {

namespace detail { class stats_export_plugin_impl; }

using namespace appbase;

#define DPN_STATS_EXPORT_PLUGIN_NAME "stats_export"

class stats_export_plugin : public appbase::plugin< stats_export_plugin >
{
   public:
      stats_export_plugin();
      virtual ~stats_export_plugin();

      APPBASE_PLUGIN_REQUIRES(
         (dpn::plugins::block_data_export::block_data_export_plugin)
         (dpn::plugins::chain::chain_plugin)
      )

      static const std::string& name() { static std::string name = DPN_STATS_EXPORT_PLUGIN_NAME; return name; }

      virtual void set_program_options( options_description& cli, options_description& cfg ) override;
      virtual void plugin_initialize( const variables_map& options ) override;
      virtual void plugin_startup() override;
      virtual void plugin_shutdown() override;

   private:
      std::unique_ptr< detail::stats_export_plugin_impl > my;
};

} } } // dpn::plugins::stats_export
