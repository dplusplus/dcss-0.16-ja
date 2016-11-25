/**
 * @file
 * @brief Classes tracking player stashes
**/

#include "AppHdr.h"

#include "stash.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>

#include "chardump.h"
#include "clua.h"
#include "cluautil.h"
#include "command.h"
#include "coordit.h"
#include "database.h"
#include "describe.h"
#include "describe-spells.h"
#include "directn.h"
#include "env.h"
#include "feature.h"
#include "godpassive.h"
#include "hints.h"
#include "invent.h"
#include "itemprop.h"
#include "items.h"
#include "libutil.h" // map_find
#include "message.h"
#include "notes.h"
#include "output.h"
#include "religion.h"
#include "rot.h"
#include "spl-book.h"
#include "state.h"
#include "stringutil.h"
#include "syscalls.h"
#include "terrain.h"
#include "traps.h"
#include "travel.h"
#include "unicode.h"
#include "viewmap.h"

#define MAX_BRANCH_ABBREV_SIZE (strwidth("ゾットの領域:5"))

// Global
StashTracker StashTrack;

string userdef_annotate_item(const char *s, const item_def *item,
                             bool exclusive)
{
#ifdef CLUA_BINDINGS
    lua_stack_cleaner cleaner(clua);
    clua_push_item(clua, const_cast<item_def*>(item));
    if (!clua.callfn(s, 1, 1) && !clua.error.empty())
        mprf(MSGCH_ERROR, "Lua error: %s", clua.error.c_str());
    string ann;
    if (lua_isstring(clua, -1))
        ann = luaL_checkstring(clua, -1);
    return ann;
#else
    return "";
#endif
}

string stash_annotate_item(const char *s, const item_def *item, bool exclusive)
{
    string text = userdef_annotate_item(s, item, exclusive);

    if (item->has_spells())
    {
        formatted_string fs;
        describe_spellset(item_spellset(*item), item, fs);
        text += "\n";
        text += fs.tostring();
    }

    // Include singular form (slice of pizza vs slices of pizza).
    if (item->quantity > 1)
    {
        text += "\n";
        text += item->name(DESC_QUALNAME);
    }

    return text;
}

void maybe_update_stashes()
{
    if (!crawl_state.game_is_arena())
        StashTrack.update_visible_stashes();
}

bool is_stash(const coord_def& c)
{
    LevelStashes *ls = StashTrack.find_current_level();
    if (ls)
    {
        Stash *s = ls->find_stash(c);
        return s && s->enabled;
    }
    return false;
}

string get_stash_desc(const coord_def& c)
{
    LevelStashes *ls = StashTrack.find_current_level();
    if (ls)
    {
        Stash *s = ls->find_stash(c);
        if (s)
        {
            const string desc = s->description();
            if (!desc.empty())
                return "[Stash: " + desc + "]";
        }
    }
    return "";
}

void describe_stash(const coord_def& c)
{
    string desc = get_stash_desc(c);
    if (!desc.empty())
        mprf(MSGCH_EXAMINE_FILTER, "%s", desc.c_str());
}

vector<item_def> Stash::get_items() const
{
    return items;
}

vector<item_def> item_list_in_stash(const coord_def& pos)
{
    vector<item_def> ret;

    LevelStashes *ls = StashTrack.find_current_level();
    if (ls)
    {
        Stash *s = ls->find_stash(pos);
        if (s)
            ret = s->get_items();
    }

    return ret;
}

static void _fully_identify_item(item_def *item)
{
    if (!item || !item->defined())
        return;

    set_ident_flags(*item, ISFLAG_IDENT_MASK);
    if (item->base_type != OBJ_WEAPONS)
        set_ident_type(*item, ID_KNOWN_TYPE);
}

// ----------------------------------------------------------------------
// Stash
// ----------------------------------------------------------------------

Stash::Stash(int xp, int yp) : enabled(true), items()
{
    // First, fix what square we're interested in
    if (xp == -1)
    {
        xp = you.pos().x;
        yp = you.pos().y;
    }
    x = xp;
    y = yp;
    abspos = GXM * (int) y + x;

    update();
}

bool Stash::are_items_same(const item_def &a, const item_def &b, bool exact)
{
    const bool same = a.is_type(b.base_type, b.sub_type)
        // Ignore Gozag's gold flag, and rod charges.
        && (a.plus == b.plus || a.base_type == OBJ_GOLD && !exact
                             || a.base_type == OBJ_RODS && !exact)
        && a.plus2 == b.plus2
        && a.special == b.special
        && a.get_colour() == b.get_colour() // ????????
        && a.flags == b.flags
        && a.quantity == b.quantity;

    return same
           || (!exact && a.base_type == b.base_type
               && (a.base_type == OBJ_CORPSES
                   || (a.is_type(OBJ_FOOD, FOOD_CHUNK)
                       && b.sub_type == FOOD_CHUNK))
               && a.plus == b.plus);
}

bool Stash::unverified() const
{
    return !verified;
}

bool Stash::pickup_eligible() const
{
    for (int i = 0, size = items.size(); i < size; ++i)
        if (item_needs_autopickup(items[i]))
            return true;

    return false;
}

bool Stash::sacrificeable() const
{
    for (int i = 0, size = items.size(); i < size; ++i)
        if (items[i].is_greedy_sacrificeable())
            return true;

    return false;
}

bool Stash::needs_stop() const
{
    for (int i = 0, size = items.size(); i < size; ++i)
        if (!items[i].is_greedy_sacrificeable()
            && !item_needs_autopickup(items[i]))
        {
            return true;
        }

    return false;
}

static bool _grid_has_perceived_item(const coord_def& pos)
{
    return you.visible_igrd(pos) != NON_ITEM;
}

static bool _grid_has_perceived_multiple_items(const coord_def& pos)
{
    int count = 0;

    for (stack_iterator si(pos, true); si && count < 2; ++si)
        ++count;

    return count > 1;
}

bool Stash::unmark_trapping_nets()
{
    bool changed = false;
    for (auto &item : items)
        if (item_is_stationary_net(item))
            item.net_placed = false, changed = true;
    return changed;
}

void Stash::update()
{
    coord_def p(x,y);
    feat = grd(p);
    trap = NUM_TRAPS;

    if (feat_is_trap(feat))
    {
        trap = get_trap_type(p);
        if (trap == TRAP_WEB)
            feat = DNGN_FLOOR, trap = TRAP_UNASSIGNED;
    }
    else if (!is_notable_terrain(feat))
        feat = DNGN_FLOOR;

    if (feat == DNGN_FLOOR)
        feat_desc = "";
    else
    {
        feat_desc = feature_description_at(coord_def(x, y), false,
                                           DESC_A, false);
    }

    // If this is your position, you know what's on this square
    if (p == you.pos())
    {
        // Zap existing items
        items.clear();

        // Now, grab all items on that square and fill our vector
        for (stack_iterator si(p, true); si; ++si)
            add_item(*si);

        verified = true;
    }
    // If this is not your position, the only thing we can do is verify that
    // what the player sees on the square is the first item in this vector.
    else
    {
        if (!_grid_has_perceived_item(p))
        {
            items.clear();
            verified = true;
            return;
        }

        // There's something on this square. Take a squint at it.
        item_def *pitem = &mitm[you.visible_igrd(p)];
        hints_first_item(*pitem);

        god_id_item(*pitem);
        maybe_identify_base_type(*pitem);
        const item_def& item = *pitem;

        if (!_grid_has_perceived_multiple_items(p))
            items.clear();

        // We knew of nothing on this square, so we'll assume this is the
        // only item here, but mark it as unverified unless we can see nothing
        // under the item.
        if (items.empty())
        {
            add_item(item);
            // Note that we could be lying here, since we can have
            // a verified falsehood (if there's a mimic.)
            verified = !_grid_has_perceived_multiple_items(p);
            return;
        }

        // There's more than one item in this pile. Check to see if
        // the top item matches what we remember.
        const item_def &first = items[0];
        // Compare these items
        if (are_items_same(first, item))
        {
            // Replace the item to reflect seen recharging, etc.
            if (!are_items_same(first, item, true))
            {
                items.erase(items.begin());
                add_item(item, true);
            }
        }
        else
        {
            // See if 'item' matches any of the items we have. If it does,
            // we'll just make that the first item and leave 'verified'
            // unchanged.

            // Start from 1 because we've already checked items[0]
            for (int i = 1, count = items.size(); i < count; ++i)
            {
                if (are_items_same(items[i], item))
                {
                    // Found it. Swap it to the front of the vector.
                    swap(items[i], items[0]);

                    // We don't set verified to true. If this stash was
                    // already unverified, it remains so.
                    return;
                }
            }

            // If this is unverified, forget last item on stack. This isn't
            // terribly clever, but it prevents the vector swelling forever.
            if (!verified)
                items.pop_back();

            // Items are different. We'll put this item in the front of our
            // vector, and mark this as unverified
            add_item(item, true);
            verified = false;
        }
    }
}

static bool _is_rottable(const item_def &item)
{
    if (is_shop_item(item))
        return false;
    return item.base_type == OBJ_CORPSES || item.is_type(OBJ_FOOD, FOOD_CHUNK);
}

static short _min_rot(const item_def &item)
{
    if (item.base_type == OBJ_FOOD)
        return 0;

    if (item.is_type(OBJ_CORPSES, CORPSE_SKELETON))
        return 0;

    if (!mons_skeleton(item.mon_type))
        return 0;
    else
        return -(FRESHEST_CORPSE);
}

// Returns the item name for a given item, with any appropriate
// stash-tracking pre/suffixes.
string Stash::stash_item_name(const item_def &item)
{
    string name = item.name(DESC_A);

    if (!_is_rottable(item))
        return name;

    if (item.stash_freshness <= _min_rot(item))
    {
        name += " " + jtrans("(gone by now)");
        return name;
    }

    // Skeletons show no signs of rotting before they're gone
    if (item.is_type(OBJ_CORPSES, CORPSE_SKELETON))
        return name;

    if (item.stash_freshness <= 0)
        name += " " + jtrans("(skeletalised by now)");

    return name;
}

class StashMenu : public InvMenu
{
public:
    StashMenu() : InvMenu(MF_SINGLESELECT), can_travel(false)
    {
        set_type(MT_PICKUP);
        set_tag("stash");       // override "inventory" tag
    }
    unsigned char getkey() const;
public:
    bool can_travel;
protected:
    void draw_title();
    int title_height() const;
    bool process_key(int key);
private:
    formatted_string create_title_string(bool wrap = true) const;
};

void StashMenu::draw_title()
{
    if (title)
    {
        cgotoxy(1, 1);
        create_title_string().display();

#ifdef USE_TILE_WEB
        webtiles_set_title(create_title_string(false));
#endif
    }
}

int StashMenu::title_height() const
{
    if (title)
        return 1 + (create_title_string().width() - 1) / get_number_of_cols();
    else
        return 0;
}

formatted_string StashMenu::create_title_string(bool wrap) const
{
    formatted_string fs = formatted_string(title->colour);
    fs.cprintf("%s", title->text.c_str());
    if (title->quantity)
    {
        fs.cprintf(jtransc(", %d item%s"), title->quantity);
    }
    fs.cprintf(")");

    vector<string> extra_parts;

    string part = "[a-z: ";
    part += string(menu_action == ACT_EXAMINE ? "解説を見る" : "購入リストに追加");
    part += "  ?/!: ";
    part += string(menu_action == ACT_EXAMINE ? "購入リストに追加" : "解説を見る");
    part += "]";
    extra_parts.push_back(part);

    if (can_travel)
        extra_parts.emplace_back(jtrans("[ENTER: travel]"));

    int term_width = get_number_of_cols();
    int remaining = term_width - fs.width();
    unsigned int extra_idx = 0;
    while (static_cast<int>(extra_parts[extra_idx].length()) + 2 <= remaining
           || !wrap)
    {
        fs.cprintf("  %s", extra_parts[extra_idx].c_str());

        remaining -= extra_parts[extra_idx].length() + 2;
        extra_idx++;
        if (extra_idx >= extra_parts.size())
            break;
    }
    // XXX assuming only two rows are possible for now
    if (extra_idx < extra_parts.size())
    {
        fs.cprintf("%s", string(remaining, ' ').c_str());

        string second_line;
        for (unsigned int i = extra_idx; i < extra_parts.size(); ++i)
            second_line += string("  ") + extra_parts[i];

        fs.cprintf("%s%s",
                   string(term_width - second_line.length(), ' ').c_str(),
                   second_line.c_str());
    }

    return fs;
}

bool StashMenu::process_key(int key)
{
    if (key == CK_ENTER)
    {
        // Travel activates.
        lastch = 1;
        return false;
    }
    return Menu::process_key(key);
}

unsigned char StashMenu::getkey() const
{
    return lastch;
}

static MenuEntry *stash_menu_fixup(MenuEntry *me)
{
    const item_def *item = static_cast<const item_def *>(me->data);
    if (item->base_type == OBJ_GOLD)
    {
        me->quantity = 0;
        me->colour   = DARKGREY;
    }

    return me;
}

bool Stash::show_menu(const level_pos &prefix, bool can_travel,
                      const vector<item_def>* matching_items) const
{
    const string prefix_str = prefix.id.describe();
    const vector<item_def> *item_list = matching_items ? matching_items
                                                       : &items;
    StashMenu menu;

    MenuEntry *mtitle = new MenuEntry("Stash (" + prefix_str, MEL_TITLE);
    menu.can_travel   = can_travel;
    mtitle->quantity  = item_list->size();
    menu.set_title(mtitle);
    menu.load_items(InvMenu::xlat_itemvect(*item_list), stash_menu_fixup);

    vector<MenuEntry*> sel;
    while (true)
    {
        sel = menu.show();
        if (menu.getkey() == 1)
            return true;

        if (sel.size() != 1)
            break;

        item_def *item = static_cast<item_def *>(sel[0]->data);
        describe_item(*item);
    }
    return false;
}

string Stash::description() const
{
    if (!enabled || items.empty())
        return "";

    const item_def &item = items[0];
    string desc = stash_item_name(item);

    size_t sz = items.size();
    if (sz > 1)
    {
        char additionals[50];
        snprintf(additionals, sizeof additionals,
                " (...%u)",
                 (unsigned int) (sz - 1));
        desc += additionals;
    }
    return desc;
}

string Stash::feature_description() const
{
    return feat_desc;
}

bool Stash::matches_search(const string &prefix,
                           const base_pattern &search,
                           stash_search_result &res) const
{
    if (!enabled || items.empty() && feat == DNGN_FLOOR)
        return false;

    for (const item_def &item : items)
    {
        const string s   = stash_item_name(item);
        const string ann = stash_annotate_item(STASH_LUA_SEARCH_ANNOTATE, &item);
        if (search.matches(prefix + " " + ann + s))
        {
            if (!res.count++)
                res.match = s;
            res.matches += item.quantity;
            res.matching_items.push_back(item);
            continue;
        }

        if (is_dumpable_artefact(item))
        {
            const string desc =
                munge_description(get_item_description(item, false, true));

            if (search.matches(desc))
            {
                if (!res.count++)
                    res.match = s;
                res.matches += item.quantity;
                res.matching_items.push_back(item);
            }
        }
    }

    if (!res.matches && feat != DNGN_FLOOR)
    {
        const string fdesc = feature_description();
        if (!fdesc.empty() && search.matches(fdesc))
        {
            res.match = fdesc;
            res.matches = 1;
        }
    }

    if (res.matches)
    {
        res.stash = this;
        // XXX pos.pos looks lame. Lameness is not solicited.
        res.pos.pos.x = x;
        res.pos.pos.y = y;
    }

    return !!res.matches;
}

void Stash::_update_corpses(int rot_time)
{
    for (int i = items.size() - 1; i >= 0; i--)
    {
        item_def &item = items[i];

        if (!_is_rottable(item))
            continue;

        int new_rot = static_cast<int>(item.stash_freshness) - rot_time;

        if (new_rot <= _min_rot(item))
        {
            items.erase(items.begin() + i);
            continue;
        }
        item.stash_freshness = static_cast<short>(new_rot);
    }
}

void Stash::_update_identification()
{
    for (int i = items.size() - 1; i >= 0; i--)
    {
        god_id_item(items[i]);
        maybe_identify_base_type(items[i]);
    }
}

void Stash::add_item(const item_def &item, bool add_to_front)
{
    if (_is_rottable(item))
        StashTrack.update_corpses();

    if (add_to_front)
        items.insert(items.begin(), item);
    else
        items.push_back(item);

    seen_item(item);

    if (!_is_rottable(item))
        return;

    // item.freshness remains unchanged in the stash, to show how fresh it
    // was when last seen.  It's stash_freshness that's decayed over time.
    item_def &it = add_to_front ? items.front() : items.back();
    it.stash_freshness     = it.freshness;
}

void Stash::write(FILE *f, int refx, int refy, string place, bool identify)
    const
{
    if (!enabled || (items.empty() && verified))
        return;

    bool note_status = notes_are_active();
    activate_notes(false);

    fprintf(f, "(%d, %d%s%s)\n", x - refx, y - refy,
            place.empty() ? "" : ", ", OUTS(place));

    for (int i = 0; i < (int) items.size(); ++i)
    {
        item_def item = items[i];

        if (identify)
            _fully_identify_item(&item);

        string s = stash_item_name(item);

        string ann = userdef_annotate_item(STASH_LUA_DUMP_ANNOTATE, &item);

        if (!ann.empty())
        {
            trim_string(ann);
            ann = " " + ann;
        }

        fprintf(f, "  %s%s%s\n", OUTS(s), OUTS(ann),
            (!verified && (items.size() > 1 || i) ? " (still there?)" : ""));

        if (is_dumpable_artefact(item))
        {
            string desc =
                munge_description(get_item_description(item, false, true));

            // Kill leading and trailing whitespace
            desc.erase(desc.find_last_not_of(" \n\t") + 1);
            desc.erase(0, desc.find_first_not_of(" \n\t"));
            // If string is not-empty, pad out to a neat indent
            if (!desc.empty())
            {
                // Walk backwards and prepend indenting spaces to \n characters.
                for (int j = desc.length() - 1; j >= 0; --j)
                    if (desc[j] == '\n')
                        desc.insert(j + 1, " ");

                fprintf(f, "    %s\n", OUTS(desc));
            }
        }
    }

    if (items.size() <= 1 && !verified)
        fprintf(f, "  (unseen)\n");

    activate_notes(note_status);
}

void Stash::save(writer& outf) const
{
    // How many items on this square?
    marshallShort(outf, (short) items.size());

    marshallByte(outf, x);
    marshallByte(outf, y);

    marshallByte(outf, feat);
    marshallByte(outf, trap);

    marshallString(outf, feat_desc);

    // Note: Enabled save value is inverted logic, so that it defaults to true
    marshallByte(outf, ((verified? 1 : 0) | (!enabled? 2 : 0)));

    // And dump the items individually. We don't bother saving fields we're
    // not interested in (and don't anticipate being interested in).
    for (const item_def &item : items)
        marshallItem(outf, item, true);
}

void Stash::load(reader& inf)
{
    // How many items?
    int count = unmarshallShort(inf);

    x = unmarshallByte(inf);
    y = unmarshallByte(inf);

    feat =  static_cast<dungeon_feature_type>(unmarshallUByte(inf));
    trap =  static_cast<trap_type>(unmarshallUByte(inf));
    feat_desc = unmarshallString(inf);

    uint8_t flags = unmarshallUByte(inf);
    verified = (flags & 1) != 0;

    // Note: Enabled save value is inverted so it defaults to true.
    enabled  = (flags & 2) == 0;

    abspos = GXM * (int) y + x;

    // Zap out item vector, in case it's in use (however unlikely)
    items.clear();
    // Read in the items
    for (int i = 0; i < count; ++i)
    {
        item_def item;
        unmarshallItem(inf, item);

        items.push_back(item);
    }
}

ShopInfo::ShopInfo(int xp, int yp) : x(xp), y(yp), name(), shoptype(-1),
                                     visited(false), items()
{
    // Most of our initialization will be done externally; this class is really
    // a mildly glorified struct.
    const shop_struct *sh = get_shop(coord_def(x, y));
    if (sh)
        shoptype = sh->type;
}

void ShopInfo::add_item(const item_def &sitem, unsigned price)
{
    shop_item it;
    it.item  = sitem;
    it.price = price;
    items.push_back(it);
}

string ShopInfo::shop_item_name(const shop_item &si) const
{
    return make_stringf(jtransc("%s%s (%u gold)"),
                        Stash::stash_item_name(si.item).c_str(),
                        shop_item_unknown(si.item) ? (" " + jtrans(" (unknown)")).c_str() : "",
                        si.price);
}

string ShopInfo::shop_item_desc(const shop_item &si) const
{
    string desc;

    const iflags_t oldflags = si.item.flags;

    if (shoptype_identifies_stock(static_cast<shop_type>(shoptype)))
        const_cast<shop_item&>(si).item.flags |= ISFLAG_IDENT_MASK;

    if (is_dumpable_artefact(si.item))
    {
        desc = munge_description(get_item_description(si.item, false, true));
        trim_string(desc);

        // Walk backwards and prepend indenting spaces to \n characters
        for (int i = desc.length() - 1; i >= 0; --i)
            if (desc[i] == '\n')
                desc.insert(i + 1, " ");
    }

    if (oldflags != si.item.flags)
        const_cast<shop_item&>(si).item.flags = oldflags;

    return desc;
}

void ShopInfo::describe_shop_item(const shop_item &si) const
{
    const iflags_t oldflags = si.item.flags;

    if (shoptype_identifies_stock(static_cast<shop_type>(shoptype)))
    {
        const_cast<shop_item&>(si).item.flags |= ISFLAG_IDENT_MASK
            | ISFLAG_NOTED_ID | ISFLAG_NOTED_GET;
    }

    item_def it = static_cast<item_def>(si.item);
    describe_item(it);

    if (oldflags != si.item.flags)
        const_cast<shop_item&>(si).item.flags = oldflags;
}

class ShopItemEntry : public InvEntry
{
    bool on_list;

public:
    ShopItemEntry(const ShopInfo::shop_item &it,
                  const string &item_name,
                  menu_letter hotkey, bool _on_list) : InvEntry(it.item)
    {
        text = item_name;
        hotkeys[0] = hotkey;
        on_list = _on_list;
    }

    string get_text(const bool = false) const
    {
        ASSERT(level == MEL_ITEM);
        ASSERT(hotkeys.size());
        char buf[300];
        snprintf(buf, sizeof buf, " %c %c %s",
                 hotkeys[0], on_list ? '$' : '-', text.c_str());
        return string(buf);
    }
};

void ShopInfo::fill_out_menu(StashMenu &menu, const level_pos &place) const
{
    menu.clear();

    menu_letter hotkey;
    for (int i = 0, count = items.size(); i < count; ++i)
    {
        bool on_list = shopping_list.is_on_list(items[i].item, &place);
        ShopItemEntry *me = new ShopItemEntry(items[i],
                                              shop_item_name(items[i]),
                                              hotkey++, on_list);
        menu.add_entry(me);
    }
}

bool ShopInfo::show_menu(const level_pos &place,
                         bool can_travel) const
{
    const string place_str = place.id.describe();

    StashMenu menu;

    MenuEntry *mtitle = new MenuEntry(name + " (" + place_str, MEL_TITLE);
    menu.can_travel   = can_travel;
    menu.action_cycle = Menu::CYCLE_TOGGLE;
    menu.menu_action  = Menu::ACT_EXAMINE;
    mtitle->quantity  = items.size();
    menu.set_title(mtitle);

    if (items.empty())
    {
        MenuEntry *me = new MenuEntry(
                visited? "  (Shop is empty)" : "  (Shop contents are unknown)",
                MEL_ITEM,
                0,
                0);
        me->colour = DARKGREY;
        menu.add_entry(me);
    }
    else
        fill_out_menu(menu, place);

    vector<MenuEntry*> sel;
    while (true)
    {
        sel = menu.show();
        if (menu.getkey() == 1)
            return true;

        if (sel.size() != 1)
            break;

        const shop_item *item = static_cast<const shop_item *>(sel[0]->data);
        if (menu.menu_action == Menu::ACT_EXAMINE)
            describe_shop_item(*item);
        else
        {
            if (shopping_list.is_on_list(item->item, &place))
                shopping_list.del_thing(item->item, &place);
            else
                shopping_list.add_thing(item->item, item->price, &place);

            // If the shop has identical items (like stacks of food in a
            // food shop) then adding/removing one to the shopping list
            // will have the same effect on the others, so the other
            // identical items will need to be re-coloured.
            fill_out_menu(menu, place);
        }
    }
    return false;
}

string ShopInfo::description() const
{
    return name;
}

bool ShopInfo::matches_search(const string &prefix,
                              const base_pattern &search,
                              stash_search_result &res) const
{
    if (items.empty() && visited)
        return false;

    bool note_status = notes_are_active();
    activate_notes(false);

    bool match = false;

    for (const shop_item &item : items)
    {
        const string sname = shop_item_name(item);
        const string ann   = stash_annotate_item(STASH_LUA_SEARCH_ANNOTATE,
                                                 &item.item, true);

        bool thismatch = false;
        if (search.matches(prefix + " " + ann + sname))
            thismatch = true;
        else
        {
            string desc = shop_item_desc(item);
            if (search.matches(desc))
                thismatch = true;
        }

        if (thismatch)
        {
            if (!res.count++)
                res.match = sname;
            res.matches++;
            res.matching_items.push_back(item.item);
        }
    }

    if (!res.matches)
    {
        string shoptitle = prefix + " {shop} " + name;
        if (!visited && items.empty())
            shoptitle += "*";
        if (search.matches(shoptitle))
        {
            match = true;
            res.match = name;
        }
    }

    if (match || res.matches)
    {
        res.shop = this;
        res.pos.pos.x = x;
        res.pos.pos.y = y;
    }

    activate_notes(note_status);
    return match || res.matches;
}

vector<item_def> ShopInfo::inventory() const
{
    vector<item_def> ret;
    for (const shop_item &item : items)
        ret.push_back(item.item);
    return ret;
}

void ShopInfo::write(FILE *f, bool identify) const
{
    bool note_status = notes_are_active();
    activate_notes(false);
    fprintf(f, "[Shop] %s\n", OUTS(name));
    if (!items.empty())
    {
        for (shop_item item : items)
        {
            if (identify)
                _fully_identify_item(&item.item);

            fprintf(f, "  %s\n", OUTS(shop_item_name(item)));
            string desc = shop_item_desc(item);
            if (!desc.empty())
                fprintf(f, "    %s\n", OUTS(desc));
        }
    }
    else if (visited)
        fprintf(f, "  (Shop is empty)\n");
    else
        fprintf(f, "  (Shop contents are unknown)\n");

    activate_notes(note_status);
}

void ShopInfo::save(writer& outf) const
{
    marshallShort(outf, shoptype);

    int mangledx = (short) x;
    if (!visited)
        mangledx |= 1024;
    marshallShort(outf, mangledx);
    marshallShort(outf, (short) y);

    marshallShort(outf, (short) items.size());

    marshallString4(outf, name);

    for (const shop_item &item : items)
    {
        marshallItem(outf, item.item, true);
        marshallShort(outf, (short) item.price);
    }
}

void ShopInfo::load(reader& inf)
{
    shoptype = unmarshallShort(inf);

    x = unmarshallShort(inf);
    visited = !(x & 1024);
    x &= 0xFF;

    y = unmarshallShort(inf);

    int itemcount = unmarshallShort(inf);

    unmarshallString4(inf, name);
    for (int i = 0; i < itemcount; ++i)
    {
        shop_item item;
        unmarshallItem(inf, item.item);
        item.price = (unsigned) unmarshallShort(inf);
        items.push_back(item);
    }
}

LevelStashes::LevelStashes()
    : m_place(level_id::current()),
      m_stashes(),
      m_shops()
{
}

level_id LevelStashes::where() const
{
    return m_place;
}

Stash *LevelStashes::find_stash(coord_def c)
{
    // FIXME: is this really necessary?
    if (c.x == -1 || c.y == -1)
        c = you.pos();

    return map_find(m_stashes, (GXM * c.y) + c.x);
}

const Stash *LevelStashes::find_stash(coord_def c) const
{
    // FIXME: is this really necessary?
    if (c.x == -1 || c.y == -1)
        c = you.pos();

    return map_find(m_stashes, (GXM * c.y) + c.x);
}

const ShopInfo *LevelStashes::find_shop(const coord_def& c) const
{
    for (const ShopInfo &shop : m_shops)
        if (shop.isAt(c))
            return &shop;

    return nullptr;
}

bool LevelStashes::shop_needs_visit(const coord_def& c) const
{
    const ShopInfo *shop = find_shop(c);
    return shop && !shop->is_visited();
}

bool LevelStashes::needs_visit(const coord_def& c, bool autopickup,
                               bool sacrifice) const
{
    const Stash *s = find_stash(c);
    if (s && (s->unverified()
              || sacrifice && s->sacrificeable()
              || autopickup && s->pickup_eligible()))
    {
        return true;
    }
    return shop_needs_visit(c);
}

bool LevelStashes::needs_stop(const coord_def &c) const
{
    const Stash *s = find_stash(c);
    return s && s->unverified() && s->needs_stop();
}

bool LevelStashes::sacrificeable(const coord_def &c) const
{
    const Stash *s = find_stash(c);
    return s && s->sacrificeable();
}

ShopInfo &LevelStashes::get_shop(const coord_def& c)
{
    for (ShopInfo &shop : m_shops)
        if (shop.isAt(c))
            return shop;

    ShopInfo si(c.x, c.y);
    si.set_name(shop_name(c));
    m_shops.push_back(si);
    return get_shop(c);
}

// Updates the stash at p. Returns true if there was a stash at p, false
// otherwise.
bool LevelStashes::update_stash(const coord_def& c)
{
    Stash *s = find_stash(c);
    if (!s)
        return false;

    s->update();
    if (s->empty())
        kill_stash(*s);
    return true;
}

bool LevelStashes::unmark_trapping_nets(const coord_def &c)
{
    if (Stash *s = find_stash(c))
        return s->unmark_trapping_nets();
    else
        return false;
}

void LevelStashes::move_stash(const coord_def& from, const coord_def& to)
{
    ASSERT(from != to);

    Stash *s = find_stash(from);
    if (!s)
        return;

    int old_abs = s->abs_pos();
    s->x = to.x;
    s->y = to.y;
    m_stashes.insert(make_pair(s->abs_pos(), *s));
    m_stashes.erase(old_abs);
}

// Removes a Stash from the level.
void LevelStashes::kill_stash(const Stash &s)
{
    m_stashes.erase(s.abs_pos());
}

void LevelStashes::no_stash(int x, int y)
{
    Stash *s = find_stash(coord_def(x, y));
    bool en = false;
    if (s)
    {
        en = s->enabled = !s->enabled;
        s->update();
        if (s->empty())
            kill_stash(*s);
    }
    else
    {
        Stash newStash(x, y);
        newStash.enabled = false;

        m_stashes[ newStash.abs_pos() ] = newStash;
    }

    mpr(en? "I'll no longer ignore what I see on this square."
          : "Ok, I'll ignore what I see on this square.");
}

void LevelStashes::add_stash(int x, int y)
{
    Stash *s = find_stash(coord_def(x, y));
    if (s)
    {
        s->update();
        if (s->empty())
            kill_stash(*s);
    }
    else
    {
        Stash new_stash(x, y);
        if (!new_stash.empty())
            m_stashes[ new_stash.abs_pos() ] = new_stash;
    }
}

bool LevelStashes::is_current() const
{
    return m_place == level_id::current();
}

string LevelStashes::level_name() const
{
    return m_place.describe(true, true);
}

string LevelStashes::short_level_name() const
{
    return m_place.describe();
}

int LevelStashes::_num_enabled_stashes() const
{
    int rawcount = m_stashes.size();
    if (!rawcount)
        return 0;

    for (const auto &entry : m_stashes)
        if (!entry.second.enabled)
            --rawcount;

    return rawcount;
}

void LevelStashes::_waypoint_search(
        int n,
        vector<stash_search_result> &results) const
{
    level_pos waypoint = travel_cache.get_waypoint(n);
    if (!waypoint.is_valid() || waypoint.id != m_place)
        return;
    const Stash* stash = find_stash(waypoint.pos);
    if (!stash)
        return;
    stash_search_result res;
    stash->matches_search("", text_pattern(".*"), res);
    res.pos.id = m_place;
    results.push_back(res);
}

void LevelStashes::get_matching_stashes(
        const base_pattern &search,
        vector<stash_search_result> &results) const
{
    string lplace = "{" + m_place.describe() + "}";

    // a single digit or * means we're searching for waypoints' content.
    const string s = search.tostring();
    if (s == "*")
    {
        for (int i = 0; i < TRAVEL_WAYPOINT_COUNT; ++i)
            _waypoint_search(i, results);
        return;
    }
    else if (s.size() == 1 && s[0] >= '0' && s[0] <= '9')
    {
        _waypoint_search(s[0] - '0', results);
        return;
    }

    for (const auto &entry : m_stashes)
    {
        if (entry.second.enabled)
        {
            stash_search_result res;
            if (entry.second.matches_search(lplace, search, res))
            {
                res.pos.id = m_place;
                results.push_back(res);
            }
        }
    }

    for (const ShopInfo &shop : m_shops)
    {
        stash_search_result res;
        if (shop.matches_search(lplace, search, res))
        {
            res.pos.id = m_place;
            results.push_back(res);
        }
    }
}

void LevelStashes::_update_corpses(int rot_time)
{
    for (auto &entry : m_stashes)
        entry.second._update_corpses(rot_time);
}

void LevelStashes::_update_identification()
{
    for (auto &entry : m_stashes)
        entry.second._update_identification();
}

void LevelStashes::write(FILE *f, bool identify) const
{
    if (visible_stash_count() == 0)
        return;

    // very unlikely level names will be localized, but hey
    fprintf(f, "%s\n", OUTS(level_name()));

    for (const ShopInfo &shop : m_shops)
        shop.write(f, identify);

    if (m_stashes.size())
    {
        const Stash &s = m_stashes.begin()->second;
        int refx = s.getX(), refy = s.getY();
        string levname = short_level_name();
        for (const auto &entry : m_stashes)
            entry.second.write(f, refx, refy, levname, identify);
    }
    fprintf(f, "\n");
}

void LevelStashes::save(writer& outf) const
{
    // How many stashes on this level?
    marshallShort(outf, (short) m_stashes.size());

    m_place.save(outf);

    // And write the individual stashes
    for (const auto &entry : m_stashes)
        entry.second.save(outf);

    marshallShort(outf, (short) m_shops.size());
    for (const ShopInfo &shop : m_shops)
        shop.save(outf);
}

void LevelStashes::load(reader& inf)
{
    int size = unmarshallShort(inf);

    m_place.load(inf);

    m_stashes.clear();
    for (int i = 0; i < size; ++i)
    {
        Stash s;
        s.load(inf);
        if (!s.empty())
            m_stashes[ s.abs_pos() ] = s;
    }

    m_shops.clear();
    int shopc = unmarshallShort(inf);
    for (int i = 0; i < shopc; ++i)
    {
        ShopInfo si(0, 0);
        si.load(inf);
        m_shops.push_back(si);
    }
}

void LevelStashes::remove_shop(const coord_def& c)
{
    for (unsigned i = 0; i < m_shops.size(); ++i)
        if (m_shops[i].isAt(c))
        {
            m_shops.erase(m_shops.begin() + i);
            return;
        }
}

LevelStashes &StashTracker::get_current_level()
{
    return levels[level_id::current()];
}

LevelStashes *StashTracker::find_level(const level_id &id)
{
    return map_find(levels, id);
}

LevelStashes *StashTracker::find_current_level()
{
    return find_level(level_id::current());
}

bool StashTracker::update_stash(const coord_def& c)
{
    LevelStashes *lev = find_current_level();
    if (lev)
    {
        bool res = lev->update_stash(c);
        if (!lev->stash_count())
            remove_level();
        return res;
    }
    return false;
}

void StashTracker::move_stash(const coord_def& from, const coord_def& to)
{
    LevelStashes *lev = find_current_level();
    if (lev)
        lev->move_stash(from, to);
}

bool StashTracker::unmark_trapping_nets(const coord_def &c)
{
    if (LevelStashes *lev = find_current_level())
        return lev->unmark_trapping_nets(c);
    else
        return false;
}

void StashTracker::remove_level(const level_id &place)
{
    levels.erase(place);
}

void StashTracker::no_stash(int x, int y)
{
    LevelStashes &current = get_current_level();
    current.no_stash(x, y);
    if (!current.stash_count())
        remove_level();
}

void StashTracker::add_stash(int x, int y)
{
    LevelStashes &current = get_current_level();
    current.add_stash(x, y);

    if (!current.stash_count())
        remove_level();
}

void StashTracker::dump(const char *filename, bool identify) const
{
    FILE *outf = fopen_u(filename, "w");
    if (outf)
    {
        write(outf, identify);
        fclose(outf);
    }
}

void StashTracker::write(FILE *f, bool identify) const
{
    fprintf(f, "%s\n\n", OUTS(you.your_name));
    if (!levels.size())
        fprintf(f, "  You have no stashes.\n");
    else
    {
        for (const auto &entry : levels)
            entry.second.write(f, identify);
    }
}

void StashTracker::save(writer& outf) const
{
    // Time of last corpse update.
    marshallInt(outf, last_corpse_update);

    // How many levels have we?
    marshallShort(outf, (short) levels.size());

    // And ask each level to write itself to the tag
    for (const auto &entry : levels)
        entry.second.save(outf);
}

void StashTracker::load(reader& inf)
{
    // Time of last corpse update.
    last_corpse_update = unmarshallInt(inf);

    int count = unmarshallShort(inf);

    levels.clear();
    for (int i = 0; i < count; ++i)
    {
        LevelStashes st;
        st.load(inf);
        if (st.stash_count())
            levels[st.where()] = st;
    }
}

void StashTracker::update_visible_stashes()
{
    LevelStashes *lev = find_current_level();
    coord_def c;
    for (radius_iterator ri(you.pos(), LOS_DEFAULT); ri; ++ri)
    {
        const dungeon_feature_type feat = grd(*ri);

        if ((!lev || !lev->update_stash(*ri))
            && (_grid_has_perceived_item(*ri)
                || is_notable_terrain(feat)))
        {
            if (!lev)
                lev = &get_current_level();
            lev->add_stash(ri->x, ri->y);
        }

        if (feat == DNGN_ENTER_SHOP)
            get_shop(*ri);
    }

    if (lev && !lev->stash_count())
        remove_level();
}

#define SEARCH_SPAM_THRESHOLD 400
static string lastsearch;
static input_history search_history(15);

string StashTracker::stash_search_prompt()
{
    vector<string> opts;
    if (!lastsearch.empty())
    {
        const string disp = replace_all(lastsearch, "<", "<<");
        opts.push_back(
            make_stringf(jtransc("Enter for \"%s\""), disp.c_str()));
    }
    if (lastsearch != ".")
        opts.emplace_back(jtrans("? for help"));

    string prompt_qual =
        comma_separated_line(opts.begin(), opts.end(), ", ", ", ");

    if (!prompt_qual.empty())
        prompt_qual = " [" + prompt_qual + "]";

    return make_stringf(jtransc("Search for what%s? "), prompt_qual.c_str()) + " ";
}

void StashTracker::remove_shop(const level_pos &pos)
{
    LevelStashes *lev = find_level(pos.id);
    if (lev)
        lev->remove_shop(pos.pos);
}

class stash_search_reader : public line_reader
{
public:
    stash_search_reader(char *buf, size_t sz,
                        int wcol = get_number_of_cols())
        : line_reader(buf, sz, wcol)
    {
        set_input_history(&search_history);
#ifdef USE_TILE_WEB
        tag = "stash_search";
#endif
    }
protected:
    int process_key(int ch)
    {
        if (ch == '?' && !pos)
        {
            *buffer = 0;
            return ch;
        }
        return line_reader::process_key(ch);
    }
};

// helper for search_stashes
static bool _compare_by_distance(const stash_search_result& lhs,
                                 const stash_search_result& rhs)
{
    if (lhs.player_distance != rhs.player_distance)
    {
        // Sort by increasing distance
        return lhs.player_distance < rhs.player_distance;
    }
    else if (lhs.player_distance == 0)
    {
        // If on the same level, sort by distance to player.
        const int lhs_dist = grid_distance(you.pos(), lhs.pos.pos);
        const int rhs_dist = grid_distance(you.pos(), rhs.pos.pos);
        if (lhs_dist != rhs_dist)
            return lhs_dist < rhs_dist;
    }

    if (lhs.matches != rhs.matches)
    {
        // Then by decreasing number of matches
        return lhs.matches > rhs.matches;
    }
    else if (lhs.match != rhs.match)
    {
        // Then by name.
        return lhs.match < rhs.match;
    }
    else
        return false;
}

// helper for search_stashes
static bool _compare_by_name(const stash_search_result& lhs,
                             const stash_search_result& rhs)
{
    if (lhs.match != rhs.match)
    {
        // Sort by name
        return lhs.match < rhs.match;
    }
    else if (lhs.player_distance != rhs.player_distance)
    {
        // Then sort by increasing distance
        return lhs.player_distance < rhs.player_distance;
    }
    else if (lhs.matches != rhs.matches)
    {
        // Then by decreasing number of matches
        return lhs.matches > rhs.matches;
    }
    else
        return false;
}

void StashTracker::search_stashes()
{
    char buf[400];

    update_corpses();
    update_identification();

    stash_search_reader reader(buf, sizeof buf);

    bool validline = false;
    msgwin_prompt(stash_search_prompt());
    while (true)
    {
        int ret = reader.read_line();
        if (!ret)
        {
            validline = true;
            break;
        }
        else if (ret == '?')
        {
            show_stash_search_help();
            redraw_screen();
        }
        else
            break;
    }
    msgwin_reply(validline ? buf : "");

    clear_messages();
    if (!validline || (!*buf && lastsearch.empty()))
    {
        canned_msg(MSG_OK);
        return;
    }

    string csearch = *buf? buf : lastsearch;
    string help = lastsearch;
    lastsearch = csearch;

    if (csearch == "@")
        csearch = ".";
    bool curr_lev = (csearch[0] == '@');
    if (curr_lev)
        csearch.erase(0, 1);
    if (csearch == ".")
        curr_lev = true;

    vector<stash_search_result> results;

    base_pattern *search = nullptr;

    text_pattern tpat(csearch, true);
    search = &tpat;

    lua_text_pattern ltpat(csearch);

    if (lua_text_pattern::is_lua_pattern(csearch))
        search = &ltpat;

    if (!search->valid() && csearch != "*")
    {
        mpr_nojoin(MSGCH_PLAIN, jtrans("Your search expression is invalid."));
        lastsearch = help;
        return ;
    }

    get_matching_stashes(*search, results, curr_lev);

    if (results.empty())
    {
        mpr_nojoin(MSGCH_PLAIN, jtrans("Can't find anything matching that."));
        return;
    }

    if (results.size() > SEARCH_SPAM_THRESHOLD)
    {
        mpr_nojoin(MSGCH_PLAIN, jtrans("Too many matches; use a more specific search."));
        return;
    }

    bool sort_by_dist = true;
    bool show_as_stacks = true;
    for (const stash_search_result &result : results)
        if (!(result.matching_items.empty() && result.shop))
        {
            // Only split up stacks if at least one match is a
            // non-shop (and split anyway in the case of a
            // weapon shop and a search for "weapon").
            show_as_stacks = false;
            break;
        }
    bool filter_useless = false;
    bool default_execute = true;
    while (true)
    {
        // Note that sort_by_dist and show_as_stacks can be modified by the
        // following call if requested by the user. Also, "results" will be
        // sorted by the call as appropriate:
        const bool again = display_search_results(results,
                                                  sort_by_dist,
                                                  show_as_stacks,
                                                  filter_useless,
                                                  default_execute);
        if (!again)
            break;
    }
}

void StashTracker::get_matching_stashes(
        const base_pattern &search,
        vector<stash_search_result> &results,
        bool curr_lev)
    const
{
    level_id curr = level_id::current();
    for (const auto &entry : levels)
    {
        if (curr_lev && curr != entry.first)
            continue;
        entry.second.get_matching_stashes(search, results);
        if (results.size() > SEARCH_SPAM_THRESHOLD)
            return;
    }

    for (stash_search_result &result : results)
    {
        int ldist = level_distance(curr, result.pos.id);
        if (ldist == -1)
            ldist = 1000;

        result.player_distance = ldist;
    }
}

class StashSearchMenu : public Menu
{
public:
    StashSearchMenu(const char* stack_style_,const char* sort_style_,const char* filtered_)
        : Menu(), can_travel(true),
          request_toggle_sort_method(false),
          request_toggle_show_as_stack(false),
          request_toggle_filter_useless(false),
          stack_style(stack_style_),
          sort_style(sort_style_),
          filtered(filtered_)
    { }

public:
    bool can_travel;
    bool request_toggle_sort_method;
    bool request_toggle_show_as_stack;
    bool request_toggle_filter_useless;
    const char* stack_style;
    const char* sort_style;
    const char* filtered;

protected:
    bool process_key(int key);
    void draw_title();
};

void StashSearchMenu::draw_title()
{
    if (title)
    {
        cgotoxy(1, 1);
        formatted_string fs = formatted_string(title->colour);
        fs.cprintf("%d件%s",
                   title->quantity, title->text.c_str());
        fs.display();

#ifdef USE_TILE_WEB
        webtiles_set_title(fs);
#endif

        draw_title_suffix(formatted_string::parse_string(make_stringf(jtransc(
                 "<lightgrey>"
                 ": <w>%s</w> [toggle: <w>!</w>],"
                 " <w>%s</w> stacks [<w>-</w>],"
                 " by <w>%s</w> [<w>/</w>],"
                 " <w>%s</w> useless [<w>=</w>]"
                 "</lightgrey>"),
                 menu_action == ACT_EXECUTE ? "対象まで移動" : "解説文を見る",
                 stack_style, sort_style, filtered)), false);
    }
}

bool StashSearchMenu::process_key(int key)
{
    if (key == '/')
    {
        request_toggle_sort_method = true;
        return false;
    }
    else if (key == '-')
    {
        request_toggle_show_as_stack = true;
        return false;
    }
    else if (key == '=')
    {
        request_toggle_filter_useless = true;
        return false;
    }

    return Menu::process_key(key);
}

string ShopInfo::get_shop_item_name(const item_def& search_item) const
{
    // Rely on items_similar, rnd, quantity to see if the item_def object is in
    // the shop (extremely unlikely to be cheated and only consequence would be a
    // wrong name showing up in the stash search):
    for (const shop_item &item : items)
    {
        if (items_similar(item.item, search_item)
            && item.item.rnd == search_item.rnd
            && item.item.quantity == search_item.quantity)
        {
            return shop_item_name(item);
        }
    }
    return "";
}

static void _stash_filter_useless(const vector<stash_search_result> &in,
                                  vector<stash_search_result> &out)
{
    // Creates search results vector with useless items filtered
    out.clear();
    out.reserve(in.size());
    for (const stash_search_result &res : in)
    {
        vector<item_def> items;

        // expand shop inventory
        if (res.matching_items.empty() && res.shop)
            items = res.shop->inventory();
        else if (!res.count)
        {
            //don't filter features
            out.push_back(res);
            continue;
        }
        else
            items = res.matching_items;

        stash_search_result tmp = res;

        tmp.count = 0;
        tmp.matches = 0;
        tmp.matching_items.clear();
        for (const item_def &item : items)
        {
            if (is_useless_item(item, false))
                continue;

            if (!tmp.count)
            {
                //find new 'first' item name
                tmp.match = Stash::stash_item_name(item);
                if (tmp.shop)
                {
                    // Need to check if the item is in the shop so we can add gold price...
                    string sn = tmp.shop->get_shop_item_name(item);
                    if (!sn.empty())
                        tmp.match=sn;
                }
            }
            tmp.matching_items.push_back(item);
            tmp.matches += item.quantity;
            tmp.count++;
        }

        if (tmp.count > 0)
            out.push_back(tmp);
    }
}

static void _stash_flatten_results(const vector<stash_search_result> &in,
                                   vector<stash_search_result> &out)
{
    // Creates search results vector with at most one item in each entry
    out.clear();
    out.reserve(in.size() * 2);
    for (const stash_search_result &res : in)
    {
        vector<item_def> items;

        // expand shop inventory
        if (res.matching_items.empty() && res.shop)
            items = res.shop->inventory();
        else if (res.count < 2)
        {
            out.push_back(res);
            continue;
        }
        else
            items = res.matching_items;

        stash_search_result tmp = res;
        tmp.count = 1;
        for (const item_def &item : items)
        {
            tmp.match = Stash::stash_item_name(item);
            if (tmp.shop)
            {
                // Need to check if the item is in the shop so we can add gold price...
                // tmp.shop->shop_item_name()
                string sn = tmp.shop->get_shop_item_name(item);
                if (!sn.empty())
                    tmp.match=sn;
            }
            tmp.matches = item.quantity;
            tmp.matching_items.clear();
            tmp.matching_items.push_back(item);
            out.push_back(tmp);
        }
    }
}

// Returns true to request redisplay if display method was toggled
bool StashTracker::display_search_results(
    vector<stash_search_result> &results_in,
    bool& sort_by_dist,
    bool& show_as_stacks,
    bool& filter_useless,
    bool& default_execute)
{
    if (results_in.empty())
        return false;

    vector<stash_search_result> * results = &results_in;
    vector<stash_search_result> results_single_items;
    vector<stash_search_result> results_filtered;

    if (filter_useless)
    {
        _stash_filter_useless(results_in, results_filtered);
        if (!show_as_stacks)
        {
            _stash_flatten_results(results_filtered, results_single_items);
            results = &results_single_items;
        }
        else
            results = &results_filtered;
    }
    else
    {
        if (!show_as_stacks)
        {
            _stash_flatten_results(results_in, results_single_items);
            results = &results_single_items;
        }
    }

    if (sort_by_dist)
        stable_sort(results->begin(), results->end(), _compare_by_distance);
    else
        stable_sort(results->begin(), results->end(), _compare_by_name);

    StashSearchMenu stashmenu(show_as_stacks ? "を隠す" : "も見る",
                              sort_by_dist ? "距離" : "名前",
                              filter_useless ? "を隠す" : "も見る");
    stashmenu.set_tag("stash");
    stashmenu.can_travel   = can_travel_interlevel();
    stashmenu.action_cycle = Menu::CYCLE_TOGGLE;
    stashmenu.menu_action  = default_execute ? Menu::ACT_EXECUTE : Menu::ACT_EXAMINE;
    string title = "一致";

    MenuEntry *mtitle = new MenuEntry(title, MEL_TITLE);
    // Abuse of the quantity field.
    mtitle->quantity = results->size();
    stashmenu.set_title(mtitle);

    // Don't make a menu so tall that we recycle hotkeys on the same page.
    if (results->size() > 52
        && (stashmenu.maxpagesize() > 52 || stashmenu.maxpagesize() == 0))
    {
        stashmenu.set_maxpagesize(52);
    }

    menu_letter hotkey;
    for (stash_search_result &res : *results)
    {
        ostringstream matchtitle;
        if (const uint8_t waypoint = travel_cache.is_waypoint(res.pos))
            matchtitle << "(" << waypoint << ") ";
        else
            matchtitle << "    ";

        matchtitle << "[" << align_centre(res.pos.id.describe_j(), MAX_BRANCH_ABBREV_SIZE) << "] "
                   << res.match;

        if (res.matches > 1 && res.count > 1)
            matchtitle << " (+" << (res.matches - 1) << ")";

        MenuEntry *me = new MenuEntry(matchtitle.str(), MEL_ITEM, 1, hotkey);
        me->data = &res;

        if (res.shop && !res.shop->is_visited())
            me->colour = CYAN;

        if (!res.matching_items.empty())
        {
            const item_def &first(*res.matching_items.begin());
            const int itemcol = menu_colour(first.name(DESC_PLAIN).c_str(),
                                            item_prefix(first), "pickup");
            if (itemcol != -1)
                me->colour = itemcol;
        }

        stashmenu.add_entry(me);
        ++hotkey;
    }

    stashmenu.set_flags(MF_SINGLESELECT);

    vector<MenuEntry*> sel;
    while (true)
    {
        sel = stashmenu.show();

        default_execute = stashmenu.menu_action == Menu::ACT_EXECUTE;
        if (stashmenu.request_toggle_sort_method)
        {
            sort_by_dist = !sort_by_dist;
            return true;
        }

        if (stashmenu.request_toggle_show_as_stack)
        {
            show_as_stacks = !show_as_stacks;
            return true;
        }

        if (stashmenu.request_toggle_filter_useless)
        {
            filter_useless = !filter_useless;
            return true;
        }

        if (sel.size() == 1
            && stashmenu.menu_action == StashSearchMenu::ACT_EXAMINE)
        {
            stash_search_result *res =
                static_cast<stash_search_result *>(sel[0]->data);

            bool dotravel = res->show_menu();

            if (dotravel && can_travel_to(res->pos.id))
            {
                redraw_screen();
                level_pos lp = res->pos;
                if (show_map(lp, true, true, true))
                {
                    start_translevel_travel(lp);
                    return false;
                }
            }
            continue;
        }
        break;
    }

    redraw_screen();
    if (sel.size() == 1 && stashmenu.menu_action == Menu::ACT_EXECUTE)
    {
        const stash_search_result *res =
                static_cast<stash_search_result *>(sel[0]->data);
        level_pos lp = res->pos;
        if (show_map(lp, true, true, true))
            start_translevel_travel(lp);
        else
            return true;
    }
    return false;
}

void StashTracker::update_corpses()
{
    if (you.elapsed_time - last_corpse_update < ROT_TIME_FACTOR)
        return;

    const int rot_time =
        (you.elapsed_time - last_corpse_update) / ROT_TIME_FACTOR;

    last_corpse_update = you.elapsed_time;

    for (auto &entry : levels)
        entry.second._update_corpses(rot_time);
}

void StashTracker::update_identification()
{
    if (!you_worship(GOD_ASHENZARI))
        return;

    for (auto &entry : levels)
        entry.second._update_identification();
}

//////////////////////////////////////////////

ST_ItemIterator::ST_ItemIterator()
{
    m_stash_level_it = StashTrack.levels.begin();
    new_level();
    //(*this)++;
}

ST_ItemIterator::operator bool() const
{
    return m_item != nullptr;
}

const item_def& ST_ItemIterator::operator *() const
{
    return *m_item;
}

const item_def* ST_ItemIterator::operator->() const
{
    return m_item;
}

const level_id &ST_ItemIterator::place()
{
    return m_place;
}

const ShopInfo* ST_ItemIterator::shop()
{
    return m_shop;
}

unsigned        ST_ItemIterator::price()
{
    return m_price;
}

const ST_ItemIterator& ST_ItemIterator::operator ++ ()
{
    m_item = nullptr;
    m_shop = nullptr;

    const LevelStashes &ls = m_stash_level_it->second;

    if (m_stash_it == ls.m_stashes.end())
    {
        if (m_shop_it == ls.m_shops.end())
        {
            m_stash_level_it++;
            if (m_stash_level_it == StashTrack.levels.end())
                return *this;

            new_level();
            return *this;
        }
        m_shop = &(*m_shop_it);

        if (m_shop_item_it != m_shop->items.end())
        {
            const ShopInfo::shop_item &item = *m_shop_item_it++;
            m_item  = &(item.item);
            ASSERT(m_item->defined());
            m_price = item.price;
            return *this;
        }

        m_shop_it++;
        if (m_shop_it != ls.m_shops.end())
            m_shop_item_it = m_shop_it->items.begin();

        ++(*this);
    }
    else
    {
        if (m_stash_item_it != m_stash_it->second.items.end())
        {
            m_item = &(*m_stash_item_it++);
            ASSERT(m_item->defined());
            return *this;
        }

        m_stash_it++;
        if (m_stash_it == ls.m_stashes.end())
        {
            ++(*this);
            return *this;
        }

        m_stash_item_it = m_stash_it->second.items.begin();
        ++(*this);
    }

    return *this;
}

void ST_ItemIterator::new_level()
{
    m_item  = nullptr;
    m_shop  = nullptr;
    m_price = 0;

    if (m_stash_level_it == StashTrack.levels.end())
        return;

    const LevelStashes &ls = m_stash_level_it->second;

    m_place = ls.m_place;

    m_stash_it = ls.m_stashes.begin();
    if (m_stash_it != ls.m_stashes.end())
    {
        m_stash_item_it = m_stash_it->second.items.begin();
        if (m_stash_item_it != m_stash_it->second.items.end())
        {
            m_item = &(*m_stash_item_it++);
            ASSERT(m_item->defined());
        }
    }

    m_shop_it = ls.m_shops.begin();
    if (m_shop_it != ls.m_shops.end())
    {
        const ShopInfo &si = *m_shop_it;

        m_shop_item_it = si.items.begin();

        if (m_item == nullptr && m_shop_item_it != si.items.end())
        {
            const ShopInfo::shop_item &item = *m_shop_item_it++;
            m_item  = &(item.item);
            ASSERT(m_item->defined());
            m_price = item.price;
            m_shop  = &si;
        }
    }
}

ST_ItemIterator ST_ItemIterator::operator ++ (int)
{
    const ST_ItemIterator copy = *this;
    ++(*this);
    return copy;
}

bool stash_search_result::show_menu() const
{
    if (shop)
        return shop->show_menu(pos, can_travel_to(pos.id));
    else if (stash)
        return stash->show_menu(pos, can_travel_to(pos.id), &matching_items);
    else
        return false;
}
