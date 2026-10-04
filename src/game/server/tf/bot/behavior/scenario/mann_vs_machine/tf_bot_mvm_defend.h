//========= Copyright Valve Corporation, All rights reserved. ============//
// tf_bot_mvm_defend.h
// Defend the bomb hatch from the robots in Mann vs Machine

#ifndef TF_BOT_MVM_DEFEND_H
#define TF_BOT_MVM_DEFEND_H

#include "Path/NextBotPathFollow.h"

class CTFNavArea;


// Return the area the robots' most dangerous push is in - the bomb or tank closest to the bomb hatch.
// The bomb only counts while the robots have it out in the world, unless there's nothing else.
CTFNavArea *FindMvMRobotPushArea( void );

// Collect the route the robots' most dangerous push will take to the bomb hatch. Return false if there isn't one.
bool CollectMvMRobotRoute( CUtlVector< CTFNavArea * > *routeVector );

// Return the area on the robots' route to the bomb hatch where we should make our stand, the given
// travel distance closer to the hatch than they are
CTFNavArea *FindMvMDefenseArea( float distanceAheadOfRobots );


//---------------------------------------------------------------------------------------------
class CTFBotMvMDefend : public Action< CTFBot >
{
public:
	virtual ActionResult< CTFBot >	OnStart( CTFBot *me, Action< CTFBot > *priorAction );
	virtual ActionResult< CTFBot >	Update( CTFBot *me, float interval );
	virtual ActionResult< CTFBot >	OnResume( CTFBot *me, Action< CTFBot > *interruptingAction );

	virtual EventDesiredResult< CTFBot > OnStuck( CTFBot *me );
	virtual EventDesiredResult< CTFBot > OnMoveToFailure( CTFBot *me, const Path *path, MoveToFailureType reason );

	virtual const CKnownEntity *SelectMoreDangerousThreat( const INextBot *me, const CBaseCombatCharacter *subject, const CKnownEntity *threat1, const CKnownEntity *threat2 ) const;

	virtual const char *GetName( void ) const	{ return "MvMDefend"; };

private:
	PathFollower m_path;
	CountdownTimer m_repathTimer;

	float m_distanceAheadOfRobots;			// how much closer to the hatch than the robots we make our stand
	CTFNavArea *m_defenseArea;
	Vector m_defenseSpot;
	CountdownTimer m_defenseAreaTimer;
};


#endif // TF_BOT_MVM_DEFEND_H
