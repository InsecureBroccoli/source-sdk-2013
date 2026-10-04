//========= Copyright Valve Corporation, All rights reserved. ============//
// tf_bot_mvm_upgrades.cpp
// Spend the money we've collected on upgrades in Mann vs Machine

#include "cbase.h"
#include "tf_player.h"
#include "tf_gamerules.h"
#include "tf_upgrades_shared.h"
#include "econ_item_system.h"
#include "player_vs_environment/tf_upgrades.h"
#include "bot/tf_bot.h"
#include "bot/behavior/scenario/mann_vs_machine/tf_bot_mvm_upgrades.h"


ConVar tf_bot_debug_mvm_upgrades( "tf_bot_debug_mvm_upgrades", "0", FCVAR_CHEAT, "Print the upgrades bots defending in MvM buy" );


// upgrades to ourselves, rather than to one of our items
#define PLAYER_UPGRADE_SLOT		-1

struct ShoppingListEntry_t
{
	int m_slot;					// the loadout slot of the item to upgrade, or PLAYER_UPGRADE_SLOT
	const char *m_attribute;	// the upgrade's attribute, as named in mvm_upgrades.txt
};


// Each class's shopping list, assuming stock weapons, most important first. We buy one step of each
// upgrade before buying a second step of any, and skip upgrades that don't apply to what we're carrying.

static const ShoppingListEntry_t s_scoutShoppingList[] =
{
	{ LOADOUT_POSITION_PRIMARY,		"damage bonus" },
	{ LOADOUT_POSITION_PRIMARY,		"faster reload rate" },
	{ PLAYER_UPGRADE_SLOT,			"health regen" },
	{ PLAYER_UPGRADE_SLOT,			"dmg taken from bullets reduced" },
	{ LOADOUT_POSITION_PRIMARY,		"clip size bonus upgrade" },
	{ LOADOUT_POSITION_PRIMARY,		"heal on kill" },
	{ PLAYER_UPGRADE_SLOT,			"move speed bonus" },
	{ PLAYER_UPGRADE_SLOT,			"dmg taken from blast reduced" },
	{ LOADOUT_POSITION_PRIMARY,		"fire rate bonus" },
	{ LOADOUT_POSITION_PRIMARY,		"projectile penetration" },
	{ PLAYER_UPGRADE_SLOT,			"dmg taken from fire reduced" },
};

static const ShoppingListEntry_t s_soldierShoppingList[] =
{
	{ LOADOUT_POSITION_PRIMARY,		"damage bonus" },
	{ LOADOUT_POSITION_PRIMARY,		"clip size upgrade atomic" },
	{ LOADOUT_POSITION_PRIMARY,		"faster reload rate" },
	{ LOADOUT_POSITION_PRIMARY,		"fire rate bonus" },
	{ PLAYER_UPGRADE_SLOT,			"health regen" },
	{ PLAYER_UPGRADE_SLOT,			"dmg taken from bullets reduced" },
	{ LOADOUT_POSITION_PRIMARY,		"rocket specialist" },
	{ LOADOUT_POSITION_PRIMARY,		"heal on kill" },
	{ PLAYER_UPGRADE_SLOT,			"dmg taken from blast reduced" },
	{ LOADOUT_POSITION_PRIMARY,		"maxammo primary increased" },
};

static const ShoppingListEntry_t s_pyroShoppingList[] =
{
	{ LOADOUT_POSITION_PRIMARY,		"damage bonus" },
	{ PLAYER_UPGRADE_SLOT,			"dmg taken from bullets reduced" },
	{ LOADOUT_POSITION_PRIMARY,		"weapon burn dmg increased" },
	{ LOADOUT_POSITION_PRIMARY,		"heal on kill" },
	{ PLAYER_UPGRADE_SLOT,			"health regen" },
	{ LOADOUT_POSITION_PRIMARY,		"maxammo primary increased" },
	{ PLAYER_UPGRADE_SLOT,			"dmg taken from blast reduced" },
	{ LOADOUT_POSITION_PRIMARY,		"weapon burn time increased" },
	{ PLAYER_UPGRADE_SLOT,			"move speed bonus" },
};

static const ShoppingListEntry_t s_demomanShoppingList[] =
{
	{ LOADOUT_POSITION_PRIMARY,		"damage bonus" },
	{ LOADOUT_POSITION_PRIMARY,		"faster reload rate" },
	{ LOADOUT_POSITION_PRIMARY,		"fire rate bonus" },
	{ PLAYER_UPGRADE_SLOT,			"health regen" },
	{ PLAYER_UPGRADE_SLOT,			"dmg taken from bullets reduced" },
	{ LOADOUT_POSITION_SECONDARY,	"damage bonus" },
	{ LOADOUT_POSITION_PRIMARY,		"clip size bonus upgrade" },
	{ PLAYER_UPGRADE_SLOT,			"dmg taken from blast reduced" },
	{ LOADOUT_POSITION_SECONDARY,	"faster reload rate" },
	{ LOADOUT_POSITION_PRIMARY,		"Projectile speed increased" },
	{ LOADOUT_POSITION_PRIMARY,		"heal on kill" },
};

static const ShoppingListEntry_t s_heavyShoppingList[] =
{
	{ LOADOUT_POSITION_PRIMARY,		"damage bonus" },
	{ PLAYER_UPGRADE_SLOT,			"dmg taken from bullets reduced" },
	{ LOADOUT_POSITION_PRIMARY,		"projectile penetration heavy" },
	{ LOADOUT_POSITION_PRIMARY,		"heal on kill" },
	{ PLAYER_UPGRADE_SLOT,			"health regen" },
	{ LOADOUT_POSITION_PRIMARY,		"fire rate bonus" },
	{ LOADOUT_POSITION_PRIMARY,		"maxammo primary increased" },
	{ LOADOUT_POSITION_PRIMARY,		"attack projectiles" },
	{ PLAYER_UPGRADE_SLOT,			"dmg taken from blast reduced" },
};

static const ShoppingListEntry_t s_engineerShoppingList[] =
{
	{ LOADOUT_POSITION_PDA,			"engy sentry fire rate increased" },
	{ LOADOUT_POSITION_PDA,			"engy building health bonus" },
	{ LOADOUT_POSITION_PDA,			"maxammo metal increased" },
	{ PLAYER_UPGRADE_SLOT,			"metal regen" },
	{ LOADOUT_POSITION_PDA,			"engy dispenser radius increased" },
	{ PLAYER_UPGRADE_SLOT,			"dmg taken from bullets reduced" },
	{ PLAYER_UPGRADE_SLOT,			"health regen" },
	{ LOADOUT_POSITION_PDA,			"bidirectional teleport" },
};

static const ShoppingListEntry_t s_medicShoppingList[] =
{
	{ LOADOUT_POSITION_SECONDARY,	"ubercharge rate bonus" },
	{ LOADOUT_POSITION_SECONDARY,	"healing mastery" },
	{ PLAYER_UPGRADE_SLOT,			"health regen" },
	{ LOADOUT_POSITION_SECONDARY,	"uber duration bonus" },
	{ PLAYER_UPGRADE_SLOT,			"dmg taken from bullets reduced" },
	{ LOADOUT_POSITION_SECONDARY,	"overheal expert" },
	{ PLAYER_UPGRADE_SLOT,			"dmg taken from blast reduced" },
	{ PLAYER_UPGRADE_SLOT,			"move speed bonus" },
};

static const ShoppingListEntry_t s_sniperShoppingList[] =
{
	{ LOADOUT_POSITION_PRIMARY,		"damage bonus" },
	{ LOADOUT_POSITION_PRIMARY,		"SRifle Charge rate increased" },
	{ LOADOUT_POSITION_PRIMARY,		"explosive sniper shot" },
	{ LOADOUT_POSITION_PRIMARY,		"projectile penetration" },
	{ PLAYER_UPGRADE_SLOT,			"health regen" },
	{ LOADOUT_POSITION_PRIMARY,		"faster reload rate" },
	{ PLAYER_UPGRADE_SLOT,			"dmg taken from bullets reduced" },
	{ PLAYER_UPGRADE_SLOT,			"dmg taken from blast reduced" },
};

static const ShoppingListEntry_t s_spyShoppingList[] =
{
	{ LOADOUT_POSITION_MELEE,		"armor piercing" },
	{ PLAYER_UPGRADE_SLOT,			"dmg taken from bullets reduced" },
	{ LOADOUT_POSITION_MELEE,		"melee attack rate bonus" },
	{ PLAYER_UPGRADE_SLOT,			"health regen" },
	{ LOADOUT_POSITION_SECONDARY,	"fire rate bonus" },
	{ PLAYER_UPGRADE_SLOT,			"move speed bonus" },
	{ PLAYER_UPGRADE_SLOT,			"dmg taken from blast reduced" },
};


//---------------------------------------------------------------------------------------------
static const ShoppingListEntry_t *GetShoppingList( int playerClass, int *count )
{
#define SHOPPING_LIST( list )	*count = ARRAYSIZE( list ); return list

	switch( playerClass )
	{
	case TF_CLASS_SCOUT:		SHOPPING_LIST( s_scoutShoppingList );
	case TF_CLASS_SOLDIER:		SHOPPING_LIST( s_soldierShoppingList );
	case TF_CLASS_PYRO:			SHOPPING_LIST( s_pyroShoppingList );
	case TF_CLASS_DEMOMAN:		SHOPPING_LIST( s_demomanShoppingList );
	case TF_CLASS_HEAVYWEAPONS:	SHOPPING_LIST( s_heavyShoppingList );
	case TF_CLASS_ENGINEER:		SHOPPING_LIST( s_engineerShoppingList );
	case TF_CLASS_MEDIC:		SHOPPING_LIST( s_medicShoppingList );
	case TF_CLASS_SNIPER:		SHOPPING_LIST( s_sniperShoppingList );
	case TF_CLASS_SPY:			SHOPPING_LIST( s_spyShoppingList );
	}

#undef SHOPPING_LIST

	*count = 0;
	return NULL;
}


//---------------------------------------------------------------------------------------------
// Return the index of the upgrade with the given attribute that we can buy for the item in the given slot
// (or for ourselves), or -1 if there isn't one. Some attributes have versions for different weapons.
static int FindUpgrade( CTFBot *me, int slot, const char *attribute )
{
	for ( int i=0; i<g_MannVsMachineUpgrades.m_Upgrades.Count(); ++i )
	{
		CMannVsMachineUpgrades *upgrade = &g_MannVsMachineUpgrades.m_Upgrades[i];

		if ( V_stricmp( upgrade->szAttrib, attribute ) )
			continue;

		// upgrades to ourselves don't go on an item, and we don't use canteens
		bool isPlayerUpgrade = ( upgrade->nUIGroup == UIGROUP_UPGRADE_ATTACHED_TO_PLAYER );
		if ( isPlayerUpgrade != ( slot == PLAYER_UPGRADE_SLOT ) || upgrade->nUIGroup == UIGROUP_POWERUPBOTTLE )
			continue;

		CEconItemAttributeDefinition *attribDef = ItemSystem()->GetStaticDataForAttributeByName( upgrade->szAttrib );
		if ( !attribDef )
			continue;

		if ( TFGameRules()->CanUpgradeWithAttrib( me, slot, attribDef->GetDefinitionIndex(), upgrade ) )
			return i;
	}

	return -1;
}


//---------------------------------------------------------------------------------------------
struct UpgradeCandidate_t
{
	int m_upgrade;
	int m_slot;
	int m_cost;
	int m_stepsOwned;
};


//---------------------------------------------------------------------------------------------
bool BuyMvMUpgrade( CTFBot *me )
{
	// the upgrade station does the actual buying
	if ( !g_hUpgradeEntity || !TFGameRules() )
		return false;

	int myClass = me->GetPlayerClass()->GetClassIndex();

	int count;
	const ShoppingListEntry_t *shoppingList = GetShoppingList( myClass, &count );

	// collect what we can afford, keeping the shopping list's order among upgrades we own the same number of steps of
	CUtlVector< UpgradeCandidate_t > candidateVector;

	for( int i=0; i<count; ++i )
	{
		int slot = shoppingList[i].m_slot;

		int upgrade = FindUpgrade( me, slot, shoppingList[i].m_attribute );
		if ( upgrade < 0 )
			continue;

		int cost = TFGameRules()->GetCostForUpgrade( &g_MannVsMachineUpgrades.m_Upgrades[ upgrade ], slot, myClass, me );
		if ( cost > me->GetCurrency() )
			continue;

		int stepsOwned = 0;
		bool isOverCap = false;
		int maxSteps = GetUpgradeStepData( me, slot, upgrade, stepsOwned, isOverCap );
		if ( isOverCap || stepsOwned >= maxSteps )
			continue;

		UpgradeCandidate_t candidate;
		candidate.m_upgrade = upgrade;
		candidate.m_slot = slot;
		candidate.m_cost = cost;
		candidate.m_stepsOwned = stepsOwned;

		int insertAt = candidateVector.Count();
		while( insertAt > 0 && candidateVector[ insertAt-1 ].m_stepsOwned > stepsOwned )
		{
			--insertAt;
		}

		candidateVector.InsertBefore( insertAt, candidate );
	}

	// spread our money around - buy whatever we own the fewest steps of
	FOR_EACH_VEC( candidateVector, i )
	{
		const UpgradeCandidate_t &candidate = candidateVector[i];

		me->BeginPurchasableUpgrades();
		bool isBought = g_hUpgradeEntity->PlayerPurchasingUpgrade( me, candidate.m_slot, candidate.m_upgrade, false );
		me->EndPurchasableUpgrades();

		if ( isBought )
		{
			if ( tf_bot_debug_mvm_upgrades.GetBool() )
			{
				Msg( "%s bought '%s' for $%d ($%d left)\n", me->GetPlayerName(), g_MannVsMachineUpgrades.m_Upgrades[ candidate.m_upgrade ].szAttrib, candidate.m_cost, me->GetCurrency() );
			}

			return true;
		}
	}

	return false;
}
