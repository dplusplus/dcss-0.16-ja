/**
 * @file
 * @brief Functions used to print messages.
**/

#include "AppHdr.h"

#include "message.h"

#include <sstream>

#include "areas.h"
#include "colour.h"
#include "database.h"
#include "delay.h"
#include "english.h"
#include "hints.h"
#include "initfile.h"
#include "libutil.h"
#ifdef WIZARD
 #include "luaterp.h"
#endif
#include "menu.h"
#include "monster.h"
#include "mon-util.h"
#include "notes.h"
#include "output.h"
#include "religion.h"
#include "state.h"
#include "stringutil.h"
#ifdef USE_TILE_WEB
 #include "tileweb.h"
#endif
#include "unwind.h"
#include "view.h"

static void _mpr(string text, msg_channel_type channel=MSGCH_PLAIN, int param=0,
                 bool nojoin=false, bool cap=true);

void mpr(const string &text)
{
    _mpr(text);
}

void mpr_nojoin(msg_channel_type channel, string text)
{
    _mpr(text, channel, 0, true);
}

void mpr_nojoin(msg_channel_type channel, int param, string text)
{
    _mpr(text, channel, param, true);
}

static bool _ends_in_punctuation(const string& text)
{
    switch (text[text.size() - 1])
    {
    case '.':
    case '!':
    case '?':
    case ',':
    case ';':
    case ':':
        return true;
    default:
        return false;
    }
}

struct message_item
{
    msg_channel_type    channel;        // message channel
    int                 param;          // param for channel (god, enchantment)
    string              text;           // text of message (tagged string...)
    int                 repeats;
    int                 turn;
    bool                join;           // may this message be joined with
                                        // others?

    message_item() : channel(NUM_MESSAGE_CHANNELS), param(0),
                     text(""), repeats(0), turn(-1), join(true)
    {
    }

    message_item(string msg, msg_channel_type chan, int par, bool jn)
        : channel(chan), param(par), text(msg), repeats(1),
          turn(you.num_turns)
    {
         // Don't join long messages.
         join = jn && strwidth(pure_text()) < 40;
    }

    // Constructor for restored messages.
    message_item(string msg, msg_channel_type chan, int par, int rep, int trn)
        : channel(chan), param(par), text(msg), repeats(rep),
          turn(trn), join(false)
    {
    }

    operator bool() const
    {
        return repeats > 0;
    }

    string pure_text() const
    {
        return formatted_string::parse_string(text).tostring();
    }

    string with_repeats() const
    {
        // TODO: colour the repeats indicator?
        string rep = "";
        if (repeats > 1)
            rep = make_stringf(" (x%d)", repeats);
        return text + rep;
    }

    string pure_text_with_repeats() const
    {
        string rep = "";
        if (repeats > 1)
            rep = make_stringf(" (x%d)", repeats);
        return pure_text() + rep;
    }

    // Tries to condense the argument into this message.
    // Either *this needs to be an empty item, or it must be the
    // same as the argument.
    bool merge(const message_item& other)
    {
        if (! *this)
        {
            *this = other;
            return true;
        }

        if (!Options.msg_condense_repeats)
            return false;
        if (other.channel == channel && other.param == param)
        {
            if (Options.msg_condense_repeats && other.text == text)
            {
                repeats += other.repeats;
                return true;
            }
            else if (Options.msg_condense_short
                     && turn == other.turn
                     && repeats == 1 && other.repeats == 1
                     && join && other.join
                     && _ends_in_punctuation(pure_text())
                        == _ends_in_punctuation(other.pure_text()))
            {
                // Note that join stays true.

                string sep = "<lightgrey>";
                int seplen = 1;
                if (!_ends_in_punctuation(pure_text()))
                {
                    seplen++;
                }
                sep += " </lightgrey>";
                if (strwidth(pure_text()) + seplen + strwidth(other.pure_text())
                    > (int)msgwin_line_length())
                {
                    return false;
                }

                text += sep;
                text += other.text;
                return true;
            }
        }
        return false;
    }
};

static int _mod(int num, int denom)
{
    ASSERT(denom > 0);
    div_t res = div(num, denom);
    return res.rem >= 0 ? res.rem : res.rem + denom;
}

template <typename T, int SIZE>
class circ_vec
{
    T data[SIZE];

    int end;   // first unfilled index

    static void inc(int* index)
    {
        ASSERT_RANGE(*index, 0, SIZE);
        *index = _mod(*index + 1, SIZE);
    }

    static void dec(int* index)
    {
        ASSERT_RANGE(*index, 0, SIZE);
        *index = _mod(*index - 1, SIZE);
    }

public:
    circ_vec() : end(0) {}

    void clear()
    {
        end = 0;
        for (int i = 0; i < SIZE; ++i)
            data[i] = T();
    }

    int size() const
    {
        return SIZE;
    }

    T& operator[](int i)
    {
        ASSERT(_mod(i, SIZE) < size());
        return data[_mod(end + i, SIZE)];
    }

    const T& operator[](int i) const
    {
        ASSERT(_mod(i, SIZE) < size());
        return data[_mod(end + i, SIZE)];
    }

    void push_back(const T& item)
    {
        data[end] = item;
        inc(&end);
    }

    void roll_back(int n)
    {
        for (int i = 0; i < n; ++i)
        {
            dec(&end);
            data[end] = T();
        }
    }
};

static void readkey_more(bool user_forced=false);

// Types of message prefixes.
// Higher values override lower.
enum prefix_type
{
    P_NONE,
    P_TURN_START,
    P_TURN_END,
    P_NEW_CMD, // new command, but no new turn
    P_NEW_TURN,
    P_FULL_MORE,   // single-character more prompt (full window)
    P_OTHER_MORE,  // the other type of --more-- prompt
};

// Could also go with coloured glyphs.
static cglyph_t _prefix_glyph(prefix_type p)
{
    cglyph_t g;
    switch (p)
    {
    case P_TURN_START:
        g.ch = Options.show_newturn_mark ? '-' : ' ';
        g.col = LIGHTGRAY;
        break;
    case P_TURN_END:
    case P_NEW_TURN:
        g.ch = Options.show_newturn_mark ? '_' : ' ';
        g.col = LIGHTGRAY;
        break;
    case P_NEW_CMD:
        g.ch = Options.show_newturn_mark ? '_' : ' ';
        g.col = DARKGRAY;
        break;
    case P_FULL_MORE:
        g.ch = '+';
        g.col = channel_to_colour(MSGCH_PROMPT);
        break;
    case P_OTHER_MORE:
        g.ch = '+';
        g.col = LIGHTRED;
        break;
    default:
        g.ch = ' ';
        g.col = LIGHTGRAY;
        break;
    }
    return g;
}

static bool _pre_more();

static bool _temporary = false;

class message_window
{
    int next_line;
    int temp_line;     // starting point of temporary messages
    int input_line;    // last line-after-input
    vector<formatted_string> lines;
    prefix_type prompt; // current prefix prompt

    int height() const
    {
        return crawl_view.msgsz.y;
    }

    int use_last_line() const
    {
        return first_col_more();
    }

    int width() const
    {
        return crawl_view.msgsz.x;
    }

    void out_line(const formatted_string& line, int n) const
    {
        cgotoxy(1, n + 1, GOTO_MSG);
        line.display();
        cprintf("%*s", width() - line.width(), "");
    }

    // Place cursor at end of last non-empty line to handle prompts.
    // TODO: might get rid of this by clearing the whole window when writing,
    //       and then just writing the actual non-empty lines.
    void place_cursor() const
    {
        // XXX: the screen may have resized since the last time we
        //  called lines.resize().  We can't actually resize lines
        //  here because this is a const method.  Consider only the
        //  last height() lines if this has happened.
        const int diff = max(int(lines.size()) - height(), 0);

        int i;
        for (i = lines.size() - 1; i >= diff && lines[i].width() == 0; --i);
        if (i >= diff && (int) lines[i].width() < crawl_view.msgsz.x)
            cgotoxy(lines[i].width() + 1, i - diff + 1, GOTO_MSG);
        else if (i < diff)
        {
            // If there were no lines, put the cursor at the upper left.
            cgotoxy(1, 1, GOTO_MSG);
        }
    }

    // Whether to show msgwin-full more prompts.
    bool more_enabled() const
    {
        return crawl_state.show_more_prompt
               && (Options.clear_messages || Options.show_more);
    }

    int make_space(int n)
    {
        int space = out_height() - next_line;

        if (space >= n)
            return 0;

        int s = 0;
        if (input_line > 0)
        {
            s = min(input_line, n - space);
            scroll(s);
            space += s;
        }

        if (space >= n)
            return s;

        if (more_enabled())
            more(true);

        // We could consider just scrolling off after --more--;
        // that would require marking the last message before
        // the prompt.
        if (!Options.clear_messages && !more_enabled())
        {
            scroll(n - space);
            return s + n - space;
        }
        else
        {
            clear();
            return height();
        }
    }

    void add_line(const formatted_string& line)
    {
        resize(); // TODO: get rid of this
        lines[next_line] = line;
        next_line++;
    }

    void output_prefix(prefix_type p)
    {
        if (!use_first_col())
            return;
        if (p <= prompt)
            return;
        prompt = p;
        if (next_line > 0)
        {
            formatted_string line;
            line.add_glyph(_prefix_glyph(prompt));
            lines[next_line-1].del_char();
            line += lines[next_line-1];
            lines[next_line-1] = line;
        }
        show();
    }

public:
    message_window()
        : next_line(0), temp_line(0), input_line(0), prompt(P_NONE)
    {
        clear_lines(); // initialize this->lines
    }

    void resize()
    {
        // XXX: broken (why?)
        lines.resize(height());
    }

    unsigned int out_width() const
    {
        return width() - (use_first_col() ? 1 : 0);
    }

    unsigned int out_height() const
    {
        return height() - (use_last_line() ? 0 : 1);
    }

    void clear_lines()
    {
        lines.clear();
        lines.resize(height());
    }

    bool first_col_more() const
    {
        return Options.small_more;
    }

    bool use_first_col() const
    {
        return !Options.clear_messages;
    }

    void set_starting_line()
    {
        // TODO: start at end (sometimes?)
        next_line = 0;
        input_line = 0;
        temp_line = 0;
    }

    void clear()
    {
        clear_lines();
        set_starting_line();
        show();
    }

    void scroll(int n)
    {
        ASSERT(next_line >= n);
        int i;
        for (i = 0; i < height() - n; ++i)
            lines[i] = lines[i + n];
        for (; i < height(); ++i)
            lines[i].clear();
        next_line -= n;
        temp_line -= n;
        input_line -= n;
    }

    // write to screen (without refresh)
    void show() const
    {
        // XXX: this should not be necessary as formatted_string should
        //      already do it
        textcolour(LIGHTGREY);

        // XXX: the screen may have resized since the last time we
        //  called lines.resize().  We can't actually resize lines
        //  here because this is a const method.  Display the last
        //  height() lines if this has happened.
        const int diff = max(int(lines.size()) - height(), 0);

        for (size_t i = diff; i < lines.size(); ++i)
            out_line(lines[i], i - diff);
        place_cursor();
#ifdef USE_TILE
        tiles.set_need_redraw();
#endif
    }

    // temporary: to be overwritten with next item, e.g. new turn
    //            leading dash or prompt without response
    void add_item(string text, prefix_type first_col = P_NONE,
                  bool temporary = false)
    {
        prompt = P_NONE; // reset prompt

        vector<formatted_string> newlines;
        linebreak_string(text, out_width());
        formatted_string::parse_string_to_multiple(text, newlines);

        for (const formatted_string &nl : newlines)
        {
            make_space(1);
            formatted_string line;
            if (use_first_col())
                line.add_glyph(_prefix_glyph(first_col));
            line += nl;
            add_line(line);
        }

        if (!temporary)
            reset_temp();

        show();
    }

    void roll_back()
    {
        temp_line = max(temp_line, 0);
        for (int i = temp_line; i < next_line; ++i)
            lines[i].clear();
        next_line = temp_line;
    }

    /**
     * Consider any formerly-temporary messages permanent.
     */
    void reset_temp()
    {
        temp_line = next_line;
    }

    void got_input()
    {
        input_line = next_line;
    }

    void new_cmdturn(bool new_turn)
    {
        output_prefix(new_turn ? P_NEW_TURN : P_NEW_CMD);
    }

    bool any_messages()
    {
        return next_line > input_line;
    }

    /*
     * Handling of more prompts (both types).
     */
    void more(bool full, bool user=false)
    {
        if (_pre_more())
            return;

        show();
        int last_row = crawl_view.msgsz.y;
        if (first_col_more())
        {
            cgotoxy(1, last_row, GOTO_MSG);
            cglyph_t g = _prefix_glyph(full ? P_FULL_MORE : P_OTHER_MORE);
            formatted_string f;
            f.add_glyph(g);
            f.display();
            // Move cursor back for nicer display.
            cgotoxy(1, last_row, GOTO_MSG);
            // Need to read_key while cursor_control in scope.
            cursor_control con(true);
            readkey_more();
        }
        else
        {
            cgotoxy(use_first_col() ? 2 : 1, last_row, GOTO_MSG);
            textcolour(channel_to_colour(MSGCH_PROMPT));
            if (crawl_state.game_is_hints())
            {
                string more_str;
                more_str = make_stringf("--続く-- 続けるには%sしてください。 "
                                        "後でCtrl-Pを押すことで再度読むこともできます。",
                                        is_tiles() ? "Spaceを押すか画面をクリック" : "Spaceを押");

                cprintf(more_str.c_str());
            }
            else
                cprintf(jtransc("--more--"));

            readkey_more(user);
        }
    }
};

message_window msgwin;

void display_message_window()
{
    msgwin.show();
}

void clear_message_window()
{
    msgwin = message_window();
}

void scroll_message_window(int n)
{
    msgwin.scroll(n);
    msgwin.show();
}

bool any_messages()
{
    return msgwin.any_messages();
}

typedef circ_vec<message_item, NUM_STORED_MESSAGES> store_t;

class message_store
{
    store_t msgs;
    message_item prev_msg;
    bool last_of_turn;
    int temp; // number of temporary messages

#ifdef USE_TILE_WEB
    int unsent; // number of messages not yet sent to the webtiles client
    int client_rollback;
    bool send_ignore_one;
#endif

public:
    message_store() : last_of_turn(false), temp(0)
#ifdef USE_TILE_WEB
                      , unsent(0), client_rollback(0), send_ignore_one(false)
#endif
    {}

    void add(const message_item& msg)
    {
        if (msg.channel != MSGCH_PROMPT && prev_msg.merge(msg))
            return;
        flush_prev();
        prev_msg = msg;
        if (msg.channel == MSGCH_PROMPT || _temporary)
            flush_prev();
    }

    bool have_prev()
    {
        return prev_msg;
    }

    void store_msg(const message_item& msg)
    {
        prefix_type p = P_NONE;
        msgs.push_back(msg);
        if (_temporary)
            temp++;
        else
            reset_temp();
#ifdef USE_TILE_WEB
        // ignore this message until it's actually displayed in case we run out
        // of space and have to display --more-- instead
        send_ignore_one = true;
#endif
        msgwin.add_item(msg.with_repeats(), p, _temporary);
#ifdef USE_TILE_WEB
        send_ignore_one = false;
#endif
    }

    void roll_back()
    {
#ifdef USE_TILE_WEB
        client_rollback = max(0, temp - unsent);
        unsent = max(0, unsent - temp);
#endif
        msgs.roll_back(temp);
        temp = 0;
    }

    void reset_temp()
    {
        temp = 0;
    }

    void flush_prev()
    {
        if (!prev_msg)
            return;
        message_item msg = prev_msg;
        // Clear prev_msg before storing it, since
        // writing out to the message window might
        // in turn result in a recursive flush_prev.
        prev_msg = message_item();
#ifdef USE_TILE_WEB
        unsent++;
#endif
        store_msg(msg);
        if (last_of_turn)
        {
            msgwin.new_cmdturn(true);
            last_of_turn = false;
        }
    }

    void new_turn()
    {
        if (prev_msg)
            last_of_turn = true;
        else
            msgwin.new_cmdturn(true);
    }

    // XXX: this should not need to exist
    const store_t& get_store()
    {
        return msgs;
    }

    void clear()
    {
        msgs.clear();
        prev_msg = message_item();
        last_of_turn = false;
        temp = 0;
    }

#ifdef USE_TILE_WEB
    void send()
    {
        if (unsent == 0 || (send_ignore_one && unsent == 1)) return;

        if (client_rollback > 0)
        {
            tiles.json_write_int("rollback", client_rollback);
            client_rollback = 0;
        }
        tiles.json_open_array("messages");
        for (int i = -unsent; i < (send_ignore_one ? -1 : 0); ++i)
        {
            message_item& msg = msgs[i];
            tiles.json_open_object();
            tiles.json_write_string("text", msg.text);
            tiles.json_write_int("turn", msg.turn);
            tiles.json_write_int("channel", msg.channel);
            if (msg.repeats > 1)
                tiles.json_write_int("repeats", msg.repeats);
            tiles.json_close_object();
        }
        tiles.json_close_array();
        unsent = send_ignore_one ? 1 : 0;
    }
#endif
};

// Circular buffer for keeping past messages.
message_store buffer;

#ifdef USE_TILE_WEB
bool _more = false, _last_more = false;

void webtiles_send_messages()
{
    webtiles_send_last_messages(0);
}
void webtiles_send_last_messages(int n)
{
    tiles.json_open_object();
    tiles.json_write_string("msg", "msgs");
    tiles.json_treat_as_empty();
    if (_more != _last_more)
    {
        tiles.json_write_bool("more", _more);
        _last_more = _more;
    }
    buffer.send();
    tiles.json_close_object(true);
    tiles.finish_message();
}
#endif

static FILE* _msg_dump_file = nullptr;

static bool suppress_messages = false;
static msg_colour_type prepare_message(const string& imsg,
                                       msg_channel_type channel,
                                       int param);

no_messages::no_messages() : msuppressed(suppress_messages)
{
    suppress_messages = true;
}

no_messages::~no_messages()
{
    suppress_messages = msuppressed;
}

msg_colour_type msg_colour(int col)
{
    return static_cast<msg_colour_type>(col);
}

static int colour_msg(msg_colour_type col)
{
    if (col == MSGCOL_MUTED)
        return DARKGREY;
    else
        return static_cast<int>(col);
}

// Returns a colour or MSGCOL_MUTED.
static msg_colour_type channel_to_msgcol(msg_channel_type channel, int param)
{
    msg_colour_type ret;

    switch (Options.channels[channel])
    {
    case MSGCOL_PLAIN:
        // Note that if the plain channel is muted, then we're protecting
        // the player from having that spread to other channels here.
        // The intent of plain is to give non-coloured messages, not to
        // suppress them.
        if (Options.channels[MSGCH_PLAIN] >= MSGCOL_DEFAULT)
            ret = MSGCOL_LIGHTGREY;
        else
            ret = Options.channels[MSGCH_PLAIN];
        break;

    case MSGCOL_DEFAULT:
    case MSGCOL_ALTERNATE:
        switch (channel)
        {
        case MSGCH_GOD:
        case MSGCH_PRAY:
            ret = (Options.channels[channel] == MSGCOL_DEFAULT)
                   ? msg_colour(god_colour(static_cast<god_type>(param)))
                   : msg_colour(god_message_altar_colour(static_cast<god_type>(param)));
            break;

        case MSGCH_DURATION:
            ret = MSGCOL_LIGHTBLUE;
            break;

        case MSGCH_DANGER:
            ret = MSGCOL_RED;
            break;

        case MSGCH_WARN:
        case MSGCH_ERROR:
            ret = MSGCOL_LIGHTRED;
            break;

        case MSGCH_FOOD:
            if (param) // positive change
                ret = MSGCOL_GREEN;
            else
                ret = MSGCOL_YELLOW;
            break;

        case MSGCH_INTRINSIC_GAIN:
            ret = MSGCOL_GREEN;
            break;

        case MSGCH_RECOVERY:
            ret = MSGCOL_LIGHTGREEN;
            break;

        case MSGCH_TALK:
        case MSGCH_TALK_VISUAL:
        case MSGCH_HELL_EFFECT:
            ret = MSGCOL_WHITE;
            break;

        case MSGCH_MUTATION:
        case MSGCH_MONSTER_WARNING:
            ret = MSGCOL_LIGHTRED;
            break;

        case MSGCH_MONSTER_SPELL:
        case MSGCH_MONSTER_ENCHANT:
        case MSGCH_FRIEND_SPELL:
        case MSGCH_FRIEND_ENCHANT:
            ret = MSGCOL_LIGHTMAGENTA;
            break;

        case MSGCH_TUTORIAL:
        case MSGCH_ORB:
        case MSGCH_BANISHMENT:
            ret = MSGCOL_MAGENTA;
            break;

        case MSGCH_MONSTER_DAMAGE:
            ret =  ((param == MDAM_DEAD)               ? MSGCOL_RED :
                    (param >= MDAM_SEVERELY_DAMAGED)   ? MSGCOL_LIGHTRED :
                    (param >= MDAM_MODERATELY_DAMAGED) ? MSGCOL_YELLOW
                                                       : MSGCOL_LIGHTGREY);
            break;

        case MSGCH_PROMPT:
            ret = MSGCOL_CYAN;
            break;

        case MSGCH_DIAGNOSTICS:
        case MSGCH_MULTITURN_ACTION:
            ret = MSGCOL_DARKGREY; // makes it easier to ignore at times -- bwr
            break;

        case MSGCH_PLAIN:
        case MSGCH_FRIEND_ACTION:
        case MSGCH_ROTTEN_MEAT:
        case MSGCH_EQUIPMENT:
        case MSGCH_EXAMINE:
        case MSGCH_EXAMINE_FILTER:
        case MSGCH_DGL_MESSAGE:
        default:
            ret = param > 0 ? msg_colour(param) : MSGCOL_LIGHTGREY;
            break;
        }
        break;

    case MSGCOL_MUTED:
        ret = MSGCOL_MUTED;
        break;

    default:
        // Setting to a specific colour is handled here, special
        // cases should be handled above.
        if (channel == MSGCH_MONSTER_DAMAGE)
        {
            // A special case right now for monster damage (at least until
            // the init system is improved)... selecting a specific
            // colour here will result in only the death messages coloured.
            if (param == MDAM_DEAD)
                ret = Options.channels[channel];
            else if (Options.channels[MSGCH_PLAIN] >= MSGCOL_DEFAULT)
                ret = MSGCOL_LIGHTGREY;
            else
                ret = Options.channels[MSGCH_PLAIN];
        }
        else
            ret = Options.channels[channel];
        break;
    }

    return ret;
}

int channel_to_colour(msg_channel_type channel, int param)
{
    return colour_msg(channel_to_msgcol(channel, param));
}

static void do_message_print(msg_channel_type channel, int param, bool cap,
                             bool nojoin, const char *format, va_list argp)
{
    va_list ap;
    va_copy(ap, argp);
    char buff[200];
    size_t len = vsnprintf(buff, sizeof(buff), format, argp);
    if (len < sizeof(buff))
        _mpr(buff, channel, param, nojoin, cap);
    else
    {
        char *heapbuf = (char*)malloc(len + 1);
        vsnprintf(heapbuf, len + 1, format, ap);
        _mpr(heapbuf, channel, param, nojoin, cap);
        free(heapbuf);
    }
    va_end(ap);
}

void mprf_nocap(msg_channel_type channel, int param, const char *format, ...)
{
    va_list argp;
    va_start(argp, format);
    do_message_print(channel, param, false, false, format, argp);
    va_end(argp);
}

void mprf_nocap(msg_channel_type channel, const char *format, ...)
{
    va_list argp;
    va_start(argp, format);
    do_message_print(channel, channel == MSGCH_GOD ? you.religion : 0,
                     false, false, format, argp);
    va_end(argp);
}

void mprf_nocap(const char *format, ...)
{
    va_list argp;
    va_start(argp, format);
    do_message_print(MSGCH_PLAIN, 0, false, false, format, argp);
    va_end(argp);
}

void mprf(msg_channel_type channel, int param, const char *format, ...)
{
    va_list argp;
    va_start(argp, format);
    do_message_print(channel, param, true, false, format, argp);
    va_end(argp);
}

void mprf(msg_channel_type channel, const char *format, ...)
{
    va_list argp;
    va_start(argp, format);
    do_message_print(channel, channel == MSGCH_GOD ? you.religion : 0,
                     true, false, format, argp);
    va_end(argp);
}

void mprf(const char *format, ...)
{
    va_list argp;
    va_start(argp, format);
    do_message_print(MSGCH_PLAIN, 0, true, false, format, argp);
    va_end(argp);
}

void mprf_nojoin(msg_channel_type channel, const char *format, ...)
{
    va_list argp;
    va_start(argp, format);
    do_message_print(channel, channel == MSGCH_GOD ? you.religion : 0,
                     true, true, format, argp);
    va_end(argp);
}

void mprf_nojoin(const char *format, ...)
{
    va_list argp;
    va_start(argp, format);
    do_message_print(MSGCH_PLAIN, 0, true, true, format, argp);
    va_end(argp);
}

#ifdef DEBUG_DIAGNOSTICS
void dprf(const char *format, ...)
{
    va_list argp;
    va_start(argp, format);
    do_message_print(MSGCH_DIAGNOSTICS, 0, false, false, format, argp);
    va_end(argp);
}

void dprf(diag_type param, const char *format, ...)
{
    if (Options.quiet_debug_messages[param])
        return;

    va_list argp;
    va_start(argp, format);
    do_message_print(MSGCH_DIAGNOSTICS, param, false, false, format, argp);
    va_end(argp);
}
#endif

static bool _updating_view = false;

static bool check_more(const string& line, msg_channel_type channel)
{
    return any_of(begin(Options.force_more_message),
                  end(Options.force_more_message),
                  bind(mem_fn(&message_filter::is_filtered),
                       placeholders::_1, channel, line));
}

static bool check_join(const string& line, msg_channel_type channel)
{
    switch (channel)
    {
    case MSGCH_EQUIPMENT:
        return false;
    default:
        break;
    }
    return true;
}

static void debug_channel_arena(msg_channel_type channel)
{
    switch (channel)
    {
    case MSGCH_PROMPT:
    case MSGCH_GOD:
    case MSGCH_PRAY:
    case MSGCH_DURATION:
    case MSGCH_FOOD:
    case MSGCH_RECOVERY:
    case MSGCH_INTRINSIC_GAIN:
    case MSGCH_MUTATION:
    case MSGCH_ROTTEN_MEAT:
    case MSGCH_EQUIPMENT:
    case MSGCH_FLOOR_ITEMS:
    case MSGCH_MULTITURN_ACTION:
    case MSGCH_EXAMINE:
    case MSGCH_EXAMINE_FILTER:
    case MSGCH_ORB:
    case MSGCH_TUTORIAL:
        die("Invalid channel '%s' in arena mode",
                 channel_to_str(channel).c_str());
        break;
    default:
        break;
    }
}

bool strip_channel_prefix(string &text, msg_channel_type &channel, bool silence)
{
    string::size_type pos = text.find(":");
    if (pos == string::npos)
        return false;

    string param = text.substr(0, pos);
    bool sound = false;

    if (param == "WARN")
        channel = MSGCH_WARN, sound = true;
    else if (param == "VISUAL WARN")
        channel = MSGCH_WARN;
    else if (param == "SOUND")
        channel = MSGCH_SOUND, sound = true;
    else if (param == "VISUAL")
        channel = MSGCH_TALK_VISUAL;
    else if (param == "SPELL")
        channel = MSGCH_MONSTER_SPELL, sound = true;
    else if (param == "VISUAL SPELL")
        channel = MSGCH_MONSTER_SPELL;
    else if (param == "ENCHANT")
        channel = MSGCH_MONSTER_ENCHANT, sound = true;
    else if (param == "VISUAL ENCHANT")
        channel = MSGCH_MONSTER_ENCHANT;
    else
    {
        param = replace_all(param, " ", "_");
        lowercase(param);
        int ch = str_to_channel(param);
        if (ch == -1)
            return false;
        channel = static_cast<msg_channel_type>(ch);
    }

    if (sound && silence)
        text = "";
    else
        text = text.substr(pos + 1);
    return true;
}

void msgwin_set_temporary(bool temp)
{
    flush_prev_message();
    _temporary = temp;
    if (!temp)
    {
        buffer.reset_temp();
        msgwin.reset_temp();
    }
}

void msgwin_clear_temporary()
{
    buffer.roll_back();
    msgwin.roll_back();
}

static int _last_msg_turn = -1; // Turn of last message.

static void _mpr(string text, msg_channel_type channel, int param, bool nojoin,
                 bool cap)
{
    if (_msg_dump_file != nullptr)
        fprintf(_msg_dump_file, "%s\n", text.c_str());

    if (crawl_state.game_crashed)
        return;

    if (crawl_state.game_is_arena())
        debug_channel_arena(channel);

#ifdef DEBUG_FATAL
    if (channel == MSGCH_ERROR)
        die_noline("%s", text.c_str());
#endif

    if (!crawl_state.io_inited)
    {
        if (channel == MSGCH_ERROR)
            fprintf(stderr, "%s\n", text.c_str());
        return;
    }

    // Flush out any "comes into view" monster announcements before the
    // monster has a chance to give any other messages.
    if (!_updating_view)
    {
        _updating_view = true;
        flush_comes_into_view();
        _updating_view = false;
    }

    if (channel == MSGCH_GOD && param == 0)
        param = you.religion;

    // Ugly hack.
    if (channel == MSGCH_DIAGNOSTICS || channel == MSGCH_ERROR)
        cap = false;

    msg_colour_type colour = prepare_message(text, channel, param);

    if (colour == MSGCOL_MUTED)
    {
        if (channel == MSGCH_PROMPT)
            msgwin.show();
        return;
    }

    bool domore = check_more(text, channel);
    bool join = !domore && !nojoin && check_join(text, channel);

    // Must do this before converting to formatted string and back;
    // that doesn't preserve close tags!
    string col = colour_to_str(colour_msg(colour));
    text = "<" + col + ">" + text + "</" + col + ">"; // XXX

    formatted_string fs = formatted_string::parse_string(text);
    if (you.duration[DUR_QUAD_DAMAGE])
        fs.all_caps(); // No sound, so we simulate the reverb with all caps.
    else if (cap)
        fs.capitalise();
    if (channel != MSGCH_ERROR && channel != MSGCH_DIAGNOSTICS)
        fs.filter_lang();
    text = fs.to_colour_string();

    message_item msg = message_item(text, channel, param, join);
    buffer.add(msg);
    _last_msg_turn = msg.turn;

    if (channel == MSGCH_ERROR)
        interrupt_activity(AI_FORCE_INTERRUPT);

    if (channel == MSGCH_PROMPT || channel == MSGCH_ERROR)
        set_more_autoclear(false);

    if (domore)
        more(true);
}

static string show_prompt(string prompt)
{
    mprf(MSGCH_PROMPT, "%s", prompt.c_str());

    // FIXME: duplicating mpr code.
    msg_colour_type colour = prepare_message(prompt, MSGCH_PROMPT, 0);
    return colour_string(prompt, colour_msg(colour));
}

static string _prompt;
void msgwin_prompt(string prompt)
{
    msgwin_set_temporary(true);
    _prompt = show_prompt(prompt);
}

void msgwin_reply(string reply)
{
    msgwin_clear_temporary();
    msgwin_set_temporary(false);
    reply = replace_all(reply, "<", "<<");
    mprf(MSGCH_PROMPT, "%s<lightgrey>%s</lightgrey>", _prompt.c_str(), reply.c_str());
    msgwin.got_input();
}

void msgwin_got_input()
{
    msgwin.got_input();
}

int msgwin_get_line(string prompt, char *buf, int len,
                    input_history *mh, const string &fill)
{
    if (prompt != "")
        msgwin_prompt(prompt);

    int ret = cancellable_get_line(buf, len, mh, nullptr, fill);
    msgwin_reply(buf);
    return ret;
}

void msgwin_new_turn()
{
    buffer.new_turn();
}

void msgwin_new_cmd()
{
    flush_prev_message();
    bool new_turn = (you.num_turns > _last_msg_turn);
    msgwin.new_cmdturn(new_turn);
}

unsigned int msgwin_line_length()
{
    return msgwin.out_width();
}

unsigned int msgwin_lines()
{
    return msgwin.out_height();
}

// mpr() an arbitrarily long list of strings without truncation or risk
// of overflow.
void mpr_comma_separated_list(const string &prefix,
                              const vector<string> &list,
                              const string &andc,
                              const string &comma,
                              const msg_channel_type channel,
                              const int param,
                              const string &outs)
{
    string out = prefix;

    for (int i = 0, size = list.size(); i < size; i++)
    {
        out += list[i];

        if (size > 0 && i < (size - 2))
            out += comma;
        else if (i == (size - 2))
            out += andc;
        else if (i == (size - 1))
            out += outs;
    }
    _mpr(out, channel, param);
}

// Checks whether a given message contains patterns relevant for
// notes, stop_running or sounds and handles these cases.
static void mpr_check_patterns(const string& message,
                               msg_channel_type channel,
                               int param)
{
    for (const text_pattern &pat : Options.note_messages)
    {
        if (channel == MSGCH_EQUIPMENT || channel == MSGCH_FLOOR_ITEMS
            || channel == MSGCH_MULTITURN_ACTION
            || channel == MSGCH_EXAMINE || channel == MSGCH_EXAMINE_FILTER
            || channel == MSGCH_TUTORIAL || channel == MSGCH_DGL_MESSAGE)
        {
            continue;
        }

        if (pat.matches(message))
        {
            take_note(Note(NOTE_MESSAGE, channel, param, message.c_str()));
            break;
        }
    }

    if (channel != MSGCH_DIAGNOSTICS && channel != MSGCH_EQUIPMENT)
        interrupt_activity(AI_MESSAGE, channel_to_str(channel) + ":" + message);

#ifdef USE_SOUND
    for (const sound_mapping &sound : Options.sound_mappings)
    {
        // Maybe we should allow message channel matching as for
        // force_more_message?
        if (sound.pattern.matches(message))
        {
            play_sound(sound.soundfile.c_str());
            break;
        }
    }
#endif
}

static bool channel_message_history(msg_channel_type channel)
{
    switch (channel)
    {
    case MSGCH_PROMPT:
    case MSGCH_EQUIPMENT:
    case MSGCH_EXAMINE_FILTER:
        return false;
    default:
        return true;
    }
}

// Returns the default colour of the message, or MSGCOL_MUTED if
// the message should be suppressed.
static msg_colour_type prepare_message(const string& imsg,
                                       msg_channel_type channel,
                                       int param)
{
    if (suppress_messages)
        return MSGCOL_MUTED;

    if (silenced(you.pos())
        && (channel == MSGCH_SOUND || channel == MSGCH_TALK))
    {
        return MSGCOL_MUTED;
    }

    msg_colour_type colour = channel_to_msgcol(channel, param);

    if (colour != MSGCOL_MUTED)
        mpr_check_patterns(imsg, channel, param);

    for (const message_colour_mapping &mcm : Options.message_colour_mappings)
    {
        if (mcm.message.is_filtered(channel, imsg))
        {
            colour = mcm.colour;
            break;
        }
    }

    return colour;
}

void flush_prev_message()
{
    buffer.flush_prev();
}

void clear_messages(bool force)
{
    if (!crawl_state.io_inited)
        return;
    // Unflushed message will be lost with clear_messages,
    // so they shouldn't really exist, but some of the delay
    // code appears to do this intentionally.
    // ASSERT(!buffer.have_prev());
    flush_prev_message();

    msgwin.got_input(); // Consider old messages as read.

    if (Options.clear_messages || force)
        msgwin.clear();

    // TODO: we could indicate indicate clear_messages with a different
    //       leading character than '-'.
}

static bool autoclear_more = false;

void set_more_autoclear(bool on)
{
    autoclear_more = on;
}

static void readkey_more(bool user_forced)
{
    if (autoclear_more)
        return;
    int keypress = 0;
#ifdef USE_TILE_WEB
    unwind_bool unwind_more(_more, true);
#endif
    mouse_control mc(MOUSE_MODE_MORE);

    do
    {
        keypress = getch_ck();
        if (keypress == CK_REDRAW)
        {
            redraw_screen();
            continue;
        }
    }
    while (keypress != ' ' && keypress != '\r' && keypress != '\n'
           && !key_is_escape(keypress)
#ifdef TOUCH_UI
           && keypress != CK_MOUSE_CLICK);
#else
           && (user_forced || keypress != CK_MOUSE_CLICK));
#endif

    if (key_is_escape(keypress))
        set_more_autoclear(true);
}

/**
 * more() preprocessing.
 *
 * @return Whether the more prompt should be skipped.
 */
static bool _pre_more()
{
    if (crawl_state.game_crashed || crawl_state.seen_hups)
        return true;

#ifdef DEBUG_DIAGNOSTICS
    if (you.running)
        return true;
#endif

    if (crawl_state.game_is_arena())
    {
        delay(Options.view_delay);
        return true;
    }

    if (crawl_state.is_replaying_keys())
        return true;

#ifdef WIZARD
    if (luaterp_running())
        return true;
#endif

    if (!crawl_state.show_more_prompt || suppress_messages)
        return true;

    return false;
}

void more(bool user_forced)
{
    if (!crawl_state.io_inited)
        return;
    flush_prev_message();
    msgwin.more(false, user_forced);
    clear_messages();
}

void canned_msg(canned_message_type which_message)
{
    switch (which_message)
    {
        case MSG_SOMETHING_APPEARS:
            mprf(jtransc("Something appears %s!"),
                 player_has_feet() ? jtransc("at your feet")
                                   : jtransc("before you"));
            break;
        case MSG_NOTHING_HAPPENS:
            mpr(jtrans("Nothing appears to happen."));
            break;
        case MSG_YOU_UNAFFECTED:
            mpr(jtrans("You are unaffected."));
            break;
        case MSG_YOU_RESIST:
            mpr(jtrans("You resist."));
            learned_something_new(HINT_YOU_RESIST);
            break;
        case MSG_YOU_PARTIALLY_RESIST:
            mpr(jtrans("You partially resist."));
            break;
        case MSG_TOO_BERSERK:
            mpr(jtrans("You are too berserk!"));
            crawl_state.cancel_cmd_repeat();
            break;
        case MSG_TOO_CONFUSED:
            mpr(jtrans("You're too confused!"));
            break;
        case MSG_PRESENT_FORM:
            mpr(jtrans("You can't do that in your present form."));
            crawl_state.cancel_cmd_repeat();
            break;
        case MSG_NOTHING_CARRIED:
            mpr(jtrans("You aren't carrying anything."));
            crawl_state.cancel_cmd_repeat();
            break;
        case MSG_CANNOT_DO_YET:
            mpr(jtrans("You can't do that yet."));
            crawl_state.cancel_cmd_repeat();
            break;
        case MSG_OK:
            mpr_nojoin(MSGCH_PROMPT, jtrans("Okay, then."));
            crawl_state.cancel_cmd_repeat();
            break;
        case MSG_UNTHINKING_ACT:
            mpr(jtrans("Why would you want to do that?"));
            crawl_state.cancel_cmd_repeat();
            break;
        case MSG_NOTHING_THERE:
            mpr(jtrans("There's nothing there!"));
            crawl_state.cancel_cmd_repeat();
            break;
        case MSG_NOTHING_CLOSE_ENOUGH:
            mpr(jtrans("There's nothing close enough!"));
            crawl_state.cancel_cmd_repeat();
            break;
        case MSG_NO_ENERGY:
            mpr(jtrans("You don't have the energy to cast that spell."));
            crawl_state.cancel_cmd_repeat();
            break;
        case MSG_SPELL_FIZZLES:
            mpr(jtrans("The spell fizzles."));
            break;
        case MSG_HUH:
            mpr_nojoin(MSGCH_EXAMINE_FILTER, jtrans("Huh?"));
            crawl_state.cancel_cmd_repeat();
            break;
        case MSG_EMPTY_HANDED_ALREADY:
        case MSG_EMPTY_HANDED_NOW:
        {
            const char* when =
            (which_message == MSG_EMPTY_HANDED_ALREADY ? "既に" : "もう");
            if (you.species == SP_FELID)
                mprf(jtransc("Your mouth is %s empty."), when);
            else if (you.has_usable_claws(true))
                mprf(jtransc("You are %s empty-clawed."), when);
            else if (you.has_usable_tentacles(true))
                mprf(jtransc("You are %s empty-tentacled."), when);
            else
                mprf(jtransc("You are %s empty-handed."), when);
            break;
        }
        case MSG_YOU_BLINK:
            mpr(jtrans("You blink."));
            break;
        case MSG_STRANGE_STASIS:
            mpr(jtrans("You feel a strange sense of stasis."));
            break;
        case MSG_NO_SPELLS:
            mpr(jtrans("You don't know any spells."));
            break;
        case MSG_MANA_INCREASE:
            mpr(jtrans("You feel your magic capacity increase."));
            break;
        case MSG_MANA_DECREASE:
            mpr(jtrans("You feel your magic capacity decrease."));
            break;
        case MSG_DISORIENTED:
            mpr(jtrans("You feel momentarily disoriented."));
            break;
        case MSG_TOO_HUNGRY:
            mpr(jtrans("You're too hungry."));
            break;
        case MSG_DETECT_NOTHING:
            mpr(jtrans("You detect nothing."));
            break;
        case MSG_CALL_DEAD:
            mpr(jtrans("You call on the dead to rise..."));
            break;
        case MSG_ANIMATE_REMAINS:
            mpr(jtrans("You attempt to give life to the dead..."));
            break;
        case MSG_DECK_EXHAUSTED:
            mpr(jtrans("The deck of cards disappears in a puff of smoke."));
            break;
        case MSG_CANNOT_MOVE:
            mpr(jtrans("You cannot move."));
            break;
        case MSG_YOU_DIE:
            mpr_nojoin(MSGCH_PLAIN, jtrans("You die..."));
            break;
        case MSG_GHOSTLY_OUTLINE:
            mpr(jtrans("You see a ghostly outline there, and the spell fizzles."));
            break;
    }
}

// Note that this function *completely* blocks messaging for monsters
// distant or invisible to the player ... look elsewhere for a function
// permitting output of "It" messages for the invisible {dlb}
// Intentionally avoids info and str_pass now. - bwr
bool simple_monster_message(const monster* mons, const char *event,
                            msg_channel_type channel,
                            int param,
                            description_level_type descrip)
{
    if (mons_near(mons)
        && (channel == MSGCH_MONSTER_SPELL || channel == MSGCH_FRIEND_SPELL
            || mons->visible_to(&you)))
    {
        string msg = jtrans(mons->name(descrip));
        msg += event;
        msg = apostrophise_fixup(msg);

        if (channel == MSGCH_PLAIN && mons->wont_attack())
            channel = MSGCH_FRIEND_ACTION;

        mprf(channel, param, "%s", msg.c_str());
        return true;
    }

    return false;
}

// yet another wrapper for mpr() {dlb}:
void simple_god_message(const char *event, god_type which_deity)
{
    string msg = jtrans(god_name(which_deity)) + event;
    msg = apostrophise_fixup(msg);
    god_speaks(which_deity, msg.c_str());
}

static bool is_channel_dumpworthy(msg_channel_type channel)
{
    return channel != MSGCH_EQUIPMENT
           && channel != MSGCH_DIAGNOSTICS
           && channel != MSGCH_TUTORIAL;
}

void clear_message_store()
{
    buffer.clear();
}

string get_last_messages(int mcount, bool full)
{
    flush_prev_message();

    string text;
    // XXX: should use some message_history iterator here
    const store_t& msgs = buffer.get_store();
    // XXX: loop wraps around otherwise. This could be done better.
    mcount = min(mcount, NUM_STORED_MESSAGES);
    for (int i = -1; mcount > 0; --i)
    {
        const message_item msg = msgs[i];
        if (!msg)
            break;
        if (full || is_channel_dumpworthy(msg.channel))
            text = msg.pure_text_with_repeats() + "\n" + text;
        mcount--;
    }

    // An extra line of clearance.
    if (!text.empty())
        text += "\n";
    return text;
}

void get_recent_messages(vector<string> &mess,
                         vector<msg_channel_type> &chan)
{
    flush_prev_message();

    const store_t& msgs = buffer.get_store();
    int mcount = NUM_STORED_MESSAGES;
    for (int i = -1; mcount > 0; --i, --mcount)
    {
        const message_item msg = msgs[i];
        if (!msg)
            break;
        mess.push_back(msg.pure_text());
        chan.push_back(msg.channel);
    }
}

// We just write out the whole message store including empty/unused
// messages. They'll be ignored when restoring.
void save_messages(writer& outf)
{
    store_t msgs = buffer.get_store();
    marshallInt(outf, msgs.size());
    for (int i = 0; i < msgs.size(); ++i)
    {
        marshallString4(outf, msgs[i].text);
        marshallInt(outf, msgs[i].channel);
        marshallInt(outf, msgs[i].param);
        marshallInt(outf, msgs[i].repeats);
        marshallInt(outf, msgs[i].turn);
    }
}

void load_messages(reader& inf)
{
    unwind_var<bool> save_more(crawl_state.show_more_prompt, false);

    int num = unmarshallInt(inf);
    for (int i = 0; i < num; ++i)
    {
        string text;
        unmarshallString4(inf, text);

        msg_channel_type channel = (msg_channel_type) unmarshallInt(inf);
        int           param      = unmarshallInt(inf);
        int           repeats    = unmarshallInt(inf);
        int           turn       = unmarshallInt(inf);

        message_item msg(message_item(text, channel, param, repeats, turn));
        if (msg)
            buffer.store_msg(msg);
    }
    // With Options.message_clear, we don't want the message window
    // pre-filled.
    clear_messages();
}

void replay_messages()
{
    formatted_scroller hist(MF_START_AT_END | MF_ALWAYS_SHOW_MORE, "");
    hist.set_more();

    const store_t msgs = buffer.get_store();
    for (int i = 0; i < msgs.size(); ++i)
        if (channel_message_history(msgs[i].channel))
        {
            string text = msgs[i].with_repeats();
            linebreak_string(text, cgetsize(GOTO_CRT).x - 1);
            vector<formatted_string> parts;
            formatted_string::parse_string_to_multiple(text, parts);
            for (unsigned int j = 0; j < parts.size(); ++j)
            {
                formatted_string line;
                prefix_type p = P_NONE;
                if (j == parts.size() - 1 && i + 1 < msgs.size()
                    && msgs[i+1].turn > msgs[i].turn)
                {
                    p = P_TURN_END;
                }
                line.add_glyph(_prefix_glyph(p));
                line += parts[j];
                hist.add_item_formatted_string(line);
            }
        }

    hist.show();
}

void set_msg_dump_file(FILE* file)
{
    _msg_dump_file = file;
}

void formatted_mpr(const formatted_string& fs,
                   msg_channel_type channel, int param)
{
    _mpr(fs.to_colour_string(), channel, param);
}
