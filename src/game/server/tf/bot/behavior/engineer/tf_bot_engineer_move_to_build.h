//========= Copyright Valve Corporation, All rights reserved. ============//
// tf_bot_engineer_move_to_build.h
// Engineer moving into position to build
// Michael Booth, February 2009

#ifndef TF_BOT_ENGINEER_MOVE_TO_BUILD_H
#define TF_BOT_ENGINEER_MOVE_TO_BUILD_H

#include "Path/NextBotPathFollow.h"

class CTFBotHintSentrygun;
class CTeamTrainWatcher;


class CTFBotEngineerMoveToBuild : public Action< CTFBot >
{
public:
	virtual ActionResult< CTFBot >	OnStart( CTFBot *me, Action< CTFBot > *priorAction );
	virtual ActionResult< CTFBot >	Update( CTFBot *me, float interval );

	virtual EventDesiredResult< CTFBot > OnStuck( CTFBot *me );
	virtual EventDesiredResult< CTFBot > OnMoveToSuccess( CTFBot *me, const Path *path );
	virtual EventDesiredResult< CTFBot > OnMoveToFailure( CTFBot *me, const Path *path, MoveToFailureType reason );

	virtual EventDesiredResult< CTFBot > OnTerritoryLost( CTFBot *me, int territoryID );
	virtual EventDesiredResult< CTFBot > OnTerritoryCaptured( CTFBot *me, int territoryID );

	virtual const char *GetName( void ) const	{ return "EngineerMoveToBuild"; };

	static float GetMoveUpDistanceBehindCart( void );		// on payload, how far behind the cart we're pushing our nest can get before we move it up

private:
	CHandle< CTFBotHintSentrygun > m_sentryBuildHint;
	Vector m_sentryBuildLocation;

	PathFollower m_path;
	CountdownTimer m_repathTimer;

	CUtlVector< CTFNavArea * > m_sentryAreaVector;
	float m_totalSurfaceArea;
	void CollectBuildAreas( CTFBot *me );
	bool CollectBuildAreasBehindCart( CTFBot *me, CTeamTrainWatcher *trainWatcher );
	void ComputeTotalSurfaceArea( void );

	void SelectBuildLocation( CTFBot *me );
	CountdownTimer m_fallBackTimer;
	CountdownTimer m_buildLocationCheckTimer;
};

#endif // TF_BOT_ENGINEER_MOVE_TO_BUILD_H
