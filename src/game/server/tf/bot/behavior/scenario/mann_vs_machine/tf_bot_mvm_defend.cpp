//========= Copyright Valve Corporation, All rights reserved. ============//
// tf_bot_mvm_defend.cpp
// Defend the bomb hatch from the robots in Mann vs Machine

#include "cbase.h"
#include "nav_mesh/tf_nav_mesh.h"
#include "tf_player.h"
#include "tf_gamerules.h"
#include "bot/tf_bot.h"
#include "bot/behavior/scenario/mann_vs_machine/tf_bot_mvm_defend.h"
#include "bot/behavior/demoman/tf_bot_prepare_stickybomb_trap.h"


ConVar tf_bot_mvm_defend_distance_scale( "tf_bot_mvm_defend_distance_scale", "1", FCVAR_CHEAT, "In MvM, scales how much closer to the bomb hatch than the robots defending bots make their stand" );
ConVar tf_bot_debug_mvm_defend( "tf_bot_debug_mvm_defend", "0", FCVAR_CHEAT, "Show the robots' route to the bomb hatch, and where defending bots are making their stand" );


//---------------------------------------------------------------------------------------------
// Return the nav area at the given position, or the closest one around it that knows its travel
// distance to the bomb hatch. Areas the robots can only drop down from (ie: their spawn) can't
// be reached by walking back from the hatch, so they don't know their distance.
static CTFNavArea *FindMvMRouteArea( const Vector &pos )
{
	CTFNavArea *area = (CTFNavArea *)TheTFNavMesh()->GetNearestNavArea( pos, false, 1000.0f );
	if ( !area )
		return NULL;

	if ( area->GetTravelDistanceToBombTarget() >= 0.0f )
		return area;

	const float searchRange = 1500.0f;
	const float maxDropDown = 1000.0f;
	CUtlVector< CNavArea * > nearbyAreaVector;
	CollectSurroundingAreas( &nearbyAreaVector, area, searchRange, StepHeight, maxDropDown );

	CTFNavArea *closeArea = NULL;
	float closeRangeSq = FLT_MAX;

	FOR_EACH_VEC( nearbyAreaVector, i )
	{
		CTFNavArea *nearbyArea = (CTFNavArea *)nearbyAreaVector[i];

		if ( nearbyArea->GetTravelDistanceToBombTarget() < 0.0f )
			continue;

		float rangeSq = ( nearbyArea->GetCenter() - pos ).LengthSqr();
		if ( rangeSq < closeRangeSq )
		{
			closeArea = nearbyArea;
			closeRangeSq = rangeSq;
		}
	}

	return closeArea;
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
	// stopping the bomb comes first
	bool isCarrier1 = IsVisibleBombCarrier( threat1 );
	bool isCarrier2 = IsVisibleBombCarrier( threat2 );

	if ( isCarrier1 && !isCarrier2 )
		return threat1;

	if ( isCarrier2 && !isCarrier1 )
		return threat2;

	return NULL;
}
