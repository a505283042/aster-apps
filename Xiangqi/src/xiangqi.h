/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef XQ_H
#define XQ_H
#include <stdint.h>
enum {XQ_KING=1,XQ_GUARD,XQ_ELEPHANT,XQ_HORSE,XQ_ROOK,XQ_CANNON,XQ_PAWN};
#define XQ_MOVES 256
#define XQ_DEPTH 20
#define XQ_TT 4096
typedef struct {int8_t sq[90],side;uint16_t quiet;} xq_board;
typedef struct {uint8_t from,to;} xq_move;
void xq_reset(xq_board *b);
int xq_valid(const xq_board *b);
int xq_check(const xq_board *b,int side);
int xq_legal(const xq_board *b,xq_move *out);
void xq_apply(xq_board *out,const xq_board *b,xq_move m);
int xq_evaluate(const xq_board *b);
typedef struct {xq_board board;xq_move moves[XQ_MOVES],best;int count,index,depth,alpha,beta,score,ply,entered,original_alpha,check;uint32_t key;} xq_node;
typedef struct {uint32_t key;int16_t score;int8_t depth;uint8_t bound;xq_move best;} xq_entry;
typedef struct {xq_entry table[XQ_TT];int history[90][90];xq_move killers[XQ_DEPTH][2];xq_node stack[XQ_DEPTH];xq_board root;xq_move best;unsigned nodes,limit;int top,iteration,completed,max_depth,done;} xq_search;
void xq_search_begin(xq_search *s,const xq_board *b,int level);
void xq_search_step(xq_search *s,unsigned budget);
#endif
