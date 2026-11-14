/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <aster_imports.h>
#include <aster_gfx_engine.h>
#include <stddef.h>
#include "xiangqi.h"
#define HISTORY 128
#define PAPER ASTER_RGB565(242,229,201)
#define INK ASTER_RGB565(61,48,37)
#define MUTED ASTER_RGB565(135,111,80)
#define RED ASTER_RGB565(159,43,35)
#define GOLD ASTER_RGB565(184,133,54)
#define JADE ASTER_RGB565(58,115,93)
typedef struct {uint32_t magic,version,sequence;xq_board positions[HISTORY+1];xq_move moves[HISTORY];uint16_t count;uint8_t mode,level;int8_t human;uint8_t pad[3];uint32_t checksum;} saved_game;
static saved_game game,incoming;
static aster_handle_t surface=ASTER_HANDLE_INVALID;
static aster_ge_t ge;static aster_gfx_cmd_t ring[256];static uint8_t arena[8192];
static xq_search search;static xq_move moves[XQ_MOVES];static uint8_t destinations[90];
static int width,height,cell,bx,by,radius,small,selected=-1,dirty=1,menu,result,reason,thinking,paused;
static int pressed=-1,down_x,down_y,dialog_x,dialog_y,dialog_w;
static uint32_t ai_started,ai_draw;static int32_t sounds[4];static int end_dialog;static int frame_mode=-1;static int retry_cue=-1,retry_left;static uint32_t retry_at,retry_pitch,retry_gain;static const char *notice="";
static const char *levels[]={"入门","进阶","挑战"};
static xq_board *board(void){return &game.positions[game.count];}
static int abs_i(int x){return x<0?-x:x;}
static int flipped(void){return !game.mode&&game.human<0;}
static uint32_t checksum(const void *bytes,size_t length){uint32_t h=2166136261u;const uint8_t *p=bytes;while(length--)h=(h^*p++)*16777619u;return h;}
static int valid_save(saved_game *s){
 if(s->magic!=0x31515843||s->version!=1||s->count>HISTORY||s->mode>1||s->level>2||(s->human!=1&&s->human!=-1)||s->checksum!=checksum(s,offsetof(saved_game,checksum)))return 0;
 for(unsigned i=0;i<=s->count;i++)if(!xq_valid(&s->positions[i]))return 0;
 for(unsigned i=0;i<s->count;i++)if(s->moves[i].from>=90||s->moves[i].to>=90)return 0;
 return 1;
}
static void persist(void){
 game.magic=0x31515843;game.version=1;game.sequence++;game.checksum=checksum(&game,offsetof(saved_game,checksum));
 const char *key=(game.sequence&1)?"save.1":"save.0";int r=aster_kv_set(key,6,&game,sizeof(game));
 if(r<0){notice="存档暂不可用";aster_printf("Xiangqi save failed %d",r);}
}
static void load(void){
 memset(&game,0,sizeof(game));game.human=1;game.level=1;xq_reset(&game.positions[0]);
 for(int i=0;i<2;i++){
  const char *key=i?"save.1":"save.0";
  if(aster_kv_get(key,6,&incoming,sizeof(incoming))==(int)sizeof(incoming)&&valid_save(&incoming)&&(!game.magic||(int32_t)(incoming.sequence-game.sequence)>0))game=incoming;
 }
}
static void clear_selection(void){selected=-1;memset(destinations,0,sizeof(destinations));pressed=-1;}
static void adjudicate(void){
 result=reason=0;int n=xq_legal(board(),moves);
 if(!n){result=board()->side>0?2:1;reason=xq_check(board(),board()->side)?1:2;return;}
 int repeats=0;for(unsigned i=0;i<=game.count;i++)if(game.positions[i].side==board()->side&&!memcmp(game.positions[i].sq,board()->sq,90))repeats++;
 if(repeats>=3){result=3;reason=3;}else if(board()->quiet>=120){result=3;reason=4;}
}
static void new_game(void){
 int human=game.human,level=game.level,mode=game.mode;uint32_t sequence=game.sequence;
 memset(&game,0,sizeof(game));game.human=(int8_t)human;game.level=(uint8_t)level;game.mode=(uint8_t)mode;game.sequence=sequence;xq_reset(board());
 thinking=result=reason=end_dialog=0;notice="";clear_selection();persist();dirty=1;
}
static void sound_cue(int cue,uint32_t pitch,uint32_t gain){
 if(cue<0||cue>3||sounds[cue]<=0)return;
 aster_audio_effect_play_desc_t d={.size=sizeof(d),.version=1,.pitch_q16=pitch,.gain_q16=gain};
 int rc=aster_audio_effect_play_ex((uint32_t)sounds[cue],&d,sizeof(d));
 if(rc<0){
  aster_printf("Xiangqi sound cue=%d error=%d",cue,rc);
  if((rc==ASTER_EBUSY||rc==ASTER_ESTATE)&&retry_left>0){retry_cue=cue;retry_pitch=pitch;retry_gain=gain;retry_at=(uint32_t)aster_time_now_ms()+50;retry_left--;}
 }else retry_cue=-1;
}
static void sound_event(int cue,uint32_t pitch,uint32_t gain){retry_left=2;retry_cue=-1;sound_cue(cue,pitch,gain);}
static void play(xq_move move){
 if(game.count==HISTORY){memmove(game.positions,game.positions+1,HISTORY*sizeof(xq_board));memmove(game.moves,game.moves+1,(HISTORY-1)*sizeof(xq_move));game.count--;}
 game.moves[game.count]=move;xq_apply(&game.positions[game.count+1],board(),move);game.count++;thinking=0;clear_selection();adjudicate();persist();dirty=1;
 int cue=result?3:xq_check(board(),board()->side)?2:game.positions[game.count-1].sq[move.to]?1:0;
 sound_event(cue,cue?65536:(uint32_t)(61000+(move.from+move.to)%5*2500),65536);
 end_dialog=result!=0;
 aster_printf("Xiangqi move %u->%u side=%d result=%d",move.from,move.to,board()->side,result);
}
static void undo(void){
 if(!game.count||(!game.mode&&game.human<0&&game.count==1))return;
 thinking=0;do{game.count--;}while(game.count&&!game.mode&&board()->side!=game.human);
 clear_selection();adjudicate();end_dialog=0;persist();dirty=1;
}
static void text(int x,int y,int px,uint16_t color,const char *s){aster_ge_text(&ge,x,y+aster_text_asc(px),px,color,ASTER_GFX_NOBG,s);}
static void centered(int x,int y,int px,uint16_t color,const char *s){int w=aster_gfx_text_w(px,s,(uint32_t)strlen(s));text(x-(w>0?w:0)/2,y,px,color,s);}
static void disc(int x,int y,int r,uint16_t color){aster_ge_ellipse(&ge,x,y,r,r,color,0);}
static void line(int x,int y,int xx,int yy,uint16_t color){aster_ge_line_aa(&ge,x,y,xx,yy,color);}
static void point(int sq,int *x,int *y){int v=flipped()?89-sq:sq;*x=bx+(v%9)*cell;*y=by+(v/9)*cell;}
static int inside(int x,int y,int rx,int ry,int w,int h){return x>=rx&&x<rx+w&&y>=ry&&y<ry+h;}
static void button(int x,int y,int w,int h,const char *label,int active){
 aster_ge_rect_r(&ge,x,y,w,h,active?RED:ASTER_RGB565(225,209,178),5,0);centered(x+w/2,y+(h-aster_text_line_h(small?14:16))/2,small?14:16,active?PAPER:INK,label);
}
static void draw_piece(int sq,int p){
 static const char *red[]={"","帅","仕","相","马","车","炮","兵"};static const char *black[]={"","将","士","象","马","车","炮","卒"};
 int x,y;point(sq,&x,&y);
 if(small){
  /* One outline leaves the whole face for the 20px glyph. */
  aster_ge_rect_r(&ge,x-radius-1,y-radius-1,2*radius+3,2*radius+3,p>0?RED:INK,radius+1,0);aster_ge_rect_r(&ge,x-radius,y-radius,2*radius+1,2*radius+1,ASTER_RGB565(255,241,211),radius,0);
 }else{
  /* Keep the complete scene below the P4 host's 256-command limit. */
  aster_ge_rect_r(&ge,x-radius,y-radius,2*radius+1,2*radius+1,p>0?RED:INK,radius,0);
  aster_ge_rect_r(&ge,x-radius+1,y-radius+1,2*radius-1,2*radius-1,ASTER_RGB565(249,228,185),radius-1,0);
 }
 int px=small?20:28;centered(x,y-aster_text_line_h(px)/2,px,p>0?RED:INK,p>0?red[p]:black[-p]);
 if(sq==selected){aster_ge_ellipse(&ge,x,y,radius+2,radius+2,GOLD,1);aster_ge_ellipse(&ge,x,y,radius+3,radius+3,GOLD,1);}
}
static void draw_settings(void){
 dialog_w=small?220:352;dialog_x=(width-dialog_w)/2;dialog_y=(height-278)/2;
 aster_ge_rect_r(&ge,dialog_x-2,dialog_y-2,dialog_w+4,280,INK,10,0);aster_ge_rect_r(&ge,dialog_x,dialog_y,dialog_w,276,PAPER,8,0);
 centered(width/2,dialog_y+10,20,RED,"棋局设置");int x=dialog_x+12,w=dialog_w-24;
 button(x,dialog_y+48,w,32,game.mode?"对弈：双人同屏":"对弈：人机对弈",0);
 button(x,dialog_y+88,w,32,game.human>0?"执子：红方先行":"执子：黑方应战",0);
 int seg=(w-8)/3;for(int i=0;i<3;i++)button(x+i*(seg+4),dialog_y+128,seg,32,levels[i],game.level==i);
 button(x,dialog_y+174,w,34,"开始新局",1);button(x,dialog_y+218,w,34,"返回棋局",0);
}
static void draw_end(void){
 int w=small?220:340,x=(width-w)/2,y=(height-208)/2;
 aster_ge_rect_r(&ge,x-2,y-2,w+4,212,RED,9,0);aster_ge_rect_r(&ge,x,y,w,208,PAPER,8,0);
 centered(width/2,y+16,small?24:28,RED,result==1?"红方获胜":result==2?"黑方获胜":"握手言和");
 centered(width/2,y+58,14,INK,reason==1?"将死 · 棋局已定":reason==2?"困毙 · 无棋可走":reason==3?"三次重复局面":"六十回合未进展");
 button(x+12,y+94,w-24,32,"再来一局",1);
 button(x+12,y+134,(w-28)/2,30,"复盘棋局",0);button(x+16+(w-28)/2,y+134,(w-28)/2,30,"返回桌面",0);
 centered(width/2,y+176,12,MUTED,"胜负一时，棋趣长存");
}
static int render(void){
 int frame_result=aster_ge_begin_frame(&ge);
 if(frame_result<0&&frame_result!=ASTER_ENOTSUP)return frame_result;
 int transactional=frame_result==0;ge.ring_n=transactional?128:256;
 if(frame_mode!=transactional){aster_printf("Xiangqi frame transactions=%d",transactional);frame_mode=transactional;}
 uint32_t calls=ge.submit_calls;
 aster_ge_clear(&ge,PAPER);
 /* Restrained paper fibres; vector geometry stays sharp at each panel size. */
 for(int y=5;y<height;y+=13)line(3,y,width-4,y,ASTER_RGB565(239,225,195));
 centered(width/2,small?2:8,small?20:28,RED,"楚 河");
 char status[96];
 if(result)aster_snprintf(status,sizeof(status),"%s%s",result==1?"红方获胜":result==2?"黑方获胜":"和棋",reason==2?" · 困毙":reason==3?" · 三次重复":reason==4?" · 六十回合":"");
 else if(thinking)aster_snprintf(status,sizeof(status),"%s思考 · %s",board()->side>0?"红方":"黑方",levels[game.level]);
 else aster_snprintf(status,sizeof(status),"%s行棋%s",board()->side>0?"红方":"黑方",xq_check(board(),board()->side)?" · 将军":"");
 centered(width/2,small?29:46,small?12:16,INK,status);
 aster_ge_rect(&ge,bx-radius-6,by-radius-5,8*cell+2*radius+12,9*cell+2*radius+10,GOLD);
 aster_ge_rect(&ge,bx-radius-4,by-radius-3,8*cell+2*radius+8,9*cell+2*radius+6,ASTER_RGB565(234,213,175));
 for(int r=0;r<10;r++)line(bx,by+r*cell,bx+8*cell,by+r*cell,INK);
 for(int c=0;c<9;c++){int x=bx+c*cell;line(x,by,x,by+4*cell,INK);line(x,by+5*cell,x,by+9*cell,INK);if(c==0||c==8)line(x,by+4*cell,x,by+5*cell,INK);}
 for(int end=0;end<2;end++){int y=by+end*7*cell;line(bx+3*cell,y,bx+5*cell,y+2*cell,INK);line(bx+5*cell,y,bx+3*cell,y+2*cell,INK);}
 centered(bx+2*cell,by+4*cell+(cell-aster_text_line_h(small?14:20))/2,small?14:20,MUTED,"楚 河");
 centered(bx+6*cell,by+4*cell+(cell-aster_text_line_h(small?14:20))/2,small?14:20,MUTED,"汉 界");
 if(game.count){xq_move last=game.moves[game.count-1];int x,y;point(last.from,&x,&y);aster_ge_ellipse(&ge,x,y,radius+2,radius+2,JADE,1);point(last.to,&x,&y);aster_ge_ellipse(&ge,x,y,radius+2,radius+2,JADE,1);}
 for(int i=0;i<90;i++)if(board()->sq[i])draw_piece(i,board()->sq[i]);
 for(int i=0;i<90;i++)if(destinations[i]){int x,y;point(i,&x,&y);if(board()->sq[i])aster_ge_ellipse(&ge,x,y,radius+2,radius+2,JADE,1);else disc(x,y,small?2:3,JADE);}
 int gap=small?4:8,bw=(width-4*gap)/3,y=height-(small?34:44);button(gap,y,bw,small?30:36,"悔棋",0);button(2*gap+bw,y,bw,small?30:36,"设置",0);button(3*gap+2*bw,y,bw,small?30:36,"退出",0);
 if(!small){text(12,height/2-30,14,MUTED,"观棋");text(12,height/2-7,14,MUTED,"不语");text(width-42,height/2-30,14,RED,"落子");text(width-42,height/2-7,14,RED,"有声");}
 if(*notice)centered(width/2,by+4*cell,12,RED,notice);
 if(menu)draw_settings();else if(end_dialog)draw_end();
 if(!transactional&&ge.submit_calls!=calls)aster_printf("Xiangqi unexpected frame spill");
 return aster_ge_present(&ge);
}
static int hit(int x,int y){
 if(end_dialog&&!menu){int w=small?220:340,rx=(width-w)/2,ry=(height-208)/2;
  if(inside(x,y,rx+12,ry+94,w-24,32))return 230;
  if(inside(x,y,rx+12,ry+134,(w-28)/2,30))return 231;
  if(inside(x,y,rx+16+(w-28)/2,ry+134,(w-28)/2,30))return 232;
  return -1;
 }
 if(menu){int px=dialog_x+12,w=dialog_w-24;
  if(inside(x,y,px,dialog_y+48,w,32))return 201;
  if(inside(x,y,px,dialog_y+88,w,32))return 202;
  int seg=(w-8)/3;for(int i=0;i<3;i++)if(inside(x,y,px+i*(seg+4),dialog_y+128,seg,32))return 210+i;
  if(inside(x,y,px,dialog_y+174,w,34))return 220;
  if(inside(x,y,px,dialog_y+218,w,34))return 221;return -1;
 }
 int gap=small?4:8,bw=(width-4*gap)/3,fy=height-(small?34:44);for(int i=0;i<3;i++)if(inside(x,y,gap+i*(gap+bw),fy,bw,small?30:36))return 100+i;
 int col=(x-bx+cell/2)/cell,row=(y-by+cell/2)/cell;
 if(x<bx-cell/2||y<by-cell/2||col<0||col>8||row<0||row>9)return -1;
 int sq=row*9+col;return flipped()?89-sq:sq;
}
static void tap(int id){
 dirty=1;
 if(id==230){new_game();return;}if(id==231){end_dialog=0;return;}if(id==232){persist();aster_app_exit(0);return;}
 if(id==100){undo();sound_event(0,52000,45000);return;}if(id==101){menu=1;thinking=0;clear_selection();return;}if(id==102){persist();aster_app_exit(0);return;}
 if(id==201){game.mode=!game.mode;return;}if(id==202){game.human=-game.human;return;}
 if(id>=210&&id<213){game.level=(uint8_t)(id-210);return;}
 if(id==220){new_game();menu=0;return;}if(id==221){menu=0;clear_selection();persist();return;}
 if(id<0||id>=90||menu||result||thinking||(!game.mode&&board()->side!=game.human))return;
 if(selected>=0&&destinations[id]){play((xq_move){(uint8_t)selected,(uint8_t)id});return;}
 clear_selection();if(board()->sq[id]*board()->side>0){selected=id;sound_event(0,90000,32000);int n=xq_legal(board(),moves);for(int i=0;i<n;i++)if(moves[i].from==id)destinations[moves[i].to]=1;}
}
static void release(void){if(surface!=ASTER_HANDLE_INVALID){persist();aster_surface_destroy(surface);surface=ASTER_HANDLE_INVALID;}for(int i=0;i<4;i++)if(sounds[i]>0){aster_audio_effect_unload((uint32_t)sounds[i]);sounds[i]=0;}thinking=0;}
ASTER_APP_EXPORT int aster_app_init(void){
 aster_display_info_t info={.size=sizeof(info),.version=1};int r=aster_display_get_info(0,&info,sizeof(info));if(r<0)return r;
 width=info.app_width;height=info.app_height;if(width<240||height<300)return ASTER_ENOTSUP;
 small=width<320;int header=small?48:68,footer=small?38:52;cell=(height-header-footer)/10;if(cell>(width-24)/9)cell=(width-24)/9;
 radius=cell/2-1;bx=(width-8*cell)/2;by=header+cell/2;
 dialog_w=small?220:352;dialog_x=(width-dialog_w)/2;dialog_y=(height-278)/2;
 aster_surface_desc_t desc={.size=sizeof(desc),.version=1,.width=width,.height=height,.format=ASTER_PIXEL_RGB565};r=aster_surface_create(&desc,sizeof(desc),&surface);if(r<0)return r;
 aster_ge_init(&ge,surface,width,height,ring,256,arena,sizeof(arena));load();adjudicate();menu=!game.magic;
 end_dialog=result!=0;
 if(aster_system_has_feature(ASTER_FEATURE_AUDIO_EFFECTS)){
  const char *paths[]={"/assets/audio/move.wav","/assets/audio/capture.wav","/assets/audio/check.wav","/assets/audio/end.wav"};
  for(int i=0;i<4;i++){sounds[i]=aster_audio_effect_load(paths[i],(uint32_t)strlen(paths[i]));aster_printf("Xiangqi sound %d handle=%d",i,sounds[i]);}
  aster_printf("Xiangqi volume=%d",aster_audio_get_volume());
 }
 r=aster_app_set_framerate(30);if(r<0){release();return r;}aster_printf("Xiangqi %dx%d cell=%d loaded=%d",width,height,cell,game.magic!=0);dirty=1;return 0;
}
ASTER_APP_EXPORT void aster_app_event(const aster_event_t *e){
 if(!e||surface==ASTER_HANDLE_INVALID)return;
 if(e->type==ASTER_EVENT_TOUCH){int action=e->u.touch.action,x=e->u.touch.x,y=e->u.touch.y;
  if(e->u.touch.slot){if(action==ASTER_TOUCH_DOWN)pressed=-1;return;}
  if(action==ASTER_TOUCH_DOWN){pressed=hit(x,y);down_x=x;down_y=y;}
  else if(action==ASTER_TOUCH_MOVE){if(abs_i(x-down_x)>cell/2||abs_i(y-down_y)>cell/2)pressed=-1;}
  else if(action==ASTER_TOUCH_UP){int id=hit(x,y),old=pressed;pressed=-1;if(id>=0&&id==old)tap(id);}
 }else if(e->type==ASTER_EVENT_FRAME&&!paused){
  uint32_t now=aster_time_now_ms();
  if(retry_cue>=0&&(int32_t)(now-retry_at)>=0){int cue=retry_cue;retry_cue=-1;sound_cue(cue,retry_pitch,retry_gain);}
  if(!menu&&!result&&!game.mode&&board()->side!=game.human){
   if(!thinking){xq_search_begin(&search,board(),game.level);thinking=1;ai_started=ai_draw=now;dirty=1;}
   uint32_t slice=now;unsigned limit=0;while(!search.done&&limit++<2048&&(uint32_t)(aster_time_now_ms()-slice)<(uint32_t)(small?16:24))xq_search_step(&search,1);
   uint32_t allowance=game.level==0?400:game.level==1?1500:3000;
   if(search.done||(uint32_t)(now-ai_started)>=allowance){aster_printf("Xiangqi AI depth=%d nodes=%u ms=%u",search.completed,search.nodes,(unsigned)(now-ai_started));if(search.best.from<90)play(search.best);else{thinking=0;adjudicate();dirty=1;}}

  }
  if(aster_gfx_fonts_pending())dirty=1;
  if(dirty){int r=render();if(r<0){aster_printf("Xiangqi render error %d",r);aster_app_exit(r);}dirty=0;}
 }else if(e->type==ASTER_EVENT_KEY&&e->u.key.pressed&&e->u.key.code==1){menu=!menu;thinking=0;clear_selection();dirty=1;}
}
ASTER_APP_EXPORT void aster_app_pause(void){paused=1;thinking=0;retry_cue=-1;clear_selection();persist();}
ASTER_APP_EXPORT void aster_app_resume(void){paused=0;thinking=0;clear_selection();dirty=1;}
ASTER_APP_EXPORT void aster_app_stop(void){release();}
ASTER_APP_EXPORT void aster_app_destroy(void){release();}





