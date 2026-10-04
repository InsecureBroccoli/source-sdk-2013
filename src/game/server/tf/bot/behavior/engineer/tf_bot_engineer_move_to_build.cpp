//========= Copyright Valve Corporation, All rights reserved. ============//
// tf_bot_engineer_move_to_build.cpp
// Engineer moving into position to build
// Michael Booth, February 2009

#include "cbase.h"
#include "nav_mesh/tf_nav_mesh.h"
#include "tf_player.h"
#include "tf_gamerules.h"
#include "tf_obj_sentrygun.h"
#include "tf_weapon_builder.h"
#include "team_train_watcher.h"
#include "bot/tf_bot.h"
#include "bot/behavior/engineer/tf_bot_engineer_build.h"
#include "bot/behavior/engineer/tf_bot_engineer_move_to_build.h"
#include "bot/behavior/engineer/tf_bot_engineer_building.h"
#include "bot/map_entities/tf_bot_hint_sentrygun.h"
#include "bot/behavior/tf_bot_get_ammo.h"
#include "bot/behavior/tf_bot_retreat_to_cover.h"
#include "bot/behavior/engineer/tf_bot_engineer_build_teleport_exit.h"
#include "bot/behavior/scenario/mann_vs_machine/tf_bot_mvm_defend.h"
#include "trigger_area_capture.h"

#include "raid/tf_raid_logic.h"


extern ConVar tf_bot_path_lookahead_range;

ConVar tf_bot_debug_sentry_placement( "tf_bot_debug_sentry_placement", "0", FCVAR_CHEAT );
ConVar tf_bot_max_teleport_exit_travel_to_point( "tf_bot_max_teleport_exit_travel_to_point", "2500", FCVAR_CHEAT, "In an offensive engineer bot's tele exit is farther from the point than this, destroy it" );
ConVar tf_bot_min_teleport_travel( "tf_bot_min_teleport_travel", "3000", FCVAR_CHEAT, "Minimum travel distance between teleporter entrance and exit before engineer bot will build one" );
ConVar tf_bot_engineer_build_behind_cart_min( "tf_bot_engineer_build_behind_cart_min", "300", FCVAR_CHEAT, "On payload, engineer bots pushing the cart build at least this far behind it, along the track" );
ConVar tf_bot_engineer_build_behind_cart_max( "tf_bot_engineer_build_behind_cart_max", "700", FCVAR_CHEAT, "On payload, engineer bots pushing the cart build at most this far behind it, along the track" );
ConVar tf_bot_engineer_move_up_behind_cart( "tf_bot_engineer_move_up_behind_cart", "1000", FCVAR_CHEAT, "On payload, engineer bots pushing the cart move their nest up once it's this far behind the cart (always at least 100 more than tf_bot_engineer_build_behind_cart_max)" );
ConVar tf_bot_engineer_mvm_build_min( "tf_bot_engineer_mvm_build_min", "0.25", FCVAR_CHEAT, "In MvM, engineer bots cover the robots' route to the bomb hatch starting at this fraction of the way from the hatch to the robots" );
ConVar tf_bot_engineer_mvm_build_max( "tf_bot_engineer_mvm_build_max", "0.5", FCVAR_CHEAT, "In MvM, engineer bots cover the robots' route to the bomb hatch up to this fraction of the way from the hatch to the robots, and never build farther out" );

//--------------------------------------------------------------------------------------------------------
static Vector s_pointCentroid;

int CompareRangeToPoint( CTFNavArea * const *area1, CTFNavArea * const *area2 )
{
	float d1 = ( (*area1)->GetCenter() - s_pointCentroid ).LengthSqr();
	float d2 = ( (*area2)->GetCenter() - s_pointCentroid ).LengthSqr();

	// reversed so farthest is sorted first in the vector
	if ( d1 < d2 )
		return 1;

	if ( d1 > d2 )
		return -1;

	return 0;
}


//---------------------------------------------------------------------------------------------
// Collect areas a little behind the cart we're pushing and near the track, so our teammates pushing it
// are between our nest and the enemy while we build. Return false if there aren't any.
bool CTFBotEngineerMoveToBuild::CollectBuildAreasBehindCart( CTFBot *me, CTeamTrainWatcher *trainWatcher )
{
	CBaseEntity *cart = trainWatcher->GetTrainEntity();
	if ( !cart )
		return false;

	// the cart's origin can be inside the cart, which fails line of sight and ground checks
	CNavArea *cartArea = TheTFNavMesh()->GetNearestNavArea( cart->GetAbsOrigin(), false, 500.0f, false, false );
	if ( !cartArea )
		return false;

	const float maxTrackDistance = 750.0f;		// stay near the track, so our teammates pushing the cart can use our nest
	const float trackClearance = 100.0f;		// but off of it, out of the way of the cart and our teammates
	const float sentryEyeHeight = 60.0f;

	float cartDistance = trainWatcher->GetTrainDistanceAlongTrack();
	float minBehind = tf_bot_engineer_build_behind_cart_min.GetFloat();
	float maxBehind = tf_bot_engineer_build_behind_cart_max.GetFloat();

	CUtlVector< CNavArea * > nearbyAreaVector;
	CollectSurroundingAreas( &nearbyAreaVector, cartArea, maxBehind + maxTrackDistance );

	for( int pass=0; pass<2 && m_sentryAreaVector.Count() == 0; ++pass )
	{
		// if nothing fits (ie: the cart is at the start of the track), accept spots on the track or without
		// a view of it, as close as right beside the cart - but never ahead of it
		bool isRelaxed = ( pass > 0 );
		float minBehindThisPass = isRelaxed ? 0.0f : minBehind;

		for( int i=0; i<nearbyAreaVector.Count(); ++i )
		{
			CTFNavArea *area = (CTFNavArea *)nearbyAreaVector[i];

			// nothing can be built in a spawn room
			if ( area->HasAttributeTF( TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_BLUE ) )
				continue;

			Vector onTrack;
			float alongTrack;
			trainWatcher->ProjectPointOntoPath( area->GetCenter(), &onTrack, &alongTrack );

			float behindCart = cartDistance - alongTrack;
			if ( behindCart < minBehindThisPass || behindCart > maxBehind )
				continue;

			float trackDistance = ( area->GetCenter() - onTrack ).AsVector2D().Length();
			if ( trackDistance > maxTrackDistance )
				continue;

			if ( !isRelaxed )
			{
				if ( trackDistance < trackClearance )
					continue;

				// our sentry should cover the track beside our nest
				if ( !me->IsLineOfFireClear( area->GetCenter() + Vector( 0, 0, sentryEyeHeight ), onTrack + Vector( 0, 0, sentryEyeHeight ) ) )
					continue;
			}

			m_sentryAreaVector.AddToTail( area );
		}
	}

	return m_sentryAreaVector.Count() > 0;
}


//---------------------------------------------------------------------------------------------
// On payload, how far behind the cart we're pushing our nest can get before we move it up
float CTFBotEngineerMoveToBuild::GetMoveUpDistanceBehindCart( void )
{
	// never so close to our build range that we'd move up from a spot we just picked
	return MAX( tf_bot_engineer_move_up_behind_cart.GetFloat(), tf_bot_engineer_build_behind_cart_max.GetFloat() + 100.0f );
}


//---------------------------------------------------------------------------------------------
// In MvM, collect areas beside the robots' route to the bomb hatch, partway between the robots and
// the hatch, that our sentry can cover the route from. Return false if there aren't any.
bool CTFBotEngineerMoveToBuild::CollectBuildAreasForMvM( CTFBot *me )
{
	CUtlVector< CTFNavArea * > routeVector;
	if ( !CollectMvMRobotRoute( &routeVector ) )
		return false;

	float robotsFromHatch = routeVector[0]->GetTravelDistanceToBombTarget();
	float minFromHatch = tf_bot_engineer_mvm_build_min.GetFloat() * robotsFromHatch;
	float maxFromHatch = tf_bot_engineer_mvm_build_max.GetFloat() * robotsFromHatch;

	// collect the part of the route we want our sentry to cover
	CUtlVector< CTFNavArea * > coverVector;
	CTFNavArea *closestToRangeArea = NULL;
	float closestToRange = FLT_MAX;

	FOR_EACH_VEC( routeVector, i )
	{
		CTFNavArea *area = routeVector[i];

		if ( area->HasAttributeTF( TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_BLUE ) )
			continue;

		float fromHatch = area->GetTravelDistanceToBombTarget();

		if ( fromHatch >= minFromHatch && fromHatch <= maxFromHatch )
		{
			coverVector.AddToTail( area );
		}

		float outOfRange = MAX( minFromHatch - fromHatch, fromHatch - maxFromHatch );
		if ( outOfRange < closestToRange )
		{
			closestToRangeArea = area;
			closestToRange = outOfRange;
		}
	}

	if ( coverVector.Count() == 0 )
	{
		// large nav areas can step right over that part of the route - cover whatever is closest to it
		if ( !closestToRangeArea )
			return false;

		coverVector.AddToTail( closestToRangeArea );
	}

	// never set up farther out than the part of the route we're covering
	float maxBuildFromHatch = 0.0f;
	FOR_EACH_VEC( coverVector, i )
	{
		maxBuildFromHatch = MAX( maxBuildFromHatch, coverVector[i]->GetTravelDistanceToBombTarget() );
	}

	const float maxCoverRange = 800.0f;			// well within our sentry's range
	const float routeClearance = 100.0f;		// out of the robots' way, since giants destroy dispensers they bump into
	const float sentryEyeHeight = 60.0f;
	const float maxDropDown = 200.0f;

	CUtlVector< CNavArea * > nearbyAreaVector;
	FOR_EACH_VEC( coverVector, i )
	{
		CUtlVector< CNavArea * > nearCoverVector;
		CollectSurroundingAreas( &nearCoverVector, coverVector[i], maxCoverRange, TF_PLAYER_JUMP_HEIGHT, maxDropDown );

		FOR_EACH_VEC( nearCoverVector, j )
		{
			if ( !nearbyAreaVector.HasElement( nearCoverVector[j] ) )
			{
				nearbyAreaVector.AddToTail( nearCoverVector[j] );
			}
		}
	}

	for( int pass=0; pass<2 && m_sentryAreaVector.Count() == 0; ++pass )
	{
		// if nothing fits, accept spots on the route or without a view of it
		bool isRelaxed = ( pass > 0 );

		FOR_EACH_VEC( nearbyAreaVector, i )
		{
			CTFNavArea *area = (CTFNavArea *)nearbyAreaVector[i];

			// nothing can be built in a spawn room
			if ( area->HasAttributeTF( TF_NAV_SPAWN_ROOM_RED | TF_NAV_SPAWN_ROOM_BLUE ) )
				continue;

			float fromHatch = area->GetTravelDistanceToBombTarget();
			if ( fromHatch < 0.0f || fromHatch > maxBuildFromHatch )
				continue;

			if ( !isRelaxed )
			{
				if ( routeVector.HasElement( area ) )
					continue;

				// find the part of the route we'd cover from here
				CTFNavArea *coverArea = NULL;
				float coverRange = FLT_MAX;
				FOR_EACH_VEC( coverVector, c )
				{
					float range = ( coverVector[c]->GetCenter() - area->GetCenter() ).Length();
					if ( range < coverRange )
					{
						coverArea = coverVector[c];
						coverRange = range;
					}
				}

				if ( coverRange < routeClearance || coverRange > maxCoverRange )
					continue;

				// our sentry needs a clear shot at the robots walking by
				if ( !me->IsLineOfFireClear( area->GetCenter() + Vector( 0, 0, sentryEyeHeight ), coverArea->GetCenter() + Vector( 0, 0, sentryEyeHeight ) ) )
					continue;
			}

			m_sentryAreaVector.AddToTail( area );
		}
	}

	return m_sentryAreaVector.Count() > 0;
}


//---------------------------------------------------------------------------------------------
void CTFBotEngineerMoveToBuild::ComputeTotalSurfaceArea( void )
{
	m_totalSurfaceArea = 0.0f;
	FOR_EACH_VEC( m_sentryAreaVector, it )
	{
		CTFNavArea *area = m_sentryAreaVector[ it ];

		m_totalSurfaceArea += area->GetSizeX() * area->GetSizeY();

		if ( tf_bot_debug_sentry_placement.GetBool() )
		{
			TheNavMesh->AddToSelectedSet( area );
		}
	}
}


//---------------------------------------------------------------------------------------------
void CTFBotEngineerMoveToBuild::CollectBuildAreas( CTFBot *me )
{
	// if we have a predesignated build area, we're done
	if ( me->GetHomeArea() )
		return;

	m_sentryAreaVector.RemoveAll();

	if ( TFGameRules()->IsMannVsMachineMode() )
	{
		// cover the robots' route to the bomb hatch
		CollectBuildAreasForMvM( me );

		if ( tf_bot_debug_sentry_placement.GetBool() )
		{
			Msg( "%s: %d places to build along the robots' route\n", me->GetPlayerName(), m_sentryAreaVector.Count() );
		}

		ComputeTotalSurfaceArea();
		return;
	}

	CUtlVector< CTFNavArea * > pointAreaVector;
	Vector pointCentroid = vec3_origin;
	float pointEnemyIncursion = 0.0f;
	int i;

	int myTeam = me->GetTeamNumber();
	int enemyTeam = ( myTeam == TF_TEAM_BLUE ) ? TF_TEAM_RED : TF_TEAM_BLUE;

	CCaptureZone *zone = me->GetFlagCaptureZone();
	if ( zone )
	{
		// NOTE: Not strictly the right thing - should defend location of our team's flag
		CTFNavArea *zoneArea = (CTFNavArea *)TheTFNavMesh()->GetNearestNavArea( zone->WorldSpaceCenter(), false, 500.0f, true );
		if ( zoneArea )
		{
			pointAreaVector.AddToTail( zoneArea );
			pointCentroid += zoneArea->GetCenter();
			pointEnemyIncursion += zoneArea->GetIncursionDistance( enemyTeam );
		}
	}
	else if ( TFGameRules()->GetGameType() == TF_GAMETYPE_ESCORT )
	{
		CTeamTrainWatcher *trainWatcher;

		if ( myTeam == TF_TEAM_BLUE )
		{
			trainWatcher = TFGameRules()->GetPayloadToPush( me->GetTeamNumber() );

			// set up near the cart instead of at the next checkpoint, which is in the middle of the enemy's defense
			if ( trainWatcher && CollectBuildAreasBehindCart( me, trainWatcher ) )
			{
				if ( tf_bot_debug_sentry_placement.GetBool() )
				{
					Msg( "%s: %d places to build behind the cart\n", me->GetPlayerName(), m_sentryAreaVector.Count() );
				}

				ComputeTotalSurfaceArea();
				return;
			}

			if ( tf_bot_debug_sentry_placement.GetBool() )
			{
				Msg( "%s: No place to build behind the cart - building near the next checkpoint instead\n", me->GetPlayerName() );
			}
		}
		else
		{
			trainWatcher = TFGameRules()->GetPayloadToBlock( me->GetTeamNumber() );
		}

		if ( trainWatcher )
		{
			Vector checkpointPos = trainWatcher->GetNextCheckpointPosition();

			CTFNavArea *checkpointArea = (CTFNavArea *)TheTFNavMesh()->GetNearestNavArea( checkpointPos, false, 500.0f, true );
			if ( checkpointArea )
			{
				pointAreaVector.AddToTail( checkpointArea );
				pointCentroid += checkpointArea->GetCenter();
				pointEnemyIncursion += checkpointArea->GetIncursionDistance( enemyTeam );
			}
		}
	}
	else
	{
		// collect all areas overlapping the point
		CTeamControlPoint *ctrlPoint = me->GetMyControlPoint();
		if ( !ctrlPoint )
			return;

		const CUtlVector< CTFNavArea * > *ctrlPointAreaVector = TheTFNavMesh()->GetControlPointAreas( ctrlPoint->GetPointIndex() );

		if ( ctrlPointAreaVector )
		{
			for( i=0; i<ctrlPointAreaVector->Count(); ++i )
			{
				CTFNavArea *area = ctrlPointAreaVector->Element(i);

				pointAreaVector.AddToTail( area );
				pointCentroid += area->GetCenter();
				pointEnemyIncursion += area->GetIncursionDistance( enemyTeam );
			}
		}
	}

	if ( pointAreaVector.Count() == 0 )
		return;

	pointCentroid /= pointAreaVector.Count();
	pointEnemyIncursion /= pointAreaVector.Count();


	// collect all areas that can see the point
	CUtlVector< CTFNavArea * > exposedAreaVector;
	for( i=0; i<pointAreaVector.Count(); ++i )
	{
		CTFAreaCollector collect;
		pointAreaVector[i]->ForAllPotentiallyVisibleAreas( collect );

		for( int j=0; j<collect.m_vector.Count(); ++j )
		{
			CTFNavArea *visibleArea = collect.m_vector[j];


			if ( visibleArea->GetIncursionDistance( myTeam ) < 0 || visibleArea->GetIncursionDistance( enemyTeam ) < 0 )
				continue;

			if ( TFGameRules()->IsInKothMode() )
			{
				// ignore areas the enemy can reach first
				if ( visibleArea->GetIncursionDistance( myTeam ) >= visibleArea->GetIncursionDistance( enemyTeam ) )
					continue;
			}

// incursion flow is badly behaved at cap #1, stage #2 in dustbowl
// 			else
// 			{
// 				if ( pointEnemyIncursion > visibleArea->GetIncursionDistance( enemyTeam ) )
// 					continue;
// 			}

			if ( TFGameRules()->GetGameType() == TF_GAMETYPE_CP )
			{
				// don't build directly on the point
				if ( visibleArea->HasAttributeTF( TF_NAV_CONTROL_POINT ) )
					continue;

				// ignore areas below the point
				const float tooFarBelow = 150.0f;
				if ( visibleArea->GetCenter().z < pointCentroid.z - tooFarBelow )
					continue;

				// ignore areas too far from the point for the sentry gun to reach
				const float tolerance = 1.1f;
				if ( ( visibleArea->GetCenter() - pointCentroid ).IsLengthGreaterThan( SENTRY_MAX_RANGE * tolerance ) )
					continue;
			}

			// ignore areas that don't have clear line of FIRE (not sight)
			const float sentryEyeHeight = 60.0f;
			const float pointFlagHeight = 70.0f; // 100.0f;
			if ( !me->IsLineOfFireClear( visibleArea->GetCenter() + Vector( 0, 0, sentryEyeHeight ), pointCentroid + Vector( 0, 0, pointFlagHeight ) ) )
				continue;

			if ( !exposedAreaVector.HasElement( visibleArea ) )
				exposedAreaVector.AddToTail( visibleArea );
		}
	}

	// keep the farthest away areas
	const float keepRatio = 1.0f; // 0.5f;
	s_pointCentroid = pointCentroid;
	exposedAreaVector.Sort( CompareRangeToPoint );

	for( i=0; i<exposedAreaVector.Count() * keepRatio; ++i )
	{
		CTFNavArea *usableArea = exposedAreaVector[i];

		m_sentryAreaVector.AddToTail( usableArea );
	}

	ComputeTotalSurfaceArea();
}


//---------------------------------------------------------------------------------------------
/**
 * Doesn't recompute the potential areas, just reselected from the list
 */
void CTFBotEngineerMoveToBuild::SelectBuildLocation( CTFBot *me )
{
	m_path.Invalidate();

	m_sentryBuildHint = NULL;
	m_sentryBuildLocation = vec3_origin;


	// if we have a build spot, use it
	if ( me->GetHomeArea() )
	{
		m_sentryBuildLocation = me->GetHomeArea()->GetCenter();
		return;
	}

	// if we have a set of specific build locations, pick one of them
	CUtlVector< CTFBotHintSentrygun * > sentryHintVector;

	CTFBotHintSentrygun *sentryHint;
	for( sentryHint = static_cast< CTFBotHintSentrygun * >( gEntList.FindEntityByClassname( NULL, "bot_hint_sentrygun" ) );
		 sentryHint;
		 sentryHint = static_cast< CTFBotHintSentrygun * >( gEntList.FindEntityByClassname( sentryHint, "bot_hint_sentrygun" ) ) )
	{
		// clear the previous owner if it is us
		if ( sentryHint->GetPlayerOwner() == me )
		{
			sentryHint->SetPlayerOwner( NULL );
		}
		if ( sentryHint->IsAvailableForSelection( me ) )
		{
			sentryHintVector.AddToTail( sentryHint );
		}
	}

	if ( sentryHintVector.Count() > 0 )
	{
		int which = RandomInt( 0, sentryHintVector.Count()-1 );

		m_sentryBuildHint = sentryHintVector[ which ];
		m_sentryBuildHint->SetPlayerOwner( me );
		m_sentryBuildLocation = m_sentryBuildHint->GetAbsOrigin();

		return;
	}


	// collect nav area candidates
	CollectBuildAreas( me );

	// choose based on surface area to avoid biasing finely subdivided areas of the mesh
	float which = RandomFloat( 0.0f, m_totalSurfaceArea - 1.0f );
	float soFar = 0.0f;
	FOR_EACH_VEC( m_sentryAreaVector, sit )
	{
		CTFNavArea *area = m_sentryAreaVector[ sit ];

		soFar += area->GetSizeX() * area->GetSizeY();

		if ( which < soFar )
		{
			m_sentryBuildLocation = area->GetRandomPoint();
			return;
		}
	}

	if ( !HushAsserts() )
	{
		Assert( !"Failed to find a build location" );
	}
	m_sentryBuildLocation = me->GetAbsOrigin();
}


//---------------------------------------------------------------------------------------------
ActionResult< CTFBot >	CTFBotEngineerMoveToBuild::OnStart( CTFBot *me, Action< CTFBot > *priorAction )
{
	m_path.SetMinLookAheadDistance( me->GetDesiredPathLookAheadRange() );

#ifdef TF_RAID_MODE
	if ( TFGameRules()->IsRaidMode() )
	{
		if ( me->GetHomeArea() && TFGameRules()->GetRaidLogic() )
		{
			// try to pick a new area
			CTFNavArea *sentryArea = TFGameRules()->GetRaidLogic()->SelectRaidSentryArea();
			if ( sentryArea )
			{
				me->SetHomeArea( sentryArea );
			}
		}
	}
#endif // TF_RAID_MODE

	SelectBuildLocation( me );

	return Continue();
}


//---------------------------------------------------------------------------------------------
ActionResult< CTFBot >	CTFBotEngineerMoveToBuild::Update( CTFBot *me, float interval )
{
	if ( m_fallBackTimer.HasStarted() )
	{
		if ( m_fallBackTimer.IsElapsed() )
		{
			SelectBuildLocation( me );
			m_fallBackTimer.Invalidate();
		}
		else
		{
			// wait a moment while we decide where to build near fallback point
			return Continue();
		}
	}

	CBaseObject	*mySentry = me->GetObjectOfType( OBJ_SENTRYGUN );
	if ( mySentry )
	{
		// we already have a sentry from a previous life - continue what we were doing

		// if we used a sentry hint last time, reuse it
		CTFBotHintSentrygun *sentryHint;
		for( sentryHint = static_cast< CTFBotHintSentrygun * >( gEntList.FindEntityByClassname( NULL, "bot_hint_sentrygun" ) );
			 sentryHint;
			 sentryHint = static_cast< CTFBotHintSentrygun * >( gEntList.FindEntityByClassname( sentryHint, "bot_hint_sentrygun" ) ) )
		{
			if ( sentryHint->GetPlayerOwner() == me )
			{
				return ChangeTo( new CTFBotEngineerBuilding( sentryHint ), "Going back to my existing sentry nest and reusing a sentry hint" );
			}
		}

		return ChangeTo( new CTFBotEngineerBuilding, "Going back to my existing sentry nest" );
	}

	// offensive engineers need to place a forward teleporter
	// (on payload our nest stays close to the cart, so we build our exit there instead)
	if ( ( TFGameRules()->IsAttackDefenseMode() && me->GetTeamNumber() == TF_TEAM_BLUE && TFGameRules()->GetGameType() != TF_GAMETYPE_ESCORT ) ||
		 ( TFGameRules()->GetGameType() == TF_GAMETYPE_CP && !TFGameRules()->IsAttackDefenseMode() && !TFGameRules()->IsInKothMode() ) )
	{
		CObjectTeleporter *myTeleportExit = (CObjectTeleporter *)me->GetObjectOfType( OBJ_TELEPORTER, MODE_TELEPORTER_EXIT );
		int myTeam = me->GetTeamNumber();

		if ( myTeleportExit )
		{
			// if exit is too far from the point, destroy it and try again
			CTeamControlPoint *point = me->GetMyControlPoint();
			if ( point )
			{
				CTFNavArea *pointArea = TheTFNavMesh()->GetControlPointCenterArea( point->GetPointIndex() );

				myTeleportExit->UpdateLastKnownArea();
				CTFNavArea *exitArea = (CTFNavArea *)myTeleportExit->GetLastKnownArea();

				if ( pointArea && exitArea )
				{
					float travelToPoint = fabs( exitArea->GetIncursionDistance( myTeam ) - pointArea->GetIncursionDistance( myTeam ) );

					if ( travelToPoint > tf_bot_max_teleport_exit_travel_to_point.GetFloat() )
					{
						// too far, destroy it
						myTeleportExit->DestroyObject();
						myTeleportExit = NULL;
					}
				}
			}
		}
		else
		{
			CObjectTeleporter *myTeleportEntrance = (CObjectTeleporter *)me->GetObjectOfType( OBJ_TELEPORTER, MODE_TELEPORTER_ENTRANCE );
			CTFNavArea *myArea = me->GetLastKnownArea();

			bool shouldBuildExit = true;

			// if we have a teleporter entrance, don't place the exit too close to it
			if ( myTeleportEntrance && myArea )
			{
				myTeleportEntrance->UpdateLastKnownArea();
				CTFNavArea *enterArea = (CTFNavArea *)myTeleportEntrance->GetLastKnownArea();

				if ( enterArea )
				{
					float travelBetween = fabs( enterArea->GetIncursionDistance( myTeam ) - myArea->GetIncursionDistance( myTeam ) );

					if ( travelBetween < tf_bot_min_teleport_travel.GetFloat() )
					{
						shouldBuildExit = false;
					}
				}
			}

			if ( shouldBuildExit )
			{
				// no exit yet - need to place one
				// when we see the enemy, retreat to cover and build the exit there
				if ( me->GetVisionInterface()->GetPrimaryKnownThreat( true ) )
				{
					if ( !me->m_Shared.InCond( TF_COND_INVULNERABLE ) && ShouldRetreat( me ) != ANSWER_NO )
					{
						Action< CTFBot > *nextActionWhenInCover = new CTFBotEngineerBuildTeleportExit;
						return SuspendFor( new CTFBotRetreatToCover( nextActionWhenInCover ), "Retreating to a safe place to build my teleporter exit" );
					}
				}
			}
		}
	}

	// the cart (or in MvM, the robots) keeps moving while we walk to our build spot - if our spot
	// gets left too far behind, pick a new one
	if ( m_sentryBuildHint == NULL && m_buildLocationCheckTimer.IsElapsed() )
	{
		m_buildLocationCheckTimer.Start( 1.0f );

		CTeamTrainWatcher *trainWatcher = TFGameRules()->GetPayloadToPush( me->GetTeamNumber() );
		if ( trainWatcher )
		{
			float alongTrack;
			trainWatcher->ProjectPointOntoPath( m_sentryBuildLocation, NULL, &alongTrack );

			if ( trainWatcher->GetTrainDistanceAlongTrack() - alongTrack > GetMoveUpDistanceBehindCart() )
			{
				SelectBuildLocation( me );
			}
		}
		else if ( TFGameRules()->IsMannVsMachineMode() )
		{
			CTFNavArea *pushArea = FindMvMRobotPushArea();
			CTFNavArea *buildArea = (CTFNavArea *)TheTFNavMesh()->GetNearestNavArea( m_sentryBuildLocation );

			if ( pushArea && buildArea && buildArea->GetTravelDistanceToBombTarget() > pushArea->GetTravelDistanceToBombTarget() )
			{
				// the robots are already closer to the hatch than our spot
				SelectBuildLocation( me );
			}
		}
	}

	// move to build position
	if ( m_repathTimer.IsElapsed() )
	{
		m_repathTimer.Start( RandomFloat( 1.0f, 2.0f ) );

		CTFBotPathCost cost( me, SAFEST_ROUTE );
		m_path.Compute( me, m_sentryBuildLocation, cost );
	}

	Vector forward;
	me->EyeVectors( &forward );
	forward.z = 0.0f;
	forward.NormalizeInPlace();

	Vector myBlueprintPosition = me->GetAbsOrigin() + 50.0f * forward;

	const float closeToHome = 25.0f;
	Vector toBuild = m_sentryBuildLocation - myBlueprintPosition;
	Vector toMe = m_sentryBuildLocation - me->GetAbsOrigin();

	if ( me->GetLocomotionInterface()->IsOnGround() )
	{
		// we need to wait until we're on the ground since the Build action assumes our position OnStart is where we are going to build
		if ( toMe.AsVector2D().IsLengthLessThan( closeToHome ) || toBuild.AsVector2D().IsLengthLessThan( closeToHome ) )
		{
			if ( m_sentryBuildHint != NULL )
			{
				return ChangeTo( new CTFBotEngineerBuilding( m_sentryBuildHint ), "Reached my precise build location" );
			}

			return ChangeTo( new CTFBotEngineerBuilding, "Reached my build location" );
		}

		m_path.Update( me );
	}

	return Continue();
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CTFBot > CTFBotEngineerMoveToBuild::OnStuck( CTFBot *me )
{
//	SelectBuildLocation( me );
	return TryContinue();
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CTFBot > CTFBotEngineerMoveToBuild::OnMoveToSuccess( CTFBot *me, const Path *path )
{
	return TryContinue();
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CTFBot > CTFBotEngineerMoveToBuild::OnMoveToFailure( CTFBot *me, const Path *path, MoveToFailureType reason )
{
	SelectBuildLocation( me );

	return TryContinue();
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CTFBot > CTFBotEngineerMoveToBuild::OnTerritoryLost( CTFBot *me, int territoryID )
{
	// we have to wait a moment until contested point changes to select a new build spot
	m_fallBackTimer.Start( 0.2f );

	return TryContinue();
}

//---------------------------------------------------------------------------------------------
EventDesiredResult< CTFBot > CTFBotEngineerMoveToBuild::OnTerritoryCaptured( CTFBot *me, int territoryID )
{
	// we have to wait a moment until contested point changes to select a new build spot
	m_fallBackTimer.Start( 0.2f );

	return TryContinue();
}
