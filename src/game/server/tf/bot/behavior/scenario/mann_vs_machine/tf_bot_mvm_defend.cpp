//========= Copyright Valve Corporation, All rights reserved. ============//
// tf_bot_mvm_defend.cpp
// Defend the bomb hatch from the robots in Mann vs Machine

#include "cbase.h"
#include "nav_mesh/tf_nav_mesh.h"
#include "tf_player.h"
#include "tf_gamerules.h"
#include "bot/tf_bot.h"
#include "bot/behavior/scenario/mann_vs_machine/tf_bot_mvm_defend.h"
#include "bot/behavior/scenario/mann_vs_machine/tf_bot_mvm_collect_money.h"
#include "bot/behavior/demoman/tf_bot_prepare_stickybomb_trap.h"


ConVar tf_bot_mvm_defend_distance_scale( "tf_bot_mvm_defend_distance_scale", "1", FCVAR_CHEAT, "In MvM, scales how much closer to the bomb hatch than the robots defending bots make their stand" );
ConVar tf_bot_debug_mvm_defend( "tf_bot_debug_mvm_defend", "0", FCVAR_CHEAT, "Show the robots' route to the bomb hatch, and where defending bots are making their stand" );


//---------------------------------------------------------------------------------------------
// Search outward for where the robots' route to the bomb hatch picks up, from somewhere that doesn't
// know its travel distance to the hatch. The distances were found by walking back from the hatch, so
// places the robots can only drop down from (ie: their spawn) don't know theirs.
class CFindMvMRouteStart : public ISearchSurroundingAreasFunctor
{
public:
	CFindMvMRouteStart( void )
	{
		m_routeArea = NULL;
		m_routeLength = FLT_MAX;
	}

	virtual bool operator() ( CNavArea *baseArea, CNavArea *priorArea, float travelDistanceSoFar )
	{
		CTFNavArea *area = (CTFNavArea *)baseArea;
		float distanceToHatch = area->GetTravelDistanceToBombTarget();

		// take the shortest way to the hatch
		if ( distanceToHatch >= 0.0f && travelDistanceSoFar + distanceToHatch < m_routeLength )
		{
			m_routeArea = area;
			m_routeLength = travelDistanceSoFar + distanceToHatch;
		}

		return true;
	}

	virtual bool ShouldSearch( CNavArea *adjArea, CNavArea *currentArea, float travelDistanceSoFar )
	{
		// once an area knows the way to the hatch, there's no need to search past it
		if ( ( (CTFNavArea *)currentArea )->GetTravelDistanceToBombTarget() >= 0.0f )
			return false;

		// search the way the robots move - through their spawn's doors (which are only closed to us),
		// and down any ledge, but not up anything too high to jump
		return currentArea->ComputeAdjacentConnectionHeightChange( adjArea ) <= TF_PLAYER_JUMP_HEIGHT;
	}

	CTFNavArea *m_routeArea;
	float m_routeLength;
};


//---------------------------------------------------------------------------------------------
// Return the nav area at the given position, or where the robots' route to the bomb hatch picks up
// from there if it doesn't know its travel distance to the hatch
static CTFNavArea *FindMvMRouteArea( const Vector &pos )
{
	CTFNavArea *area = (CTFNavArea *)TheTFNavMesh()->GetNearestNavArea( pos, false, 1000.0f );
	if ( !area )
		return NULL;

	if ( area->GetTravelDistanceToBombTarget() >= 0.0f )
		return area;

	const float searchRange = 5000.0f;
	CFindMvMRouteStart findRouteStart;
	SearchSurroundingAreas( area, findRouteStart, searchRange );

	return findRouteStart.m_routeArea;
}


//---------------------------------------------------------------------------------------------
static bool IsCloserToHatch( CTFNavArea *area, CTFNavArea *otherArea )
{
	return !otherArea || area->GetTravelDistanceToBombTarget() < otherArea->GetTravelDistanceToBombTarget();
}


//---------------------------------------------------------------------------------------------
CTFNavArea *FindMvMRobotPushArea( void )
{
	CTFNavArea *pushArea = NULL;
	CTFNavArea *homeBombArea = NULL;

	for ( int i=0; i<ICaptureFlagAutoList::AutoList().Count(); ++i )
	{
		CCaptureFlag *bomb = static_cast< CCaptureFlag* >( ICaptureFlagAutoList::AutoList()[i] );

		CTFPlayer *carrier = ToTFPlayer( bomb->GetOwnerEntity() );
		CTFNavArea *bombArea = FindMvMRouteArea( carrier ? carrier->GetAbsOrigin() : bomb->WorldSpaceCenter() );

		if ( !bombArea )
			continue;

		if ( bomb->IsHome() || bomb->IsDisabled() )
		{
			if ( IsCloserToHatch( bombArea, homeBombArea ) )
			{
				homeBombArea = bombArea;
			}
		}
		else if ( IsCloserToHatch( bombArea, pushArea ) )
		{
			pushArea = bombArea;
		}
	}

	// tanks deliver their own bomb
	CBaseEntity *tank = NULL;
	while( ( tank = gEntList.FindEntityByClassname( tank, "tank_boss" ) ) != NULL )
	{
		if ( !tank->IsAlive() )
			continue;

		CTFNavArea *tankArea = FindMvMRouteArea( tank->GetAbsOrigin() );

		if ( tankArea && IsCloserToHatch( tankArea, pushArea ) )
		{
			pushArea = tankArea;
		}
	}

	return pushArea ? pushArea : homeBombArea;
}


//---------------------------------------------------------------------------------------------
bool CollectMvMRobotRoute( CUtlVector< CTFNavArea * > *routeVector )
{
	routeVector->RemoveAll();

	// follow the route by stepping to whichever adjacent area is closest to the hatch
	CTFNavArea *area = FindMvMRobotPushArea();
	while( area )
	{
		routeVector->AddToTail( area );

		float closestDistance = area->GetTravelDistanceToBombTarget();
		CTFNavArea *nextArea = NULL;

		for( int dir=0; dir<NUM_DIRECTIONS; ++dir )
		{
			const NavConnectVector *adjVector = area->GetAdjacentAreas( (NavDirType)dir );
			FOR_EACH_VEC( (*adjVector), it )
			{
				CTFNavArea *adjArea = (CTFNavArea *)(*adjVector)[ it ].area;
				float adjDistance = adjArea->GetTravelDistanceToBombTarget();

				if ( adjDistance >= 0.0f && adjDistance < closestDistance )
				{
					closestDistance = adjDistance;
					nextArea = adjArea;
				}
			}
		}

		// every step gets closer to the hatch, so this ends when we reach it
		area = nextArea;
	}

	return routeVector->Count() > 0;
}


//---------------------------------------------------------------------------------------------
CTFNavArea *FindMvMDefenseArea( float distanceAheadOfRobots )
{
	CUtlVector< CTFNavArea * > routeVector;
	if ( !CollectMvMRobotRoute( &routeVector ) )
		return NULL;

	// measure from where the route leaves the robots' spawn, so we don't camp right at its exit
	float startDistance = -1.0f;
	CTFNavArea *defenseArea = NULL;

	FOR_EACH_VEC( routeVector, i )
	{
		CTFNavArea *area = routeVector[i];

		if ( tf_bot_debug_mvm_defend.GetBool() )
		{
			area->DrawFilled( 255, 255, 0, 50, 1.0f );
		}

		// we can't get into the robots' spawn, and there's nothing to stop in ours
		if ( area->HasAttributeTF( TF_NAV_SPAWN_ROOM_BLUE | TF_NAV_SPAWN_ROOM_RED ) )
			continue;

		float distance = area->GetTravelDistanceToBombTarget();
		if ( startDistance < 0.0f )
		{
			startDistance = distance;
		}

		// if the robots are already this close to the hatch, make our stand at the hatch itself
		defenseArea = area;

		if ( distance <= startDistance - distanceAheadOfRobots )
			break;
	}

	return defenseArea;
}


//---------------------------------------------------------------------------------------------
// How much closer to the hatch than the robots we make our stand, depending on our weapons' range
static float GetMvMDefendDistance( CTFBot *me )
{
	float distance;

	switch( me->GetPlayerClass()->GetClassIndex() )
	{
	case TF_CLASS_PYRO:			distance = 300.0f;	break;	// flamethrowers need to be close
	case TF_CLASS_SCOUT:		distance = 400.0f;	break;	// scatterguns too
	case TF_CLASS_HEAVYWEAPONS:	distance = 500.0f;	break;
	case TF_CLASS_DEMOMAN:		distance = 900.0f;	break;	// room to lay a sticky trap
	default:					distance = 800.0f;	break;
	}

	return distance * tf_bot_mvm_defend_distance_scale.GetFloat();
}


//---------------------------------------------------------------------------------------------
ActionResult< CTFBot >	CTFBotMvMDefend::OnStart( CTFBot *me, Action< CTFBot > *priorAction )
{
	m_path.SetMinLookAheadDistance( me->GetDesiredPathLookAheadRange() );

	// spread out, so we don't all crowd onto the same spot
	m_distanceAheadOfRobots = GetMvMDefendDistance( me ) + RandomFloat( -150.0f, 150.0f );

	m_defenseArea = NULL;
	m_defenseAreaTimer.Invalidate();
	m_repathTimer.Invalidate();
	m_moneySearchTimer.Invalidate();

	return Continue();
}


//---------------------------------------------------------------------------------------------
ActionResult< CTFBot >	CTFBotMvMDefend::Update( CTFBot *me, float interval )
{
	const CKnownEntity *threat = me->GetVisionInterface()->GetPrimaryKnownThreat();
	if ( threat && threat->IsVisibleRecently() )
	{
		// prepare to fight
		me->EquipBestWeaponForThreat( threat );
	}

	// Scouts collect the money the robots drop - it heals them, too
	if ( me->IsPlayerClass( TF_CLASS_SCOUT ) && m_moneySearchTimer.IsElapsed() )
	{
		m_moneySearchTimer.Start( 0.5f );

		CCurrencyPack *money = CTFBotMvMCollectMoney::FindMoneyToCollect( me );
		if ( money )
		{
			return SuspendFor( new CTFBotMvMCollectMoney( money ), "Collecting money" );
		}
	}

	// the robots keep pushing toward the hatch, so keep our stand between them and it
	if ( m_defenseAreaTimer.IsElapsed() )
	{
		m_defenseAreaTimer.Start( RandomFloat( 1.0f, 2.0f ) );

		CTFNavArea *defenseArea = FindMvMDefenseArea( m_distanceAheadOfRobots );
		if ( defenseArea && defenseArea != m_defenseArea )
		{
			m_defenseArea = defenseArea;
			m_defenseSpot = defenseArea->GetRandomPoint();
			m_repathTimer.Invalidate();
		}
	}

	if ( !m_defenseArea )
	{
		// no robots or no bomb hatch - nothing to defend
		return Continue();
	}

	if ( tf_bot_debug_mvm_defend.GetBool() )
	{
		m_defenseArea->DrawFilled( 0, 255, 0, 100, NDEBUG_PERSIST_TILL_NEXT_SERVER );
		NDebugOverlay::Line( me->GetAbsOrigin(), m_defenseSpot, 0, 255, 0, true, NDEBUG_PERSIST_TILL_NEXT_SERVER );
	}

	const float atSpotRange = 50.0f;
	if ( ( me->GetAbsOrigin() - m_defenseSpot ).AsVector2D().IsLengthGreaterThan( atSpotRange ) )
	{
		// move to where we're making our stand
		if ( m_repathTimer.IsElapsed() )
		{
			m_repathTimer.Start( RandomFloat( 1.0f, 2.0f ) );

			CTFBotPathCost cost( me, FASTEST_ROUTE );
			m_path.Compute( me, m_defenseSpot, cost );
		}

		m_path.Update( me );
	}
	else if ( CTFBotPrepareStickybombTrap::IsPossible( me ) )
	{
		return SuspendFor( new CTFBotPrepareStickybombTrap, "Laying sticky bombs!" );
	}

	return Continue();
}


//---------------------------------------------------------------------------------------------
ActionResult< CTFBot > CTFBotMvMDefend::OnResume( CTFBot *me, Action< CTFBot > *interruptingAction )
{
	// the robots have likely moved while we were busy
	m_defenseAreaTimer.Invalidate();
	m_repathTimer.Invalidate();

	return Continue();
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CTFBot > CTFBotMvMDefend::OnStuck( CTFBot *me )
{
	m_repathTimer.Invalidate();
	me->GetLocomotionInterface()->ClearStuckStatus();

	return TryContinue();
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CTFBot > CTFBotMvMDefend::OnMoveToFailure( CTFBot *me, const Path *path, MoveToFailureType reason )
{
	// try another spot
	m_defenseArea = NULL;
	m_defenseAreaTimer.Invalidate();

	return TryContinue();
}


//---------------------------------------------------------------------------------------------
static bool IsVisibleBombCarrier( const CKnownEntity *threat )
{
	if ( !threat->IsVisibleRecently() )
		return false;

	CTFPlayer *player = ToTFPlayer( threat->GetEntity() );
	return player && player->HasTheFlag();
}


//---------------------------------------------------------------------------------------------
// Return the more dangerous of the two threats to 'subject', or NULL if we have no opinion
const CKnownEntity *CTFBotMvMDefend::SelectMoreDangerousThreat( const INextBot *me, const CBaseCombatCharacter *subject, const CKnownEntity *threat1, const CKnownEntity *threat2 ) const
{
	// don't waste time on robots we can't do anything to yet (ie: ones leaving their spawn)
	bool isUnaffected1 = IsUnaffectedByAttacks( threat1->GetEntity() );
	bool isUnaffected2 = IsUnaffectedByAttacks( threat2->GetEntity() );

	if ( isUnaffected1 && !isUnaffected2 )
		return threat2;

	if ( isUnaffected2 && !isUnaffected1 )
		return threat1;

	// stopping the bomb comes first
	bool isCarrier1 = IsVisibleBombCarrier( threat1 );
	bool isCarrier2 = IsVisibleBombCarrier( threat2 );

	if ( isCarrier1 && !isCarrier2 )
		return threat1;

	if ( isCarrier2 && !isCarrier1 )
		return threat2;

	return NULL;
}
