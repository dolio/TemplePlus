from toee import *
import race_defs

###################################################
print "Registering race: Wild Elf"

raceEnum = race_elf + (4 << 5)

raceSpec = race_defs.RaceSpec()
raceSpec.hit_dice        = dice_new("0d0")
raceSpec.level_modifier  = 0
raceSpec.help_topic      = "TAG_WILD_ELF"
raceSpec.flags           = 0
raceSpec.modifier_name   = "Wild Elf"
raceSpec.height_male     = [53, 65]
raceSpec.height_female   = [53, 65]
raceSpec.weight_male     = [87, 121]
raceSpec.weight_female   = [82, 116]
raceSpec.stat_modifiers  = [0, 2, 0, -2, 0, 0]   # str, dex, con, int, wis, cha
raceSpec.proto_id        = 13004
raceSpec.material_offset = 2         # offset into rules/material_ext.mes file
raceSpec.feats           = [feat_simple_weapon_proficiency_elf]

###################################################
def RegisterRace():
	raceSpec.register(raceEnum)

def GetFavoredClass(obj = OBJ_HANDLE_NULL):
	return stat_level_sorcerer

def GetLevelModifier(obj = OBJ_HANDLE_NULL):
	return 0

