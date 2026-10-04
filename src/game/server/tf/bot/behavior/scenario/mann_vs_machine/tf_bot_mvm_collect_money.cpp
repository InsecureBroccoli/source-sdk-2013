//========= Copyright Valve Corporation, All rights reserved. ============//
// tf_bot_mvm_collect_money.cpp
// Collect the money robots drop in Mann vs Machine

#include "cbase.h"
#include "tf_player.h"
#include "tf_gamerules.h"
#include "entity_currencypack.h"
#include "bot/tf_bot.h"
#include "bot/behavior/scenario/mann_vs_machine/tf_bot_mvm_collect_money.h"


extern ConVar tf_bot_debug_mvm_defend;

ConVar tf_bot_mvm_collect_money_range( "tf_bot_mvm_collect_money_range", "2000", FCVAR_CHEAT, "In MvM, how far away defending Scout bots will go to collect money" );
ConVar tf_bot_mvm_collect_money_danger_range( "tf_bot_mvm_collect_money_danger_range", "400", FCVAR_CHEAT, "In MvM, defending Scout bots leave money alone while a robot is this close to it" );


// money we couldn't reach, so we don't keep trying for it
static CUtlVector< CHandle< CCurrencyPack > > s_unreachableMoneyVector;


//---------------------------------------------------------------------------------------------
static bool IsMoneyUnreachable( CCurrencyPack *money )
{
	CHandle< CCurrencyPack > hMoney( money );
	return s_unreachableMoneyVector.HasElement( hMoney );
}


//---------------------------------------------------------------------------------------------
// Return false if robots are too close to the given money to go after it
static bool IsMoneySafeToCollect( CTFBot *me, CCurrencyPack *money )
{
	CUtlVector< CTFPlayer * > enemyVector;
	CollectPlayers( &enemyVector, GetEnemyTeam( me->GetTeamNumber() ), COLLECT_ONLY_LIVING_PLAYERS );

	FOR_EACH_VEC( enemyVector, i )
	{
		if ( ( enemyVector[i]->GetAbsOrigin() - money->GetAbsOrigin() ).IsLengthLessThan( tf_bot_mvm_collect_money_danger_range.GetFloat() ) )
			return false;
	}

	return true;
}


//---------------------------------------------------------------------------------------------
CCurrencyPack *CTFBotMvMCollectMoney::FindMoneyToCollect( CTFBot *me )
{
	// forget about money that has since been collected or faded away
	FOR_EACH_VEC_BACK( s_unreachableMoneyVector, i )
	{
		if ( s_unreachableMoneyVector[i] == NULL )
		{
			s_unreachableMoneyVector.Remove( i );
		}
	}

	// red money has already been given to the team, but it can still heal us
	bool isHurt = me->GetHealth() < me->GetMaxHealth();

	CCurrencyPack *closeMoney = NULL;
	float closeRangeSq = tf_bot_mvm_collect_money_range.GetFloat() * tf_bot_mvm_collect_money_range.GetFloat();

	for ( int i=0; i<ICurrencyPackAutoList::AutoList().Count(); ++i )
	{
		CCurrencyPack *money = static_cast< CCurrencyPack* >( ICurrencyPackAutoList::AutoList()[i] );

		// claimed money is already flying toward a Scout
		if ( money->IsMarkedForDeletion() || money->IsClaimed() )
			continue;

		if ( money->IsDistributed() && !isHurt )
			continue;

		float rangeSq = ( money->GetAbsOrigin() - me->GetAbsOrigin() ).LengthSqr();
		if ( rangeSq >= closeRangeSq )
			continue;

		if ( IsMoneyUnreachable( money ) || !IsMoneySafeToCollect( me, money ) )
			continue;

		closeMoney = money;
		closeRangeSq = rangeSq;
	}

	return closeMoney;
}


//---------------------------------------------------------------------------------------------
CTFBotMvMCollectMoney::CTFBotMvMCollectMoney( CCurrencyPack *money )
{
	m_money = money;
}


//---------------------------------------------------------------------------------------------
ActionResult< CTFBot >	CTFBotMvMCollectMoney::OnStart( CTFBot *me, Action< CTFBot > *priorAction )
{
	m_path.SetMinLookAheadDistance( me->GetDesiredPathLookAheadRange() );

	if ( m_money == NULL )
	{
		return Done( "The money is gone" );
	}

	CTFBotPathCost cost( me, FASTEST_ROUTE );
	if ( !m_path.Compute( me, m_money->GetAbsOrigin(), cost ) )
	{
		s_unreachableMoneyVector.AddToTail( m_money );
		return Done( "Can't reach the money" );
	}

	// money doesn't last long - don't chase it forever
	const float giveUpTime = 10.0f;
	m_giveUpTimer.Start( giveUpTime );
	m_repathTimer.Start( RandomFloat( 0.5f, 1.0f ) );

	return Continue();
}


//---------------------------------------------------------------------------------------------
ActionResult< CTFBot >	CTFBotMvMCollectMoney::Update( CTFBot *me, float interval )
{
	if ( m_money == NULL || m_money->IsMarkedForDeletion() )
	{
		return Done( "The money has been collected" );
	}

	if ( m_money->IsClaimed() )
	{
		// a Scout's magnet is pulling it in - probably ours
		return Done( "The money is on its way" );
	}

	if ( m_giveUpTimer.IsElapsed() )
	{
		s_unreachableMoneyVector.AddToTail( m_money );
		return Done( "Taking too long to reach the money" );
	}

	if ( !IsMoneySafeToCollect( me, m_money ) )
	{
		return Done( "Robots are too close to the money" );
	}

	const CKnownEntity *threat = me->GetVisionInterface()->GetPrimaryKnownThreat();
	if ( threat && threat->IsVisibleRecently() )
	{
		// prepare to fight
		me->EquipBestWeaponForThreat( threat );
	}

	if ( tf_bot_debug_mvm_defend.GetBool() )
	{
		NDebugOverlay::Line( me->GetAbsOrigin(), m_money->GetAbsOrigin(), 255, 255, 0, true, NDEBUG_PERSIST_TILL_NEXT_SERVER );
	}

	// money bounces around after it drops, so keep our path up to date
	if ( m_repathTimer.IsElapsed() )
	{
		m_repathTimer.Start( RandomFloat( 0.5f, 1.0f ) );

		CTFBotPathCost cost( me, FASTEST_ROUTE );
		m_path.Compute( me, m_money->GetAbsOrigin(), cost );
	}

	m_path.Update( me );

	return Continue();
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CTFBot > CTFBotMvMCollectMoney::OnStuck( CTFBot *me )
{
	if ( m_money != NULL )
	{
		s_unreachableMoneyVector.AddToTail( m_money );
	}

	return TryDone( RESULT_CRITICAL, "Stuck trying to reach the money" );
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CTFBot > CTFBotMvMCollectMoney::OnMoveToFailure( CTFBot *me, const Path *path, MoveToFailureType reason )
{
	if ( m_money != NULL )
	{
		s_unreachableMoneyVector.AddToTail( m_money );
	}

	return TryDone( RESULT_CRITICAL, "Failed to reach the money" );
}
