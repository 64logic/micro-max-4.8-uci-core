/* micro-Max 4.8 UCI (Core) v1.0                                              */
/* UCI integration by 64Logic                                                 */
/* Canonical micro-Max 4.8 source: umax4_8.c by H.G. Muller                   */
/*                                                                            */
/* Copyright (c) 2026 64Logic                                                 */
/* SPDX-License-Identifier: MIT                                               */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>

/* This guard applies only to this translation unit. Canonical umax4_8.c      */
/* must also be compiled with -fsigned-char so plain char values are signed.  */
#if CHAR_MIN == 0
#error "use -fsigned-char when compiling all source files, including umax4_8.c"
#endif

/* Canonical micro-Max 4.8 interface                                          */
extern int  M, S, I, Q, O, K, N, R, J, Z, k;
extern char L;
extern char b[129];
extern char T[1035];
extern char o[];
extern int  D(int q, int l, int e, int E, int z, int n);

/* Game and branch replay                                                     */
/*                                                                            */
/* A[] starts with all entries zero and later contains ordinary search        */
/* entries and protected D=99 game history entries. game_init() clears A[]    */
/* only when restoring fresh canonical state for a new game or replaying      */
/* move history supplied by the 'position' command. During normal play, the   */
/* integration does not inspect, rewrite, filter, or repair search entries.   */
#define U (1 << 24)
extern struct _ { int K, V; char X, Y, D; } A[U];

/* Tracks whether canonical game state has been initialized.                  */
static int game_started = 0;

/* True only when the tracked UCI 'position' history is trusted and canonical */
/* state is either at that position or exactly one known search move beyond   */
/* it. A received UCI 'position' command defines the requested position. The  */
/* integration may replay the complete move history once if D() rejects a     */
/* supported newly appended move. Unsupported or unrepresentable input is     */
/* never translated into a different chess position. If synchronization       */
/* cannot be established, the integration refuses to search until a later     */
/* 'position' command or game reset restores it.                              */
static int position_synced = 1;

/* Diagnostic output controlled by the UCI 'debug' command is off by default. */
static int debug_enabled = 0;

/* Suppress repeated capability warnings when debug mode is off.              */
static int warned_search_limits = 0;
static int warned_ponder = 0;
static int warned_infinite = 0;

/* A micro-Max search can finish before UCI permits 'bestmove' for            */
/* 'ponder' or 'infinite'. Keep the completed result and the conditions that  */
/* delay its release.                                                         */
#define HOLD_PONDER   1
#define HOLD_INFINITE 2
static int held_search_mode = 0;
static char held_bestmove[8];

static void clear_held_search(void)
{
    held_search_mode = 0;
    held_bestmove[0] = 0;
}

static void hold_bestmove(const char *move, int mode)
{
    strcpy(held_bestmove, move);
    held_search_mode = mode;
}

static void release_held_search(void)
{
    if (!held_search_mode)
        return;
    printf("bestmove %s\n", held_bestmove);
    clear_held_search();
}

/* D() commits the move it selects before returning. UCI 'bestmove'           */
/* does not itself advance the last requested position, so canonical state    */
/* can be one known move ahead until a later 'position' confirms that move    */
/* or the integration restores the last requested position by replaying its   */
/* move history.                                                              */
static int search_move_pending = 0;
static char pending_search_move[8];

static void clear_pending_search_move(void)
{
    search_move_pending = 0;
    pending_search_move[0] = 0;
}

static void set_pending_search_move(const char *move)
{
    strcpy(pending_search_move, move);
    search_move_pending = 1;
}

/* Incremental move tracking                                                  */
/*                                                                            */
/* Each 'position startpos moves ...' command resends the full move list.     */
/* Replaying the entire list on every update would call D() redundantly for   */
/* historical plies, discard live position and search state, and make         */
/* cumulative move application work grow O(n^2) with game length.             */
/* Advance canonical state only for newly appended moves and never apply a    */
/* previously applied move twice.                                             */
/*                                                                            */
/* Track the move history already applied. While synchronized, if the new     */
/* history contains that exact prefix followed by additional moves, apply     */
/* only the newly appended moves. Otherwise replay the complete move history  */
/* from 'startpos' for a takeback, branch, replacement history, or recovery   */
/* from unsynchronized state. The bookkeeping never changes D() or            */
/* umax4_8.c.                                                                 */
/* Move history storage is simple and static.                                 */
/* Move history longer than MAX_TRACKED_MOVES is rejected explicitly rather   */
/* than being silently truncated.                                             */
#define MAX_TRACKED_MOVES 18000
#define MAX_UCI_LINE      131072
static char applied_moves[MAX_TRACKED_MOVES][8];
static int  applied_count = 0;

/* engine_init() performs process setup once for the micro-Max hash           */
/* translation table T[]. It must run exactly once per process. Replacing     */
/* values in T[] later would invalidate hash entries that still use the       */
/* previous values.                                                           */
static void engine_init(void)
{
    /* Match the original initialization loop by filling T[136..1034] with    */
    /* random values used for the hash keys. N ends at 135 because the loop   */
    /* tests N-- > M. The original board printing loop leaves N at 128 before */
    /* each D() call.                                                         */
    N = 1035;
    while (N-- > M) T[N] = rand() >> 9;

    /* The original center table loop uses global char L as its countdown     */
    /* variable, leaving L == (char)-1 after initialization. Set that         */
    /* side effect explicitly.                                                */
    L = (char)-1;
}

/* game_init() restores fresh canonical game state. A new micro-Max process   */
/* gets Q=O=R=J=Z=0 from static storage initialized to zero and k=16 from     */
/* its explicit initializer. The setup loop leaves the middle ranks empty.    */
/*                                                                            */
/* The engine process can remain running across games, so each 'ucinewgame'   */
/* must restore fresh canonical game state. Otherwise pieces, hash keys,      */
/* and search state could carry over between games.                           */
static void game_init(void)
{
    int i, kk, ll;

    /* Clear the entire logical board, including the real squares and the     */
    /* unused half that stores the center points table, before replaying      */
    /* setup. Clearing b[] prevents a piece from a previous game from         */
    /* surviving on a middle rank that the original setup loop does not       */
    /* write.                                                                 */
    /* micro-Max uses 0x88 board indexing, where the 0x88 mask identifies     */
    /* squares outside the board. b[] has 129 bytes and uses index 128 as the */
    /* micro-Max dummy square. Clear the complete array before setup so b[]   */
    /* begins from the same zeroed state as a fresh process.                  */
    for (i = 0; i < 129; i++) b[i] = 0;

    kk = 8;
    while (kk--) {
        b[kk] = (b[kk+112] = o[kk+24] + 8) + 8;
        b[kk+16] = 18;
        b[kk+96] = 9;
    }
    ll = 8;
    while (ll--) {
        kk = 8;
        while (kk--)
            b[16*ll + kk + 8] = (kk-4)*(kk-4) + (ll-3.5)*(ll-3.5);
    }

    /* A fresh process starts with A[] set to zero. On every later game       */
    /* reset or replay, clear A[] so ordinary search entries and protected    */
    /* D=99 game history from an abandoned line cannot become history of the  */
    /* rebuilt line. During ordinary play, the integration never clears or    */
    /* edits A[].                                                             */
    if (game_started)
        memset(A, 0, sizeof A);

    Q = 0;
    O = 0;
    R = 0;
    J = 0;
    Z = 0;
    k = 16;

    /* Before the first D() call, micro-Max 4.8 sets K = I, while its         */
    /* initialization leaves global char L at (char)-1. Set both so           */
    /* 'go' immediately after a reset begins from canonical state.            */
    K = I;
    L = (char)-1;

    /* micro-Max 4.8 leaves N at 128 before each D() call. The                */
    /* integration sets N = 128 in both apply_move() and the 'go' handler to  */
    /* preserve that behavior.                                                */
    N = 128;

    game_started = 1;
    position_synced = 1;
}

static int real_board_changed(const char before[129]);

/* apply_move() replays one move through the canonical move validation and    */
/* application path used by micro-Max 4.8.                                    */
/*                                                                            */
/* micro-Max 4.8 leaves N at 128 before each D() call, so each                */
/* replayed ply explicitly sets N = 128 here.                                 */
/*                                                                            */
/* K and L alone cannot prove that a supplied move was committed. D() can     */
/* reject a move while leaving those globals equal to the requested squares.  */
/* Use real_board_changed() to determine whether D() committed the move.      */
static int apply_move(int from, int to)
{
    char board_before[129];

    /* Snapshot b[] before D(). Commit detection later compares only the 64   */
    /* real board squares; see real_board_changed() for why b[128] is         */
    /* excluded.                                                              */
    memcpy(board_before, b, sizeof board_before);

    K = from;
    L = to;
    N = 128;
    D(-I, I, Q, O, 1, 3);

    return real_board_changed(board_before);
}

/* Translate a UCI square pair such as 'e2e4' into micro-Max K/L square       */
/* encoding using the original micro-Max 4.8 conversion formula unchanged.    */
/* ASCII values for files 'a' to 'h' and ranks '1' to '8' match UCI input.    */
static void parse_move(const char *s, int *from, int *to)
{
    *from = s[0] - 16*s[1] + 799;
    *to   = s[2] - 16*s[3] + 799;
}

/* b[] contains more than the 64 chessboard squares. In particular, b[128] is */
/* the micro-Max dummy square, and D() can change it even when no             */
/* game move is committed. Compare only valid 0x88 board squares.             */
/*                                                                            */
/* Every committed chess move changes at least one real square, while D()     */
/* restores the real board after rejected or exploratory candidates.          */
static int real_board_changed(const char before[129])
{
    int i;
    for (i = 0; i < 128; i++)
        if (!(i & M) && before[i] != b[i])
            return 1;
    return 0;
}

/* Match the complete UCI command. Do not accept prefixes such as 'good' for  */
/* 'go' or 'positionXYZ' for 'position'.                                      */
static int command_is(const char *cmd, const char *command)
{
    size_t n = strlen(command);
    return !strncmp(cmd, command, n) &&
           (cmd[n] == 0 || isspace((unsigned char)cmd[n]));
}

/* UCI requires unknown commands or leading tokens to be ignored while        */
/* parsing continues on the same line. Find the first recognized UCI command  */
/* without changing the input buffer.                                         */
static int is_recognized_uci_command(const char *s, size_t len)
{
    static const char *const commands[] = {
        "uci", "debug", "isready", "setoption", "register",
        "ucinewgame", "position", "go", "stop", "ponderhit", "quit"
    };
    size_t i;

    for (i = 0; i < sizeof commands / sizeof commands[0]; i++)
        if (strlen(commands[i]) == len && !strncmp(s, commands[i], len))
            return 1;
    return 0;
}

static char *find_uci_command(char *line)
{
    char *p = line;

    while (*p) {
        char *start;
        size_t len;

        while (*p && isspace((unsigned char)*p))
            p++;
        if (!*p)
            break;
        start = p;
        while (*p && !isspace((unsigned char)*p))
            p++;
        len = (size_t)(p - start);
        if (is_recognized_uci_command(start, len))
            return start;
    }
    return NULL;
}

/* Classify the standard parameters in a UCI 'go' command. The integration    */
/* does not use parameter values, so scan each token independently and ignore */
/* values and unknown tokens. A missing value cannot hide a later UCI         */
/* keyword.                                                                   */
/* 'searchmoves' is flagged immediately because its restricted root move set  */
/* cannot be imposed on micro-Max search, so the 'go' request is rejected.    */
struct go_request {
    int searchmoves;
    int ponder;
    int infinite;
    int clock_fields;
    int search_limits;
};

static void parse_go_request(const char *cmd, struct go_request *request)
{
    static char copy[MAX_UCI_LINE];
    char *tok;

    memset(request, 0, sizeof *request);
    /* read_uci_line() guarantees dispatched commands fit MAX_UCI_LINE.       */
    memcpy(copy, cmd, strlen(cmd) + 1);

    /* Skip the already validated 'go' command.                               */
    strtok(copy, " \t\r\n\v\f");

    while ((tok = strtok(NULL, " \t\r\n\v\f")) != NULL) {
        if (!strcmp(tok, "searchmoves")) {
            request->searchmoves = 1;
        } else if (!strcmp(tok, "ponder")) {
            request->ponder = 1;
        } else if (!strcmp(tok, "infinite")) {
            request->infinite = 1;
        } else if (!strcmp(tok, "wtime") || !strcmp(tok, "btime") ||
                   !strcmp(tok, "winc") || !strcmp(tok, "binc") ||
                   !strcmp(tok, "movestogo")) {
            request->clock_fields = 1;
        } else if (!strcmp(tok, "depth") || !strcmp(tok, "nodes") ||
                   !strcmp(tok, "mate") || !strcmp(tok, "movetime")) {
            request->search_limits = 1;
        }
    }
}

/* A token is one part of a UCI command separated by whitespace. Validate a   */
/* move token before converting its squares to 0x88 indexes. Validation       */
/* prevents invalid move tokens from being indexed and prevents tokens such   */
/* as 'e2e4x' or 'e2e4q' from being treated as ordinary 'e2e4'.               */
/*                                                                            */
/* The integration also accepts uppercase promotion letters, although         */
/* conforming UCI input normally uses lowercase.                              */
/* micro-Max 4.8 cannot underpromote, so knight, rook, and bishop             */
/* promotion tokens are rejected before calling D().                          */
static int valid_uci_move_token(const char *s, int *from, int *to)
{
    size_t len;
    int promotion_geometry;
    int piece;

    if (!s) return 0;
    len = strlen(s);
    if (len != 4 && len != 5) return 0;

    if (s[0] < 'a' || s[0] > 'h' ||
        s[1] < '1' || s[1] > '8' ||
        s[2] < 'a' || s[2] > 'h' ||
        s[3] < '1' || s[3] > '8')
        return 0;

    if (len == 5 &&
        s[4] != 'q' && s[4] != 'Q' &&
        s[4] != 'r' && s[4] != 'R' &&
        s[4] != 'b' && s[4] != 'B' &&
        s[4] != 'n' && s[4] != 'N')
        return 0;

    parse_move(s, from, to);

    /* A promotion suffix is accepted here only when the source square        */
    /* contains a pawn and the destination is the back rank. Such a token     */
    /* requires a UCI promotion suffix.                                       */
    piece = b[*from] & 7;
    promotion_geometry = (b[*from] != 0) &&
                         (piece < 3) &&
                         (((*to & 0x70) == 0) || ((*to & 0x70) == 0x70));

    if (len == 5 && !promotion_geometry) return 0;
    if (len == 4 &&  promotion_geometry) return 0;

    return 1;
}

/* Detect a UCI move token that requests promotion to anything other than a   */
/* queen, such as 'e7e8n'. D() promotes only to a queen, so micro-Max 4.8     */
/* cannot underpromote. Reject the request before calling D() rather than     */
/* translating it into a different chess position.                            */
static int is_underpromotion(const char *tok)
{
    size_t len = strlen(tok);
    return (len == 5 && tok[4] != 'q' && tok[4] != 'Q');
}

static void format_move(char out[8], int from, int to, int promote)
{
    /* UCI requires a promotion suffix when a reported move is a promotion.   */
    out[0] = 'a' + (from & 7);
    out[1] = '8' - ((from >> 4) & 7);
    out[2] = 'a' + (to & 7);
    out[3] = '8' - ((to >> 4) & 7);
    if (promote) {
        out[4] = 'q';
        out[5] = 0;
    } else {
        out[4] = 0;
    }
}

/* Record why apply_move_range() stopped. When applying newly appended moves, */
/* a supported move rejected by D() may trigger one replay of the complete    */
/* move history supplied by the 'position' command. Invalid move tokens and   */
/* underpromotion do not trigger a replay.                                    */
#define APPLY_RANGE_OK             0
#define APPLY_RANGE_INVALID        1
#define APPLY_RANGE_UNDERPROMOTION 2
#define APPLY_RANGE_REJECTED       3

/* Apply moves from index start through count - 1, one at a time, and stop at */
/* the first invalid, unsupported, or rejected entry. Return the index one    */
/* past the last successfully applied move.                                   */
/* When failure_kind is not NULL, record why processing stopped.              */
static int apply_move_range(char moves[][8], int start, int count,
                            int *failure_kind)
{
    int i;
    if (failure_kind)
        *failure_kind = APPLY_RANGE_OK;

    for (i = start; i < count; i++) {
        int from, to;

        /* Invalid square coordinates must not reach 0x88 square conversion.  */
        /* Invalid tokens longer than seven characters are already truncated  */
        /* in storage, so do not echo them as if they were the complete       */
        /* original input.                                                    */
        if (!valid_uci_move_token(moves[i], &from, &to)) {
            if (failure_kind)
                *failure_kind = APPLY_RANGE_INVALID;
            printf("info string invalid move token at ply %d: "
                   "remaining moves ignored\n", i + 1);
            return i;
        }

        /* micro-Max 4.8 has no input that represents knight, rook, or bishop */
        /* promotion. Passing only from/to would ask D() to commit a queen.   */
        /* That would produce a different chess position from the 'position'  */
        /* request, so do not call D().                                       */
        if (is_underpromotion(moves[i])) {
            if (failure_kind)
                *failure_kind = APPLY_RANGE_UNDERPROMOTION;
            printf("info string underpromotion '%s' unsupported at ply %d\n",
                   moves[i], i + 1);
            return i;
        }

        if (!apply_move(from, to)) {
            if (failure_kind)
                *failure_kind = APPLY_RANGE_REJECTED;
            return i;
        }
    }
    return count;
}

/* Restore canonical state to the last requested UCI 'position' when a        */
/* previous search committed its selected move but no new 'position' command  */
/* has been received before another search begins.                            */
static int restore_requested_position(void)
{
    int target_count = applied_count;
    int applied_ok;

    clear_pending_search_move();
    game_init();
    applied_ok = apply_move_range(applied_moves, 0, target_count, NULL);
    applied_count = applied_ok;
    position_synced = (applied_ok == target_count);

    if (!position_synced) {
        printf("info string replay failed at ply %d: "
               "position unsynchronized\n", applied_ok + 1);
        return 0;
    }
    return 1;
}

/* UCI input lines                                                            */
/*                                                                            */
/* Read one complete UCI input line at a time from standard input. An input   */
/* line may end with carriage return (CR, '\r'), line feed (LF, '\n'), or a   */
/* combination of the two. fgets() recognizes LF as a line terminator but     */
/* does not treat a line ending only in CR as complete, so read input one     */
/* character at a time to support either terminator.                          */
/*                                                                            */
/* Return as soon as either CR or LF is received. Do not wait for a possible  */
/* second line ending character because doing so could block the engine       */
/* before it sends its response. In a CRLF or LFCR pair, the second character */
/* is read as an empty line on the next pass and safely ignored.              */
/* If an input line is too long for the buffer or contains an embedded NUL    */
/* byte, consume the rest so its suffix cannot become a second command.       */
static int read_uci_line(char *buf, size_t cap, int *overflow, int *malformed)
{
    size_t len = 0;
    int c;
    int saw_any = 0;

    *overflow = 0;
    *malformed = 0;

    while ((c = getchar()) != EOF) {
        saw_any = 1;

        if (c == '\r' || c == '\n') {
            buf[len] = 0;
            return 1;
        }

        if (c == 0) {
            *malformed = 1;
            continue;
        }

        if (len + 1 < cap)
            buf[len++] = (char)c;
        else
            *overflow = 1;
    }

    if (saw_any) {
        buf[len] = 0;
        return 1;
    }

    return 0;
}

/* UCI command loop                                                           */
int main(void)
{
    static char line[MAX_UCI_LINE];
    char *cmd;
    int line_overflow;
    int line_malformed;

    engine_init();

    while (read_uci_line(line, sizeof line, &line_overflow,
                         &line_malformed)) {
        if (line_overflow || line_malformed) {
            /* The discarded line might have been a new 'position' command.   */
            /* Do not trust the current canonical state until a later valid   */
            /* 'position' command or game reset establishes a trusted state.  */
            position_synced = 0;
            clear_held_search();
            clear_pending_search_move();
            if (line_malformed)
                printf("info string malformed UCI input: embedded NUL byte; "
                       "position unsynchronized\n");
            else
                printf("info string UCI input too long: ignored; position "
                       "unsynchronized\n");
            fflush(stdout);
            continue;
        }

        /* Remove trailing UCI whitespace. Then skip unknown leading tokens   */
        /* until the first recognized UCI command. read_uci_line() has        */
        /* already removed the line ending.                                   */
        {
            size_t len = strlen(line);
            while (len > 0 && isspace((unsigned char)line[len-1]))
                line[--len] = 0;
        }
        cmd = find_uci_command(line);
        if (!cmd)
            continue;

        if (command_is(cmd, "uci")) {
            printf("id name micro-Max 4.8 UCI (Core) v1.0\n");
            printf("id author UCI integration by 64Logic; "
                   "micro-Max 4.8 by H.G. Muller\n");
            printf("info string micro-Max 4.8: No FEN Input / "
                   "No Clock Awareness / No Underpromotion\n");
            printf("uciok\n");
        }
        else if (command_is(cmd, "debug")) {
            const char *arg = cmd + 5;
            while (*arg && isspace((unsigned char)*arg))
                arg++;
            if (!strcmp(arg, "on"))
                debug_enabled = 1;
            else if (!strcmp(arg, "off"))
                debug_enabled = 0;
        }
        else if (command_is(cmd, "isready")) {
            printf("readyok\n");
        }
        else if (command_is(cmd, "setoption")) {
            /* No UCI options are defined, so 'setoption' has nothing to      */
            /* change.                                                        */
            if (debug_enabled)
                printf("info string 'setoption' ignored: no UCI options\n");
        }
        else if (command_is(cmd, "register")) {
            /* Registration is not required, so 'register' is ignored.        */
            if (debug_enabled)
                printf("info string 'register' ignored: registration not "
                       "required\n");
        }
        else if (command_is(cmd, "ucinewgame")) {
            /* 'ucinewgame' discards any held result and unconfirmed selected */
            /* move from the previous search.                                 */
            clear_held_search();
            clear_pending_search_move();
            game_init();
            applied_count = 0;
        }
        else if (command_is(cmd, "position")) {
            char *tok;
            static char linecopy[MAX_UCI_LINE];
            static char new_moves[MAX_TRACKED_MOVES][8];
            int new_count = 0;
            int move_overflow = 0;

            /* Receiving a new 'position' command supersedes any held result  */
            /* from the previous search. Discard that result before           */
            /* processing the command.                                        */
            clear_held_search();

            /* read_uci_line() guarantees dispatched commands fit             */
            /* MAX_UCI_LINE.                                                  */
            memcpy(linecopy, cmd, strlen(cmd) + 1);

            /* UCI permits arbitrary whitespace between tokens. The line      */
            /* reader already removes CR/LF terminators. strtok() also        */
            /* accepts tabs, form feeds, and vertical tabs here, so they      */
            /* cannot accidentally join two tokens.                           */
            /*                                                                */
            /* The first strtok() call starts parsing the copied command and  */
            /* returns the already validated 'position' command.              */
            strtok(linecopy, " \t\r\n\v\f");
            tok = strtok(NULL, " \t\r\n\v\f");

            if (tok && !strcmp(tok, "startpos")) {
                tok = strtok(NULL, " \t\r\n\v\f");
                if (tok && !strcmp(tok, "moves")) {
                    while ((tok = strtok(NULL, " \t\r\n\v\f")) != NULL) {
                        if (new_count >= MAX_TRACKED_MOVES) {
                            move_overflow = 1;
                            break;
                        }
                        strncpy(new_moves[new_count], tok, 7);
                        new_moves[new_count][7] = 0;
                        new_count++;
                    }
                }
                else if (tok) {
                    printf("info string 'position startpos': expected 'moves' "
                           "or end of line\n");
                    position_synced = 0;
                    clear_pending_search_move();
                    fflush(stdout);
                    continue;
                }

                /* Reject move histories that exceed tracking capacity.       */
                /* Leave the current engine position unchanged.               */
                if (move_overflow) {
                    printf("info string move history exceeds %d-ply limit and "
                           "cannot be represented\n", MAX_TRACKED_MOVES);
                    position_synced = 0;
                    clear_pending_search_move();
                    fflush(stdout);
                    continue;
                }

                /* UCI does not require 'ucinewgame' before the first         */
                /* 'position' command. Initialize canonical start state       */
                /* before applying moves.                                     */
                if (!game_started)
                    game_init();

                /* Count how many moves at the start of new_moves[] match the */
                /* moves already recorded in applied_moves[].                 */
                {
                    int matching_moves = 0;
                    int applied_ok;
                    int force_replay = 0;
                    int failure_kind = APPLY_RANGE_OK;
                    while (matching_moves < applied_count &&
                           matching_moves < new_count &&
                           !strcmp(applied_moves[matching_moves],
                                   new_moves[matching_moves]))
                        matching_moves++;

                    /* A completed search leaves canonical state one selected */
                    /* move ahead. If the next 'position' history extends the */
                    /* tracked history with that move, record it without      */
                    /* applying that move again. Any other 'position' history */
                    /* requires a replay from 'startpos'.                     */
                    if (search_move_pending) {
                        if (position_synced &&
                            matching_moves == applied_count &&
                            new_count > applied_count &&
                            !strcmp(new_moves[applied_count],
                                    pending_search_move)) {
                            strcpy(applied_moves[applied_count],
                                   new_moves[applied_count]);
                            applied_count++;
                            matching_moves++;
                            clear_pending_search_move();
                        } else {
                            clear_pending_search_move();
                            force_replay = 1;
                        }
                    }

                    if (!force_replay && position_synced &&
                        matching_moves == applied_count &&
                        new_count >= applied_count) {
                        int old_applied_count = applied_count;

                        /* When the new move history extends the synchronized */
                        /* history already applied, apply only the newly      */
                        /* appended moves to preserve micro-Max search and    */
                        /* game history.                                      */
                        applied_ok = apply_move_range(new_moves, applied_count,
                                                      new_count, &failure_kind);

                        /* If D() rejects a supported newly appended move,    */
                        /* replay the complete move history supplied by the   */
                        /* 'position' command once from 'startpos'. The       */
                        /* integration does not inspect or selectively        */
                        /* repair A[] entries, invent a move, or retry.       */
                        if (applied_ok < new_count &&
                            failure_kind == APPLY_RANGE_REJECTED) {
                            if (debug_enabled) {
                                printf("info string move rejected at ply %d: "
                                       "replaying move history from "
                                       "'startpos'\n", applied_ok + 1);
                                fflush(stdout);
                            }
                            game_init();
                            failure_kind = APPLY_RANGE_OK;
                            applied_ok = apply_move_range(new_moves, 0,
                                                          new_count,
                                                          &failure_kind);
                            memcpy(applied_moves, new_moves,
                                   (size_t)applied_ok * sizeof new_moves[0]);

                            if (applied_ok == new_count) {
                                if (debug_enabled)
                                    printf("info string replay succeeded: "
                                           "position synchronization "
                                           "restored\n");
                            } else if (failure_kind == APPLY_RANGE_REJECTED) {
                                printf("info string replay failed at ply %d: "
                                       "position unsynchronized after '%s'\n",
                                       applied_ok + 1, new_moves[applied_ok]);
                            }
                        } else {
                            /* Invalid move tokens and underpromotion do not  */
                            /* call D(). Record the applied prefix.           */
                            /* apply_move_range() starts at                   */
                            /* old_applied_count, so copy only entries        */
                            /* applied by that call.                          */
                            if (applied_ok > old_applied_count)
                                memcpy(applied_moves + old_applied_count,
                                       new_moves + old_applied_count,
                                       (size_t)(applied_ok -
                                                old_applied_count) *
                                       sizeof new_moves[0]);
                        }
                    } else {
                        /* A different move history, or any previously        */
                        /* unsynchronized state, requires a fresh replay from */
                        /* 'startpos'. Replay once and do not retry on        */
                        /* failure.                                           */
                        if (!position_synced && debug_enabled) {
                            printf("info string position unsynchronized: "
                                   "replaying full move history from "
                                   "'startpos'\n");
                            fflush(stdout);
                        }
                        game_init();
                        applied_ok = apply_move_range(new_moves, 0, new_count,
                                                      &failure_kind);
                        memcpy(applied_moves, new_moves,
                               (size_t)applied_ok * sizeof new_moves[0]);
                        if (applied_ok < new_count &&
                            failure_kind == APPLY_RANGE_REJECTED)
                            printf("info string move '%s' rejected at ply %d: "
                                   "position unsynchronized\n",
                                   new_moves[applied_ok], applied_ok + 1);
                    }

                    applied_count = applied_ok;
                    position_synced = (applied_ok == new_count);
                }
            }
            else if (tok && !strcmp(tok, "fen")) {
                printf("info string 'position fen' unsupported: use "
                       "'position startpos [moves ...]'\n");
                position_synced = 0;
                clear_pending_search_move();
            }
            else {
                printf("info string unsupported 'position' command: use "
                       "'position startpos [moves ...]'\n");
                position_synced = 0;
                clear_pending_search_move();
            }
        }
        else if (command_is(cmd, "go")) {
            struct go_request request;
            int hold_mode;
            int can_search = 1;
            char result_move[8];

            /* Only one UCI search request can remain outstanding. A second   */
            /* 'go' cannot replace it or create another pending 'bestmove'.   */
            if (held_search_mode) {
                if (debug_enabled)
                    printf("info string 'go' ignored: search active\n");
            } else {
                /* Classify every standard UCI 'go' parameter.                */
                /* Unsupported limits never alter micro-Max search behavior.  */
                parse_go_request(cmd, &request);
                hold_mode = (request.ponder ? HOLD_PONDER : 0) |
                            (request.infinite ? HOLD_INFINITE : 0);
                strcpy(result_move, "0000");

                /* 'searchmoves' changes the allowed root move set. Running   */
                /* an unrestricted search would misrepresent the request.     */
                if (request.searchmoves) {
                    printf("info string 'searchmoves' unsupported\n");
                    can_search = 0;
                }

                /* Never search from canonical state that is not trusted.     */
                if (can_search && !position_synced) {
                    printf("info string position unsynchronized: search "
                           "refused\n");
                    can_search = 0;
                }

                if (can_search && !game_started)
                    game_init();

                /* A previous D() may have committed its selected move even   */
                /* though no new 'position' command has been received. A      */
                /* repeated 'go' must search the last requested position      */
                /* again.                                                     */
                if (can_search && search_move_pending) {
                    if (debug_enabled)
                        printf("info string repeated 'go': restoring last "
                               "requested position\n");
                    if (!restore_requested_position())
                        can_search = 0;
                }

                /* Never make an engine move that cannot be tracked when the  */
                /* next complete 'position' history is received.              */
                if (can_search && applied_count >= MAX_TRACKED_MOVES) {
                    printf("info string move history full: search refused to "
                           "preserve synchronization\n");
                    can_search = 0;
                }

                if (can_search) {
                    char board_before[129];
                    int is_promo;
                    int result_nodes;
                    int move_committed;

                    /* Clock fields are recognized but micro-Max 4.8 has no   */
                    /* clock awareness.                                       */
                    if (request.clock_fields && debug_enabled)
                        printf("info string clock input ignored: micro-Max 4.8 "
                               "has no clock awareness\n");

                    /* Exact 'depth', 'nodes', 'mate', and 'movetime'         */
                    /* limits cannot be imposed without changing micro-Max    */
                    /* search termination.                                    */
                    if (request.search_limits &&
                        (!warned_search_limits || debug_enabled))
                        printf("info string search limits unsupported: "
                               "running micro-Max 4.8 search\n");
                    if (request.search_limits)
                        warned_search_limits = 1;

                    /* 'ponder' and 'infinite' use the same micro-Max search. */
                    /* Only delivery of 'bestmove' is delayed.                */
                    if (request.ponder && (!warned_ponder || debug_enabled))
                        printf("info string 'ponder': micro-Max 4.8 "
                               "ponder hold clears on 'ponderhit'/'stop'\n");
                    if (request.ponder)
                        warned_ponder = 1;

                    if (request.infinite &&
                        (!warned_infinite || debug_enabled))
                        printf("info string 'infinite': micro-Max 4.8 "
                               "search result held until 'stop'\n");
                    if (request.infinite)
                        warned_infinite = 1;

                    /* D() commits its selected move to canonical state.      */
                    /* Snapshot the board before the search so the            */
                    /* integration can detect whether a move was committed    */
                    /* and whether it was a promotion.                        */
                    memcpy(board_before, b, sizeof board_before);

                    /* Set K = I so D() searches rather than applying a       */
                    /* supplied move.                                         */
                    K = I;

                    /* Set N = 128 before D(). N-S below is the node count    */
                    /* for this D() call, not a cumulative counter for the    */
                    /* whole game. Flush any pending UCI output before        */
                    /* entering the synchronous D() search.                   */
                    fflush(stdout);
                    N = 128;
                    D(-I, I, Q, O, 1, 3);
                    result_nodes = N - S;
                    move_committed = real_board_changed(board_before);

                    if (move_committed) {
                        /* Detect whether the move D() just committed was a   */
                        /* promotion. Pawn types are < 3 in micro-Max.        */
                        /* A pawn reaching either back rank promotes.         */
                        is_promo = ((board_before[K] & 7) < 3) &&
                                   ((L & 0x70) == 0 || (L & 0x70) == 0x70);
                        format_move(result_move, K, L, is_promo);

                        /* D() has advanced canonical state by the selected   */
                        /* move. The next 'position' command can confirm that */
                        /* move as part of the requested position history.    */
                        set_pending_search_move(result_move);

                        /* Report only search data available after D()        */
                        /* returns. Search depth is local to D() and is not   */
                        /* available to the integration after the search.     */
                        printf("info nodes %d pv %s\n", result_nodes,
                               result_move);
                    } else {
                        printf("info nodes %d\n", result_nodes);
                    }
                }

                if (hold_mode)
                    hold_bestmove(result_move, hold_mode);
                else
                    printf("bestmove %s\n", result_move);
            }
        }
        else if (command_is(cmd, "stop")) {
            /* If a completed result is being held, 'stop' releases its       */
            /* pending 'bestmove'. An idle 'stop' has no effect.              */
            if (held_search_mode)
                release_held_search();
            else if (debug_enabled)
                printf("info string 'stop' ignored: no search to stop\n");
        }
        else if (command_is(cmd, "ponderhit")) {
            /* If both 'ponder' and 'infinite' are present, 'ponderhit'       */
            /* clears only the ponder hold; 'infinite' continues to delay     */
            /* 'bestmove' until 'stop'.                                       */
            if (held_search_mode == HOLD_PONDER) {
                release_held_search();
            } else if (held_search_mode & HOLD_PONDER) {
                held_search_mode &= ~HOLD_PONDER;
            } else if (debug_enabled) {
                printf("info string 'ponderhit' ignored: no ponder search "
                       "active\n");
            }
        }
        else if (command_is(cmd, "quit")) {
            /* 'quit' discards any held result without sending 'bestmove'.    */
            break;
        }

        fflush(stdout);
    }
    return 0;
}
