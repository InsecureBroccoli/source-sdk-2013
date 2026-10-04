//========= Copyright Valve Corporation, All rights reserved. ============//
// tf_bot_mvm_upgrades.h
// Spend the money we've collected on upgrades in Mann vs Machine

#ifndef TF_BOT_MVM_UPGRADES_H
#define TF_BOT_MVM_UPGRADES_H

class CTFBot;


// Buy the next upgrade on our class's shopping list that we can afford. Return true if we bought one.
bool BuyMvMUpgrade( CTFBot *me );


#endif // TF_BOT_MVM_UPGRADES_H
