/**
 * @file
 * @brief Self-enchantment spells.
**/

#include "AppHdr.h"

#include "spl-selfench.h"

#include "areas.h"
#include "art-enum.h"
#include "butcher.h" // butcher_corpse
#include "coordit.h" // radius_iterator
#include "database.h"
#include "godconduct.h"
#include "hints.h"
#include "items.h" // stack_iterator
#include "libutil.h"
#include "macro.h"
#include "message.h"
#include "output.h"
#include "religion.h"
#include "spl-transloc.h"
#include "spl-util.h"
#include "transform.h"
#include "tilepick.h"
#include "view.h"
#include "viewchar.h"

int allowed_deaths_door_hp()
{
    int hp = calc_spell_power(SPELL_DEATHS_DOOR, true) / 10;

    if (in_good_standing(GOD_KIKUBAAQUDGHA))
        hp += you.piety / 15;

    return max(hp, 1);
}

spret_type cast_deaths_door(int pow, bool fail)
{
    if (you.undead_state())
        mpr(jtrans("You're already dead!"));
    else if (you.duration[DUR_EXHAUSTED])
        mpr(jtrans("You are too exhausted to enter Death's door!"));
    else if (you.duration[DUR_DEATHS_DOOR])
        mpr(jtrans("Your appeal for an extension has been denied."));
    else
    {
        fail_check();
        mpr(jtrans("You stand defiantly in death's doorway!"));
        mpr_nojoin(MSGCH_SOUND, jtrans("You seem to hear sand running through an hourglass..."));

        set_hp(allowed_deaths_door_hp());
        deflate_hp(you.hp_max, false);

        you.set_duration(DUR_DEATHS_DOOR, 10 + random2avg(13, 3)
                                           + (random2(pow) / 10));

        if (you.duration[DUR_DEATHS_DOOR] > 25 * BASELINE_DELAY)
            you.duration[DUR_DEATHS_DOOR] = (23 + random2(5)) * BASELINE_DELAY;
        return SPRET_SUCCESS;
    }

    return SPRET_ABORT;
}

void remove_ice_armour()
{
    mpr_nojoin(MSGCH_DURATION, jtrans("Your icy armour melts away."));
    you.redraw_armour_class = true;
    you.duration[DUR_ICY_ARMOUR] = 0;
}

spret_type ice_armour(int pow, bool fail)
{
    if (!player_effectively_in_light_armour())
    {
        mpr(jtrans("Your body armour is too heavy."));
        return SPRET_ABORT;
    }

    if (player_stoneskin() || you.form == TRAN_STATUE)
    {
        mpr(jtrans("The film of ice won't work on stone."));
        return SPRET_ABORT;
    }

    if (you.duration[DUR_FIRE_SHIELD])
    {
        mpr(jtrans("Your ring of flames would instantly melt the ice."));
        return SPRET_ABORT;
    }

    fail_check();

    if (you.duration[DUR_ICY_ARMOUR])
        mpr(jtrans("Your icy armour thickens."));
    else if (you.form == TRAN_ICE_BEAST)
        mpr(jtrans("Your icy body feels more resilient."));
    else
        mpr(jtrans("A film of ice covers your body!"));

    if (you.attribute[ATTR_BONE_ARMOUR] > 0)
    {
        you.attribute[ATTR_BONE_ARMOUR] = 0;
        mpr(jtrans("Your corpse armour falls away."));
    }

    you.increase_duration(DUR_ICY_ARMOUR, 20 + random2(pow) + random2(pow), 50,
                          nullptr);
    you.props[ICY_ARMOUR_KEY] = pow;
    you.redraw_armour_class = true;

    return SPRET_SUCCESS;
}

/**
 * Iterate over all corpses in LOS and harvest them (unless it's just a test
 * run)
 *
 * @param harvester   The entity planning to do the harvesting.
 * @param dry_run     Whether this is a test run & no corpses should be
 *                    actually destroyed.
 * @return            The total number of corpses (available to be) destroyed.
 */
int harvest_corpses(const actor &harvester, bool dry_run)
{
    int harvested = 0;

    for (radius_iterator ri(harvester.pos(), LOS_NO_TRANS); ri; ++ri)
    {
        for (stack_iterator si(*ri, true); si; ++si)
        {
            item_def &item = *si;
            if (item.base_type != OBJ_CORPSES)
                continue;

            ++harvested;

            if (dry_run)
                continue;

            // don't spam animations
            if (harvested <= 5)
            {
                bolt beam;
                beam.source = *ri;
                beam.target = harvester.pos();
                beam.glyph = dchar_glyph(DCHAR_FIRED_CHUNK);
                beam.colour = item.get_colour();
                beam.range = LOS_RADIUS;
                beam.aimed_at_spot = true;
                beam.item = &item;
                beam.flavour = BEAM_VISUAL;
                beam.draw_delay = 3;
                beam.fire();
                viewwindow();
            }

            destroy_item(item.index());
        }
    }

    return harvested;
}


/**
 * Casts the player spell "Cigotuvi's Embrace", pulling all corpses into LOS
 * around the caster to serve as armour.
 *
 * @param pow   The spellpower at which the spell is being cast.
 * @param fail  Whether the casting failed.
 * @return      SPRET_ABORT if you already have an incompatible buff running,
 *              SPRET_FAIL if fail is true, and SPRET_SUCCESS otherwise.
 */
spret_type corpse_armour(int pow, bool fail)
{
    if (player_stoneskin() || you.form == TRAN_STATUE)
    {
        mpr(jtrans("The corpses won't embrace your stony flesh."));
        return SPRET_ABORT;
    }

    if (you.duration[DUR_ICY_ARMOUR])
    {
        mpr(jtrans("The corpses won't embrace your icy flesh."));
        return SPRET_ABORT;
    }

    // Could check carefully to see if it's even possible that there are any
    // valid corpses/skeletons in LOS (any piles with stuff under them, etc)
    // before failing, but it's better to be simple + predictable from the
    // player's perspective.
    fail_check();

    const int harvested = harvest_corpses(you);
    dprf("Harvested: %d", harvested);

    if (!harvested)
    {
        canned_msg(MSG_NOTHING_HAPPENS);
        return SPRET_SUCCESS; // still takes a turn, etc
    }

    if (you.attribute[ATTR_BONE_ARMOUR] <= 0)
        mpr(jtrans("The bodies of the dead rush to embrace you!"));
    else
        mpr(jtrans("Your shell of carrion and bone grows thicker."));

    you.attribute[ATTR_BONE_ARMOUR] += harvested;
    you.redraw_armour_class = true;

    return SPRET_SUCCESS;
}


spret_type missile_prot(int pow, bool fail)
{
    if (you.attribute[ATTR_REPEL_MISSILES]
        || you.attribute[ATTR_DEFLECT_MISSILES]
        || player_equip_unrand(UNRAND_AIR))
    {
        mpr(jtrans("You are already protected from missiles."));
        return SPRET_ABORT;
    }
    fail_check();
    you.attribute[ATTR_REPEL_MISSILES] = 1;
    mpr(jtrans("You feel protected from missiles."));
    return SPRET_SUCCESS;
}

spret_type deflection(int pow, bool fail)
{
    if (you.attribute[ATTR_DEFLECT_MISSILES])
    {
        mpr(jtrans("You are already deflecting missiles."));
        return SPRET_ABORT;
    }
    fail_check();
    you.attribute[ATTR_DEFLECT_MISSILES] = 1;
    mpr(jtrans("You feel very safe from missiles."));
    // Replace RMsl, if active.
    if (you.attribute[ATTR_REPEL_MISSILES])
        you.attribute[ATTR_REPEL_MISSILES] = 0;

    return SPRET_SUCCESS;
}

spret_type cast_regen(int pow, bool fail)
{
    fail_check();
    you.increase_duration(DUR_REGENERATION, 5 + roll_dice(2, pow / 3 + 1), 100,
                          jtransc("Your skin crawls."));

    return SPRET_SUCCESS;
}

spret_type cast_revivification(int pow, bool fail)
{
    if (you.hp == you.hp_max)
        canned_msg(MSG_NOTHING_HAPPENS);
    else if (you.hp_max < 21)
        mpr(jtrans("You lack the resilience to cast this spell."));
    else
    {
        fail_check();
        mpr(jtrans("Your body is healed in an amazingly painful way."));

        const int loss = 6 + binomial(9, 8, pow);
        dec_max_hp(loss * you.hp_max / 100);
        set_hp(you.hp_max);

        if (you.duration[DUR_DEATHS_DOOR])
        {
            mpr_nojoin(MSGCH_DURATION, jtrans("Your life is in your own hands once again."));
            // XXX: better cause name?
            paralyse_player("Death's Door abortion", 5 + random2(5));
            confuse_player(10 + random2(10));
            you.duration[DUR_DEATHS_DOOR] = 0;
        }
        return SPRET_SUCCESS;
    }

    return SPRET_ABORT;
}

spret_type cast_swiftness(int power, bool fail)
{
    if (you.is_stationary())
    {
        canned_msg(MSG_CANNOT_MOVE);
        return SPRET_ABORT;
    }

    if (!you.duration[DUR_SWIFTNESS] && player_movement_speed() <= 6)
    {
        mpr(jtrans("You can't move any more quickly."));
        return SPRET_ABORT;
    }

    if (you.duration[DUR_SWIFTNESS])
    {
        mpr(jtrans("This spell is already in effect."));
        return SPRET_ABORT;
    }

    fail_check();

    if (you.in_liquid())
    {
        // Hint that the player won't be faster until they leave the liquid.
        mprf("The %s foams!", you.in_water() ? "水たまり"
                            : you.in_lava()  ? "溶岩"
                                             : "液状化した地面");
    }

    you.set_duration(DUR_SWIFTNESS, 12 + random2(power)/2, 30,
                     jtransc("You feel quick."));
    you.attribute[ATTR_SWIFTNESS] = you.duration[DUR_SWIFTNESS];
    did_god_conduct(DID_HASTY, 8, true);

    return SPRET_SUCCESS;
}

spret_type cast_fly(int power, bool fail)
{
    if (!flight_allowed())
        return SPRET_ABORT;

    fail_check();
    const int dur_change = 25 + random2(power) + random2(power);
    const bool was_flying = you.airborne();

    you.increase_duration(DUR_FLIGHT, dur_change, 100);
    you.attribute[ATTR_FLIGHT_UNCANCELLABLE] = 1;

    if (!was_flying)
        float_player();
    else
        mpr(jtrans("You feel more buoyant."));
    return SPRET_SUCCESS;
}

spret_type cast_teleport_control(int power, bool fail)
{
    fail_check();
    if (allow_control_teleport(true))
        mpr(jtrans("You feel in control."));
    else
        mpr(jtrans("You feel your control is inadequate."));

    if (you.duration[DUR_TELEPORT] && !player_control_teleport())
    {
        mpr(jtrans("You feel your translocation being delayed."));
        you.increase_duration(DUR_TELEPORT, 1 + random2(3));
    }

    you.increase_duration(DUR_CONTROL_TELEPORT, 10 + random2(power), 50);

    return SPRET_SUCCESS;
}

int cast_selective_amnesia(const string &pre_msg)
{
    if (you.spell_no == 0)
    {
        canned_msg(MSG_NO_SPELLS);
        return 0;
    }

    int keyin = 0;
    spell_type spell;
    int slot;

    // Pick a spell to forget.
    mpr_nojoin(MSGCH_PROMPT, jtrans("Forget which spell ([?*] list [ESC] exit)? "));
    keyin = list_spells(false, false, false, jtrans("Forget which spell?"));
    redraw_screen();

    while (true)
    {
        if (key_is_escape(keyin))
        {
            canned_msg(MSG_OK);
            return -1;
        }

        if (keyin == '?' || keyin == '*')
        {
            keyin = list_spells(false, false, false, jtrans("Forget which spell?"));
            redraw_screen();
        }

        if (!isaalpha(keyin))
        {
            clear_messages();
            mpr_nojoin(MSGCH_PROMPT, jtrans("Forget which spell ([?*] list [ESC] exit)? "));
            keyin = get_ch();
            continue;
        }

        spell = get_spell_by_letter(keyin);
        slot = get_spell_slot_by_letter(keyin);

        if (spell == SPELL_NO_SPELL)
        {
            mpr(jtrans("You don't know that spell."));
            mpr_nojoin(MSGCH_PROMPT, jtrans("Forget which spell ([?*] list [ESC] exit)? "));
            keyin = get_ch();
        }
        else
            break;
    }

    if (!pre_msg.empty())
        mpr(pre_msg);

    del_spell_from_memory_by_slot(slot);

    return 1;
}

spret_type cast_infusion(int pow, bool fail)
{
    fail_check();
    if (!you.duration[DUR_INFUSION])
        mpr(jtrans("You begin infusing your attacks with magical energy."));
    else
        mpr(jtrans("You extend your infusion's duration."));

    you.increase_duration(DUR_INFUSION,  8 + roll_dice(2, pow), 100);
    you.props["infusion_power"] = pow;

    return SPRET_SUCCESS;
}

spret_type cast_song_of_slaying(int pow, bool fail)
{
    fail_check();

    if (you.duration[DUR_SONG_OF_SLAYING])
        mpr(jtrans("You start a new song!"));
    else
        mpr(jtrans("You start singing a song of slaying."));

    you.set_duration(DUR_SONG_OF_SLAYING, 20 + random2avg(pow, 2));

    you.props["song_of_slaying_bonus"] = 0;
    return SPRET_SUCCESS;
}

spret_type cast_silence(int pow, bool fail)
{
    fail_check();
    mpr(jtrans("A profound silence engulfs you."));

    you.increase_duration(DUR_SILENCE, 10 + pow/4 + random2avg(pow/2, 2), 100);
    invalidate_agrid(true);

    if (you.beheld())
        you.update_beholders();

    learned_something_new(HINT_YOU_SILENCE);
    return SPRET_SUCCESS;
}

spret_type cast_liquefaction(int pow, bool fail)
{
    if (!you.stand_on_solid_ground())
    {
        if (!you.ground_level())
            mpr(jtrans("You can't cast this spell without touching the ground."));
        else
            mpr(jtrans("You need to be on clear, solid ground to cast this spell."));
        return SPRET_ABORT;
    }

    if (you.duration[DUR_LIQUEFYING] || liquefied(you.pos()))
    {
        mpr(jtrans("The ground here is already liquefied! You'll have to wait."));
        return SPRET_ABORT;
    }

    fail_check();
    flash_view_delay(UA_PLAYER, BROWN, 80);
    flash_view_delay(UA_PLAYER, YELLOW, 80);
    flash_view_delay(UA_PLAYER, BROWN, 140);

    mpr(jtrans("The ground around you becomes liquefied!"));

    you.increase_duration(DUR_LIQUEFYING, 10 + random2avg(pow, 2), 100);
    invalidate_agrid(true);
    return SPRET_SUCCESS;
}

spret_type cast_shroud_of_golubria(int pow, bool fail)
{
    fail_check();
    if (you.duration[DUR_SHROUD_OF_GOLUBRIA])
        mpr(jtrans("You renew your shroud."));
    else
        mpr(jtrans("Space distorts slightly along a thin shroud covering your body."));

    you.increase_duration(DUR_SHROUD_OF_GOLUBRIA, 7 + roll_dice(2, pow), 50);
    return SPRET_SUCCESS;
}

spret_type cast_transform(int pow, transformation_type which_trans, bool fail)
{
    if (!transform(pow, which_trans, false, true))
        return SPRET_ABORT;

    fail_check();
    transform(pow, which_trans);
    return SPRET_SUCCESS;
}
