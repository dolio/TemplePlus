from templeplus.pymod import PythonModifier
from toee import *
import tpdp

print "Registering spell resistance additions"

# Query for previous check result
#   0 is no result
#   1 is previously failed
#   2 is previously succeeded
def CheckResult(critter, args, evt_obj):
	spell_id = args.get_arg(0)
	tgt_spell_id = evt_obj.data1

	if spell_id == tgt_spell_id:
		evt_obj.return_val = args.get_arg(1)

	return 0

# In case the recipient wasn't actively targeted when the spell
# ends, check each round if the spell is inactive and remove
# the condition.
def CheckEnd(critter, args, evt_obj):
	spell_id = args.get_arg(0)

	if not game.is_spell_active(spell_id):
		args.condition_remove()

	return 0

def End(critter, args, evt_obj):
	args.condition_remove()

	return 0

# Condition for tracking results of spell resistance against
# particular spells. The rules say you should only check once
# per spell.
#
# 0 = spell_id, 1 = result
sr_checked = PythonModifier("Spell Resistance Checked", 3, False)
sr_checked.AddHook(ET_OnD20PythonQuery, "Spell Resistance Result", CheckResult, ())
sr_checked.AddHook(ET_OnD20Signal, EK_S_Spell_End, End, ())
sr_checked.AddHook(ET_OnBeginRound, EK_NONE, CheckEnd, ())
