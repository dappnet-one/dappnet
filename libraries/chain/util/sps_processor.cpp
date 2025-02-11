#include <dpn/chain/util/sps_processor.hpp>

namespace dpn { namespace chain {

using dpn::protocol::asset;
using dpn::protocol::operation;

using dpn::chain::proposal_object;
using dpn::chain::by_start_date;
using dpn::chain::by_end_date;
using dpn::chain::proposal_index;
using dpn::chain::proposal_id_type;
using dpn::chain::proposal_vote_index;
using dpn::chain::by_proposal_voter;
using dpn::chain::by_voter_proposal;
using dpn::protocol::proposal_pay_operation;
using dpn::chain::sps_helper;
using dpn::chain::dynamic_global_property_object;
using dpn::chain::block_notification;

const std::string sps_processor::removing_name = "sps_processor_remove";
const std::string sps_processor::calculating_name = "sps_processor_calculate";

bool sps_processor::is_maintenance_period( const time_point_sec& head_time ) const
{
   return db.get_dynamic_global_properties().next_maintenance_time <= head_time;
}

void sps_processor::remove_proposals( const time_point_sec& head_time )
{
   FC_TODO("implement proposal removal based on automatic actions")
   auto& proposalIndex = db.get_mutable_index< proposal_index >();
   auto& byEndDateIdx = proposalIndex.indices().get< by_end_date >();

   auto& votesIndex = db.get_mutable_index< proposal_vote_index >();
   auto& byVoterIdx = votesIndex.indices().get< by_proposal_voter >();

   auto found = byEndDateIdx.upper_bound( head_time );
   auto itr = byEndDateIdx.begin();

   sps_removing_reducer obj_perf( db.get_sps_remove_threshold() );

   while( itr != found )
   {
      itr = sps_helper::remove_proposal< by_end_date >( itr, proposalIndex, votesIndex, byVoterIdx, obj_perf );
      if( obj_perf.done )
         break;
   }
}

void sps_processor::find_active_proposals( const time_point_sec& head_time, t_proposals& proposals )
{
   const auto& pidx = db.get_index< proposal_index >().indices().get< by_start_date >();

   std::for_each( pidx.begin(), pidx.upper_bound( head_time ), [&]( auto& proposal )
                                             {
                                                if( head_time >= proposal.start_date && head_time <= proposal.end_date )
                                                   proposals.emplace_back( proposal );
                                             } );
}

uint64_t sps_processor::calculate_votes( const proposal_id_type& id )
{
   uint64_t ret = 0;

   const auto& pvidx = db.get_index< proposal_vote_index >().indices().get< by_proposal_voter >();
   auto found = pvidx.find( id );

   while( found != pvidx.end() && found->proposal_id == id )
   {
      const auto& _voter = db.get_account( found->voter );

      //If _voter has set proxy, then his votes aren't taken into consideration
      if( _voter.proxy == DPN_PROXY_TO_SELF_ACCOUNT )
      {
         auto sum = _voter.witness_vote_weight();
         ret += sum.value;
      }

      ++found;
   }

   return ret;
}

void sps_processor::calculate_votes( const t_proposals& proposals )
{
   for( auto& item : proposals )
   {
      const proposal_object& _item = item;
      auto total_votes = calculate_votes( _item.proposal_id );

      db.modify( _item, [&]( auto& proposal )
                        {
                           proposal.total_votes = total_votes;
                        } );
   }
}

void sps_processor::sort_by_votes( t_proposals& proposals )
{
   std::sort( proposals.begin(), proposals.end(), []( const proposal_object& a, const proposal_object& b )
                                                      {
                                                         if (a.total_votes == b.total_votes)
                                                         {
                                                            return a.proposal_id < b.proposal_id;
                                                         }
                                                         return a.total_votes > b.total_votes;
                                                      } );
}

asset sps_processor::get_treasury_fund()
{
   auto& treasury_account = db.get_account( DPN_TREASURY_ACCOUNT );

   return treasury_account.dbd_balance;
}

asset sps_processor::get_daily_inflation()
{
   FC_TODO( "to invent how to get inflation needed for DPN_TREASURY_ACCOUNT" )
   return asset( 0, DBD_SYMBOL );
}

asset sps_processor::calculate_maintenance_budget( const time_point_sec& head_time )
{
   //Get funds from 'treasury' account ( treasury_fund )
   asset treasury_fund = get_treasury_fund();

   //Get daily proposal inflation ( daily_proposal_inflation )
   asset daily_inflation = get_daily_inflation();

   FC_ASSERT( treasury_fund.symbol == daily_inflation.symbol, "symbols must be the same" );

   //Calculate budget for given maintenance period
   uint32_t passed_time_seconds = ( head_time - db.get_dynamic_global_properties().last_budget_time ).to_seconds();

   //Calculate daily_budget_limit
   int64_t daily_budget_limit = treasury_fund.amount.value / total_amount_divider + daily_inflation.amount.value;

   daily_budget_limit = ( ( uint128_t( passed_time_seconds ) * daily_budget_limit ) / daily_seconds ).to_uint64();

   //Transfer daily_proposal_inflation to `treasury account`
   transfer_daily_inflation_to_treasury( daily_inflation );

   return asset( daily_budget_limit, treasury_fund.symbol );
}

void sps_processor::transfer_daily_inflation_to_treasury( const asset& daily_inflation )
{
   /*
      Now `daily_inflation` is always zero. Take a look at `get_daily_inflation`

      Comment from Michael Vandeberg:

         Is this printing new DBD?

         That's not how we have handled inflation in the past.
         Either inflation should be paid, per block,
         in to the treasury account in database::process_funds or added to a temp fund in the dgpo
         that is then transferred in to the treasury account during maintenance.
   */
   FC_TODO( "to choose how to transfer inflation into DPN_TREASURY_ACCOUNT" )
   // Ifdeffing this out so that no inflation is accidentally created on main net.
#ifdef IS_TEST_NET
   if( daily_inflation.amount.value > 0 )
   {
      const auto& treasury_account = db.get_account( DPN_TREASURY_ACCOUNT );
      db.adjust_balance( treasury_account, daily_inflation );
   }
#endif
}

void sps_processor::transfer_payments( const time_point_sec& head_time, asset& maintenance_budget_limit, const t_proposals& proposals )
{
   if( maintenance_budget_limit.amount.value == 0 )
      return;

   const auto& treasury_account = db.get_account(DPN_TREASURY_ACCOUNT);

   uint32_t passed_time_seconds = ( head_time - db.get_dynamic_global_properties().last_budget_time ).to_seconds();
   uint128_t ratio = ( passed_time_seconds * DPN_100_PERCENT ) / daily_seconds;

   auto processing = [this, &treasury_account]( const proposal_object& _item, const asset& payment )
   {
      const auto& receiver_account = db.get_account( _item.receiver );

      operation vop = proposal_pay_operation( _item.receiver, payment, db.get_current_trx(), db.get_current_op_in_trx() );
      /// Push vop to be recorded by other parts (like AH plugin etc.)
      db.push_virtual_operation(vop);
      /// Virtual ops have no evaluators, so operation must be immediately "evaluated"
      db.adjust_balance( treasury_account, -payment );
      db.adjust_balance( receiver_account, payment );
   };

   for( auto& item : proposals )
   {
      const proposal_object& _item = item;

      //Proposals without any votes shouldn't be treated as active
      if( _item.total_votes == 0 )
         break;

      asset period_pay = asset( ( ratio * _item.daily_pay.amount.value ).to_uint64() / DPN_100_PERCENT, _item.daily_pay.symbol );

      if( period_pay >= maintenance_budget_limit )
      {
         processing( _item, maintenance_budget_limit );
         break;
      }
      else
      {
         processing( _item, period_pay );
         maintenance_budget_limit -= period_pay;
      }
   }
}

void sps_processor::update_settings( const time_point_sec& head_time )
{
   db.modify( db.get_dynamic_global_properties(), [&]( dynamic_global_property_object& _dgpo )
                                          {
                                             _dgpo.next_maintenance_time = head_time + fc::seconds( DPN_PROPOSAL_MAINTENANCE_PERIOD );
                                             _dgpo.last_budget_time = head_time;
                                          } );
}

void sps_processor::remove_old_proposals( const block_notification& note )
{
   auto head_time = note.block.timestamp;

   if( db.get_benchmark_dumper().is_enabled() )
      db.get_benchmark_dumper().begin();

   remove_proposals( head_time );

   if( db.get_benchmark_dumper().is_enabled() )
      db.get_benchmark_dumper().end( sps_processor::removing_name );
}

void sps_processor::make_payments( const block_notification& note )
{
   auto head_time = note.block.timestamp;

   //Check maintenance period
   if( !is_maintenance_period( head_time ) )
      return;

   if( db.get_benchmark_dumper().is_enabled() )
      db.get_benchmark_dumper().begin();

   t_proposals active_proposals;

   //Find all active proposals, where actual_time >= start_date and actual_time <= end_date
   find_active_proposals( head_time, active_proposals );
   if( active_proposals.empty() )
   {
      if( db.get_benchmark_dumper().is_enabled() )
         db.get_benchmark_dumper().end( sps_processor::calculating_name );

      //Set `new maintenance time` and `last budget time`
      update_settings( head_time );
      return;
   }

   //Calculate total_votes for every active proposal
   calculate_votes( active_proposals );

   //Sort all active proposals by total_votes
   sort_by_votes( active_proposals );

   //Calculate budget for given maintenance period
   asset maintenance_budget_limit = calculate_maintenance_budget( head_time );

   //Execute transfer for every active proposal
   transfer_payments( head_time, maintenance_budget_limit, active_proposals );

   //Set `new maintenance time` and `last budget time`
   update_settings( head_time );

   if( db.get_benchmark_dumper().is_enabled() )
      db.get_benchmark_dumper().end( sps_processor::calculating_name );
}

const std::string& sps_processor::get_removing_name()
{
   return removing_name;
}

const std::string& sps_processor::get_calculating_name()
{
   return calculating_name;
}

void sps_processor::run( const block_notification& note )
{
   remove_old_proposals( note );
   record_funding( note );
   make_payments( note );
}

void sps_processor::record_funding( const block_notification& note )
{
   if( !is_maintenance_period( note.block.timestamp ) )
      return;

   const auto& props = db.get_dynamic_global_properties();

   if ( props.sps_interval_ledger.amount.value <= 0 )
      return;

   operation vop = sps_fund_operation( props.sps_interval_ledger );
   db.push_virtual_operation( vop );

   db.modify( props, []( dynamic_global_property_object& dgpo )
   {
      dgpo.sps_interval_ledger = asset( 0, DBD_SYMBOL );
   });
}

} } // namespace dpn::chain
