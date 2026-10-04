//========= Copyright Valve Corporation, All rights reserved. ============//
// tf_bot_mvm_collect_money.h
// Collect the money robots drop in Mann vs Machine

#ifndef TF_BOT_MVM_COLLECT_MONEY_H
#define TF_BOT_MVM_COLLECT_MONEY_H

#include "Path/NextBotPathFollow.h"

class CCurrencyPack;


class CTFBotMvMCollectMoney : public Action< CTFBot >
{
public:
	CTFBotMvMCollectMoney( CCurrencyPack *money );

	static CCurrencyPack *FindMoneyToCollect( CTFBot *me );		// return the money we should go collect, or NULL if there isn't any

	virtual ActionResult< CTFBot >	OnStart( CTFBot *me, Action< CTFBot > *priorAction );
	virtual ActionResult< CTFBot >	Update( CTFBot *me, float interval );

	virtual EventDesiredResult< CTFBot > OnStuck( CTFBot *me );
	virtual EventDesiredResult< CTFBot > OnMoveToFailure( CTFBot *me, const Path *path, MoveToFailureType reason );

	virtual const char *GetName( void ) const	{ return "MvMCollectMoney"; };

private:
	CHandle< CCurrencyPack > m_money;

	PathFollower m_path;
	CountdownTimer m_repathTimer;
	CountdownTimer m_giveUpTimer;
};


#endif // TF_BOT_MVM_COLLECT_MONEY_H
