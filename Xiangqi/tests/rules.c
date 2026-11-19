/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "xiangqi.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#ifdef XQ_TEST_REFERENCE
extern int xq_check_reference(const xq_board *, int);
#define CHECK_FAST(p) \
    do { \
        assert(xq_check(p, 1) == xq_check_reference(p, 1)); \
        assert(xq_check(p, -1) == xq_check_reference(p, -1)); \
    } while (0)
#else
#define CHECK_FAST(p) ((void)0)
#endif
static xq_board b;
static xq_move moves[XQ_MOVES];

static int has(int f, int t)
{
    int n = xq_legal(&b, moves);
    for (int i = 0; i < n; i++)
        if (moves[i].from == f && moves[i].to == t)
            return 1;
    return 0;
}

static void bare(void)
{
    memset(&b, 0, sizeof(b));
    b.side = 1;
    b.sq[4] = -1;
    b.sq[85] = 1;
    b.sq[49] = 7;
}

static unsigned perft(const xq_board *p, int depth)
{
    if (!depth)
        return 1;
    xq_move m[XQ_MOVES];
    int n = xq_legal(p, m);
    unsigned total = 0;
    for (int i = 0; i < n; i++) {
        xq_board q;
        xq_apply(&q, p, m[i]);
        total += perft(&q, depth - 1);
    }
    return total;
}

int main(void)
{
    xq_reset(&b);
    assert(xq_valid(&b));
    assert(xq_legal(&b, moves) == 44);
    assert(!xq_check(&b, 1) && !xq_check(&b, -1));
    assert(perft(&b, 2) == 1920);
    assert(perft(&b, 3) == 79666);
    /* Horse legs, elephant eyes, river and palace. */
    assert(has(82, 63));
    b.sq[73] = 7;
    assert(!has(82, 63) && !has(82, 65));
    xq_reset(&b);
    assert(has(83, 63));
    b.sq[73] = 7;
    assert(!has(83, 63));
    bare();
    b.sq[38] = 3;
    assert(!has(38, 18));
    bare();
    assert(!has(85, 87));
    b.sq[84] = 2;
    assert(has(84, 76));
    assert(!has(84, 74));
    /* Cannon must have exactly one screen to capture; cannot jump to empty. */
    bare();
    b.sq[63] = 6;
    b.sq[9] = -5;
    assert(!has(63, 9));
    b.sq[45] = 7;
    assert(has(63, 9));
    assert(!has(63, 36));
    b.sq[27] = -7;
    assert(!has(63, 9));
    /* Pawn cannot retreat or sidestep before crossing. */
    bare();
    b.sq[54] = 7;
    assert(has(54, 45) && !has(54, 55) && !has(54, 63));
    b.sq[54] = 0;
    b.sq[36] = 7;
    assert(has(36, 37) && has(36, 27) && !has(36, 45));
    /* Moving the only screen must not expose the two generals. */
    bare();
    b.sq[49] = 5;
    assert(!has(49, 48));
    /* Check, checkmate and stalemate all remove unsafe moves. */
    bare();
    b.sq[49] = 0;
    b.sq[13] = 5;
    assert(xq_check(&b, -1));
    /* Checkmate and stalemate both end the game, including a non-check loss. */
    memset(&b, 0, sizeof(b));
    b.side = -1;
    b.sq[4] = -1;
    b.sq[85] = 1;
    b.sq[12] = 5;
    b.sq[14] = 5;
    b.sq[22] = 5;
    assert(xq_check(&b, -1) && xq_legal(&b, moves) == 0);
    b.sq[22] = 0;
    b.sq[49] = 7;
    assert(!xq_check(&b, -1) && xq_legal(&b, moves) == 0);
    /* Each generated move preserves the moving side's king. */
    xq_reset(&b);
    int n = xq_legal(&b, moves);
    for (int i = 0; i < n; i++) {
        xq_board next;
        xq_apply(&next, &b, moves[i]);
        assert(!xq_check(&next, 1));
        assert(next.side == -1);
    }
    /* Search advances in bounded steps and returns a legal move without mutation. */
    static xq_search s;
    xq_board original = b;
    xq_search_begin(&s, &b, 1);
    unsigned calls = 0;
    while (!s.done && calls++ < 100000) {
        unsigned old = s.nodes;
        xq_search_step(&s, 7);
        assert(s.nodes - old <= 7);
    }
    assert(s.done && s.completed >= 1 && !memcmp(&b, &original, sizeof(b)) &&
           has(s.best.from, s.best.to));
    /* Do not trade a cannon for the corner horse when the rook recaptures. */
    xq_reset(&b);
    xq_board response;
    xq_apply(&response, &b, (xq_move){54, 45});
    xq_search_begin(&s, &response, 1);
    for (unsigned steps = 0; !s.done && steps < 1000000; steps++)
        xq_search_step(&s, 64);
    printf("Opening response %u->%u depth=%d nodes=%u\n", s.best.from, s.best.to, s.completed,
           s.nodes);
    assert(s.done && !(s.best.from == 19 && s.best.to == 82));
    /* A defended pawn can be a forced mate, not just a material exchange. */
    memset(&b, 0, sizeof(b));
    b.side = 1;
    b.sq[85] = 1;
    b.sq[4] = -1;
    b.sq[20] = 4;
    b.sq[24] = 4;
    b.sq[22] = 7;
    b.sq[49] = 6;
    b.sq[12] = 5;
    b.sq[13] = -7;
    assert(!xq_check(&b, -1));
    xq_search_begin(&s, &b, 2);
    for (unsigned steps = 0; !s.done && steps < 1000000; steps++)
        xq_search_step(&s, 64);
    xq_board mate;
    xq_apply(&mate, &b, s.best);
    printf("Best %u->%u remaining=%d check=%d depth=%d nodes=%u\n", s.best.from, s.best.to,
           xq_legal(&mate, moves), xq_check(&mate, -1), s.completed, s.nodes);
    fflush(stdout);
    assert(s.done && xq_legal(&mate, moves) == 0 && xq_check(&mate, -1));
    printf("Forced mate found: %u->%u depth=%d nodes=%u\n", s.best.from, s.best.to, s.completed,
           s.nodes);
    unsigned random = 317;
    for (int game = 0; game < 12; game++) {
        xq_reset(&b);
        for (int ply = 0; ply < 150; ply++) {
            int count = xq_legal(&b, moves);
            if (!count)
                break;
            random = random * 1664525u + 1013904223u;
            xq_board next;
            xq_apply(&next, &b, moves[random % (unsigned)count]);
            assert(!xq_check(&next, b.side));
            assert(xq_valid(&next));
            CHECK_FAST(&next);
            b = next;
        }
    }
    printf("Rules and cooperative AI passed; nodes=%u depth=%d\n", s.nodes, s.completed);
    return 0;
}
