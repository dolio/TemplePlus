from toee import *
import tpdp

def OnBeginSpellCast( spell ):
	print "Spike Growth OnBeginSpellCast"
	print "spell.target_list=", spell.target_list
	print "spell.caster=", spell.caster, " caster.level= ", spell.caster_level
	game.particles( "sp-transmutation-conjure", spell.caster )

def	OnSpellEffect( spell ):
	print "Spike Growth OnSpellEffect"

	spell.duration = 600 * spell.caster_level
	
	#Using the same areas as entangle.  From the SRD:
	#In areas of bare earth, roots and rootlets act in the same way. Typically, spike growth can be cast in any outdoor setting except open water, ice, heavy snow, sandy desert, or bare stone.
	outdoor_map_list = [5001, 5002, 5009, 5042, 5043, 5051, 5062, 5068, 5069, 5070, 5071, 5072, 5073, 5074, 5075, 5076, 5077, 5078, 5091, 5093, 5094, 5095, 5096, 5097, 5099, 5100, 5108, 5110, 5111, 5112, 5113, 5119, 5120, 5121, 5132, 5142, 5189]

	if (tpdp.config_get_bool("StricterRulesEnforcement") and tpdp.is_temple_module()) or (game.leader.map in outdoor_map_list):

		# spawn one spell_object object
		spell_obj = game.obj_create( OBJECT_SPELL_GENERIC, spell.target_loc, spell.target_loc_off_x, spell.target_loc_off_y )

		# add to d20initiative
		caster_init_value = spell.caster.get_initiative()
		spell_obj.d20_status_init()
		spell_obj.set_initiative( caster_init_value )

		# put sp-Spike Growth condition on obj
		spell_obj_partsys_id = game.particles( 'sp-Spike Growth', spell_obj )
		spell_obj.condition_add_with_args( 'sp-Spike Growth', spell.id, spell.duration, 0, spell_obj_partsys_id )
		#spell_obj.condition_add_arg_x( 3, spell_obj_partsys_id )
		#objectevent_id = spell_obj.condition_get_arg_x( 2 )
	
	else:
		# No plants to entangle with
		game.particles( 'Fizzle', spell.caster )
		spell.caster.float_mesfile_line( 'mes\\spell.mes', 30000 )
		spell.caster.float_mesfile_line( 'mes\\spell.mes', 16014 )

def OnBeginRound( spell ):
	print "Spike Growth OnBeginRound"

def OnEndSpellCast( spell ):
	print "Spike Growth OnEndSpellCast"

def OnAreaOfEffectHit( spell ):
	print "Spike Growth OnAreaOfEffectHit"

def OnSpellStruck( spell ):
	print "Spike Growth OnSpellStruck"