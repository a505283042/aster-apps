/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "xiangqi.h"
#ifdef __wasm__
#include <aster_imports.h>
#else
#include <string.h>
#endif
static int ab(int a){return a<0?-a:a;}
static int sign(int a){return (a>0)-(a<0);}
static int palace(int x,int y,int side){return x>=3&&x<=5&&(side>0?y>=7:y<=2);}
static int crossed(int y,int side){return side>0?y<=4:y>=5;}
static int between(const xq_board *b,int from,int to){
 int x=from%9,y=from/9,dx=sign(to%9-x),dy=sign(to/9-y),n=0;
 for(x+=dx,y+=dy;y*9+x!=to;x+=dx,y+=dy)n+=b->sq[y*9+x]!=0;
 return n;
}
static int attacks(const xq_board *b,int f,int t){
 int p=b->sq[f],s=sign(p),x=f%9,y=f/9,tx=t%9,ty=t/9,dx=tx-x,dy=ty-y;
 switch(ab(p)){
 case XQ_KING:
  if(!dx&&b->sq[t]==-s*XQ_KING&&!between(b,f,t))return 1;
  return ab(dx)+ab(dy)==1&&palace(tx,ty,s);
 case XQ_GUARD:return ab(dx)==1&&ab(dy)==1&&palace(tx,ty,s);
 case XQ_ELEPHANT:return ab(dx)==2&&ab(dy)==2&&(s>0?ty>=5:ty<=4)&&!b->sq[(y+dy/2)*9+x+dx/2];
 case XQ_HORSE:
  if(ab(dx)==2&&ab(dy)==1)return !b->sq[y*9+x+dx/2];
  if(ab(dx)==1&&ab(dy)==2)return !b->sq[(y+dy/2)*9+x];
  return 0;
 case XQ_ROOK:return (!dx||!dy)&&!between(b,f,t);
 case XQ_CANNON:return (!dx||!dy)&&between(b,f,t)==(b->sq[t]!=0);
 case XQ_PAWN:return (!dx&&dy==-s)||(crossed(y,s)&&!dy&&ab(dx)==1);
 }return 0;
}
void xq_reset(xq_board *b){
 static const int8_t back[9]={5,4,3,2,1,2,3,4,5};memset(b,0,sizeof(*b));b->side=1;
 for(int x=0;x<9;x++){b->sq[x]=-back[x];b->sq[81+x]=back[x];}
 b->sq[19]=b->sq[25]=-6;b->sq[64]=b->sq[70]=6;
 for(int x=0;x<9;x+=2){b->sq[27+x]=-7;b->sq[54+x]=7;}
}
int xq_valid(const xq_board *b){
 int red=0,black=0;if(b->side!=1&&b->side!=-1)return 0;
 for(int i=0;i<90;i++){int p=b->sq[i];if(ab(p)>7)return 0;if(p==1){red++;if(!palace(i%9,i/9,1))return 0;}if(p==-1){black++;if(!palace(i%9,i/9,-1))return 0;}}
 return red==1&&black==1;
}
int xq_check(const xq_board *b,int side){
 int king=-1;for(int i=0;i<90;i++)if(b->sq[i]==side){king=i;break;}
 if(king<0)return 1;
 int x=king%9,y=king/9;
 const int axes[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
 for(int d=0;d<4;d++){
  int screen=0;
  for(int n=1;n<10;n++){
   int xx=x+axes[d][0]*n,yy=y+axes[d][1]*n;if(xx<0||xx>8||yy<0||yy>9)break;
   int p=b->sq[yy*9+xx];if(!p)continue;
   if(!screen){
    if(p==-side*XQ_ROOK||(d>=2&&p==-side*XQ_KING))return 1;
    if(n==1&&p==-side*XQ_PAWN&&attacks(b,yy*9+xx,king))return 1;
    screen=1;
   }else{if(p==-side*XQ_CANNON)return 1;break;}
  }
 }
 const int horse[8][2]={{1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,-1},{-2,1},{-1,2}};
 for(int i=0;i<8;i++){
  int xx=x+horse[i][0],yy=y+horse[i][1];if(xx<0||xx>8||yy<0||yy>9)continue;
  int f=yy*9+xx;if(b->sq[f]==-side*XQ_HORSE&&attacks(b,f,king))return 1;
 }
 for(int step=1;step<=2;step++)for(int dx=-1;dx<=1;dx+=2)for(int dy=-1;dy<=1;dy+=2){
  int xx=x+dx*step,yy=y+dy*step;if(xx<0||xx>8||yy<0||yy>9)continue;
  int f=yy*9+xx,p=b->sq[f];if(p==-side*(step==1?XQ_GUARD:XQ_ELEPHANT)&&attacks(b,f,king))return 1;
 }
 return 0;
}
#ifdef XQ_TEST_REFERENCE
int xq_check_reference(const xq_board *b,int side){
 int king=-1;for(int i=0;i<90;i++)if(b->sq[i]==side)king=i;
 if(king<0)return 1;
 for(int i=0;i<90;i++)if(sign(b->sq[i])==-side&&attacks(b,i,king))return 1;
 return 0;
}
#endif
void xq_apply(xq_board *out,const xq_board *b,xq_move m){
 int p=b->sq[m.from],capture=b->sq[m.to];*out=*b;out->sq[m.to]=(int8_t)p;out->sq[m.from]=0;out->side=-b->side;
 out->quiet=(capture||ab(p)==XQ_PAWN)?0:(uint16_t)(b->quiet+1);
}
static void candidate(const xq_board *b,xq_move *out,int *n,int from,int x,int y,int captures){
 if(x<0||x>8||y<0||y>9||*n>=XQ_MOVES)return;
 int to=y*9+x;
 if((captures&&!b->sq[to])||sign(b->sq[to])==b->side||!attacks(b,from,to))return;
 xq_move m={(uint8_t)from,(uint8_t)to};xq_board next;xq_apply(&next,b,m);
 if(!xq_check(&next,b->side))out[(*n)++]=m;
}
static int generate(const xq_board *b,xq_move *out,int captures){
 static const int axes[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
 static const int horses[8][2]={{1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,-1},{-2,1},{-1,2}};
 int n=0;
 for(int f=0;f<90;f++){
  int p=b->sq[f],kind=ab(p),x=f%9,y=f/9;if(sign(p)!=b->side)continue;
  if(kind==XQ_ROOK||kind==XQ_CANNON){
   for(int d=0;d<4;d++){int screen=0;for(int k=1;k<10;k++){
    int tx=x+axes[d][0]*k,ty=y+axes[d][1]*k;if(tx<0||tx>8||ty<0||ty>9)break;
    int occupied=b->sq[ty*9+tx]!=0;
    if(kind==XQ_ROOK){candidate(b,out,&n,f,tx,ty,captures);if(occupied)break;}
    else if(!screen){if(occupied)screen=1;else candidate(b,out,&n,f,tx,ty,captures);}
    else if(occupied){candidate(b,out,&n,f,tx,ty,captures);break;}
   }}
  }else if(kind==XQ_HORSE){for(int k=0;k<8;k++)candidate(b,out,&n,f,x+horses[k][0],y+horses[k][1],captures);}
  else if(kind==XQ_ELEPHANT||kind==XQ_GUARD){int step=kind==XQ_ELEPHANT?2:1;for(int dx=-1;dx<=1;dx+=2)for(int dy=-1;dy<=1;dy+=2)candidate(b,out,&n,f,x+dx*step,y+dy*step,captures);}
  else if(kind==XQ_KING){for(int d=0;d<4;d++)candidate(b,out,&n,f,x+axes[d][0],y+axes[d][1],captures);for(int t=0;t<90;t++)if(b->sq[t]==-p)candidate(b,out,&n,f,t%9,t/9,captures);}
  else {candidate(b,out,&n,f,x,y-b->side,captures);if(crossed(y,b->side)){candidate(b,out,&n,f,x-1,y,captures);candidate(b,out,&n,f,x+1,y,captures);}}
 }return n;
}

int xq_legal(const xq_board *b,xq_move *out){return generate(b,out,0);}
static const int value[8]={0,20000,190,200,420,950,460,110};
int xq_evaluate(const xq_board *b){
 int score=0,rooks[2]={0,0},horses[2]={0,0};
 for(int i=0;i<90;i++){
  int p=b->sq[i],s=sign(p),k=ab(p),y=i/9,x=i%9;if(!p)continue;
  int v=value[k],advance=s>0?9-y:y,center=4-ab(x-4),which=s>0;
  if(k==XQ_PAWN){
   v+=crossed(y,s)?90+advance*12+center*8:advance*5;
   if(advance==9)v-=65;
   if(x&&b->sq[i-1]==p)v+=18;
   if(x<8&&b->sq[i+1]==p)v+=18;
  }
  if(k==XQ_HORSE){
   horses[which]++;v+=center*9+(advance>1&&advance<8?22:0);
   const int d[4]={-9,9,-1,1};
   for(int j=0;j<4;j++){int t=i+d[j];if(t>=0&&t<90&&ab(t%9-x)<=1&&!b->sq[t])v+=9;}
  }
  if(k==XQ_ROOK||k==XQ_CANNON){
   if(k==XQ_ROOK)rooks[which]++;
   const int dx[4]={1,-1,0,0},dy[4]={0,0,1,-1};
   for(int d=0;d<4;d++)for(int n=1;n<10;n++){
    int xx=x+dx[d]*n,yy=y+dy[d]*n;if(xx<0||xx>8||yy<0||yy>9)break;
    int target=b->sq[yy*9+xx];if(!target)v+=k==XQ_ROOK?4:2;else{if(sign(target)!=s)v+=6;break;}
   }
   v+=center*(k==XQ_ROOK?3:5);
  }
  if(k==XQ_GUARD||k==XQ_ELEPHANT)v+=advance<3?12:0;
  if(k==XQ_KING){v-=advance*12;for(int d=-1;d<=1;d+=2){int t=i+d;if(t>=0&&t<90&&b->sq[t]==s*XQ_GUARD)v+=16;}}
  score+=s*v;
 }
 if(rooks[1]==2)score+=25;
 if(rooks[0]==2)score-=25;
 if(horses[1]==2)score+=12;
 if(horses[0]==2)score-=12;
 return score*b->side+8;
}
static uint32_t position_key(const xq_board *b){
 uint32_t h=2166136261u;for(int i=0;i<90;i++)h=(h^(uint8_t)b->sq[i])*16777619u;
 return (h^(uint8_t)b->side)*16777619u;
}
static int same(xq_move a,xq_move b){return a.from==b.from&&a.to==b.to;}
static void root_iteration(xq_search *s){
 xq_node *n=&s->stack[0];memset(n,0,sizeof(*n));n->board=s->root;n->depth=s->iteration;n->alpha=-30000;n->beta=30000;s->top=0;
}
void xq_search_begin(xq_search *s,const xq_board *b,int level){
 memset(s,0,sizeof(*s));s->root=*b;s->iteration=1;s->max_depth=level==0?3:level==1?6:8;s->limit=level==0?15000:level==1?150000:600000;
 xq_move moves[XQ_MOVES];int n=xq_legal(b,moves);s->best=n?moves[0]:(xq_move){255,255};s->done=!n;root_iteration(s);
}
static void finish_node(xq_search *s,int score){
 xq_node *n=&s->stack[s->top];
 if(n->depth>0&&n->entered&&ab(score)<28000){
  xq_entry *e=&s->table[n->key&(XQ_TT-1)];
  if(e->key!=n->key||n->depth>=e->depth){e->key=n->key;e->score=(int16_t)score;e->depth=(int8_t)n->depth;e->best=n->best;e->bound=score<=n->original_alpha?2:score>=n->beta?3:1;}
 }
 if(!s->top){
  if(n->best.from<90)s->best=n->best;
  s->completed=s->iteration;
  if(++s->iteration>s->max_depth||s->nodes>=s->limit||ab(score)>28000){s->done=1;return;}
  root_iteration(s);return;
 }
 xq_move move=s->stack[s->top-1].moves[s->stack[s->top-1].index-1];s->top--;xq_node *p=&s->stack[s->top];score=-score;
 if(score>p->score){p->score=score;p->best=move;}if(score>p->alpha)p->alpha=score;
 if(p->alpha>=p->beta){
  if(!p->board.sq[move.to]&&p->depth>0){
   if(!same(s->killers[p->ply][0],move)){s->killers[p->ply][1]=s->killers[p->ply][0];s->killers[p->ply][0]=move;}
   int *h=&s->history[move.from][move.to];if(*h<100000)*h+=p->depth*p->depth;
  }
  p->index=p->count;
 }
}
static int order_value(const xq_search *s,const xq_node *n,xq_move m,xq_move preferred){
 if(same(m,preferred))return 1000000;
 int capture=ab(n->board.sq[m.to]);
 if(capture)return 100000+value[capture]*16-value[ab(n->board.sq[m.from])];
 if(same(m,s->killers[n->ply][0]))return 90000;
 if(same(m,s->killers[n->ply][1]))return 80000;
 return s->history[m.from][m.to];
}
void xq_search_step(xq_search *s,unsigned budget){
 while(budget--&&!s->done){
  if(s->nodes>=s->limit){s->done=1;break;}
  xq_node *n=&s->stack[s->top];
  if(!n->entered){
   s->nodes++;n->entered=1;n->score=-30000;n->best=(xq_move){255,255};n->original_alpha=n->alpha;n->key=position_key(&n->board);
   /* Search-path repetition is a draw; do not cache its path-dependent score. */
   int repeat=0;for(int i=0;i<s->top;i++)if(s->stack[i].key==n->key&&!memcmp(s->stack[i].board.sq,n->board.sq,90)){repeat=1;break;}
   if(repeat){n->depth=0;finish_node(s,0);continue;}
   xq_entry *e=&s->table[n->key&(XQ_TT-1)];xq_move preferred=(xq_move){255,255};
   if(e->key==n->key){
    preferred=e->best;
    if(s->top&&n->depth>0&&e->depth>=n->depth&&(e->bound==1||(e->bound==2&&e->score<=n->alpha)||(e->bound==3&&e->score>=n->beta))){n->depth=0;finish_node(s,e->score);continue;}
   }
   if(!s->top&&s->completed)preferred=s->best;
   n->check=xq_check(&n->board,n->board.side);
   if(n->ply>=XQ_DEPTH-2||n->depth<=-5){finish_node(s,xq_evaluate(&n->board));continue;}
   if(n->depth<=0&&!n->check){
    n->score=xq_evaluate(&n->board);
    if(n->score>=n->beta){finish_node(s,n->score);continue;}
    if(n->score>n->alpha)n->alpha=n->score;
    n->count=generate(&n->board,n->moves,1);
   }else{
    n->count=xq_legal(&n->board,n->moves);
    if(!n->count){finish_node(s,-29000+n->ply);continue;}
   }
   for(int i=1;i<n->count;i++){
    xq_move m=n->moves[i];int j=i,key=order_value(s,n,m,preferred);
    while(j&&order_value(s,n,n->moves[j-1],preferred)<key){n->moves[j]=n->moves[j-1];j--;}n->moves[j]=m;
   }
  }
  if(n->index>=n->count){finish_node(s,n->score);continue;}
  xq_move m=n->moves[n->index++];xq_node *child=&s->stack[++s->top];memset(child,0,sizeof(*child));xq_apply(&child->board,&n->board,m);
  child->depth=n->depth-1;child->ply=n->ply+1;child->alpha=-n->beta;child->beta=-n->alpha;
 }
}
