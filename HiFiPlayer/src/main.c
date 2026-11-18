/* SPDX-License-Identifier: MIT */
#include <aster_imports.h>
#include <aster_gfx_engine.h>
static aster_handle_t surface,dir;
static aster_ge_t ge;
static aster_gfx_cmd_t commands[160];
static uint8_t arena[4096];
static aster_player_info_t info;
static uint32_t bluetooth_rate;

static uint32_t width,height,last_poll;
static uint32_t mode=ASTER_PLAYER_LOCAL;
static int dirty=1,library,page,files,scanning,pressed;
static char names[64][160],message[128],pending_file[256],current_file[256];
static uint32_t message_until;
static int seeking;
static uint32_t seek_preview_ms,seek_deadline;
static int volume=40,touch_start_x,touch_start_y;
enum { VOLUME_TOP=60, VOLUME_HEIGHT=44, VOLUME_X=94, VOLUME_WIDTH=254 };
static const uint16_t bg=0x0863,panel=0x10e5,muted=0x9513,white=0xef7c,accent=0x96d5;
static int sx(int x){return x*(int)width/460;}
static int sy(int y){return y*(int)height/460;}
static void rect(int x,int y,int w,int h,uint16_t color,int radius){aster_ge_rect_r(&ge,sx(x),sy(y),sx(w),sy(h),color,sx(radius),0);}
static void text(int x,int y,int px,uint16_t color,const char *value,int maxw){
 char out[224];int size=aster_text_for_display(px,width,height);
 aster_text_elide(size,value,strlen(value),sx(maxw),out,sizeof(out));
 aster_ge_text(&ge,sx(x),sy(y)+aster_text_asc(size),size,color,ASTER_GFX_NOBG,out);
}
static void center(int x,int y,int w,int px,uint16_t color,const char *value){
 char out[224];
 int size=aster_text_for_display(px,width,height);
 aster_text_elide(size,value,strlen(value),sx(w),out,sizeof(out));
 int tw=aster_gfx_text_w(size,out,strlen(out));
 aster_ge_text(&ge,sx(x)+(sx(w)-tw)/2,sy(y)+aster_text_asc(size),size,color,ASTER_GFX_NOBG,out);
}
static void notice(const char *value){aster_snprintf(message,sizeof(message),"%s",value);message_until=aster_time_now_ms()+4000;dirty=1;}
static int request(uint32_t command,uint32_t value,const char *path){
 aster_player_request_t r={.size=sizeof(r),.version=1,.command=command,.value=value};
 if(path)aster_snprintf(r.path,sizeof(r.path),"%s",path);
 int result=aster_player_control(&r,sizeof(r));
 if(result<0){
  if(result==ASTER_EBUSY)notice("音频正忙，请稍后再试");
  else if(result==ASTER_ESTATE)notice(mode==ASTER_PLAYER_BLUETOOTH?"请先连接手机并打开音乐应用":"蓝牙音频输出不可用，请检查系统设置");
  else if(result==ASTER_ENOTSUP)notice("当前设备不支持此功能");
  else {aster_snprintf(message,sizeof(message),"操作失败 %d",result);message_until=aster_time_now_ms()+4000;dirty=1;}
 }
 return result;
}
static void scan_close(void){if(dir){aster_fs_closedir(dir);dir=0;}scanning=0;}
static void scan_start(void){
 scan_close();files=page=0;
 int r=aster_fs_opendir("/music",6,&dir);
 if(r<0){notice("将 WAV / FLAC 文件放入 SD 的 music 目录");dir=0;return;}
 scanning=1;
}
static int extension(const char *name){
 int n=(int)strlen(name);if(n<4)return 0;
 const char *p=name+n-4;
 if(p[0]=='.' && (p[1]=='w'||p[1]=='W') && (p[2]=='a'||p[2]=='A') && (p[3]=='v'||p[3]=='V'))return 1;
 if(n<5)return 0;
 p=name+n-5;
 return p[0]=='.' && (p[1]=='f'||p[1]=='F') && (p[2]=='l'||p[2]=='L') && (p[3]=='a'||p[3]=='A') && (p[4]=='c'||p[4]=='C');
}
static void scan_tick(void){
 for(int i=0;i<2 && scanning;i++){
  char name[256];uint32_t isdir=0;
  int r=aster_fs_dir_next(dir,name,sizeof(name),&isdir);
  if(r<=0){scan_close();if(r<0)notice("读取音乐目录失败");dirty=1;break;}
  if(!isdir && extension(name) && strlen(name)<sizeof(names[0])){
   aster_snprintf(names[files++],sizeof(names[0]),"%s",name);dirty=1;
   if(files==64){scan_close();notice("本页曲库最多显示 64 首");}
  }
 }
}
static void button(int id,int x,int y,int w,int h,const char *label,int selected){
 rect(x,y,w,h,pressed==id?0x2948:selected?accent:panel,12);
 center(x,y+(h-24)/2,w,16,selected?bg:white,label);
}
static const char *state_text(void){
 if(info.state==ASTER_PLAYER_ERROR)return "播放发生错误";
 if(info.state==ASTER_PLAYER_STOPPING)return "正在切换音频模式...";
 if(mode==ASTER_PLAYER_BLUETOOTH){
  if(info.mode!=ASTER_PLAYER_BLUETOOTH)return "蓝牙接收未开启";
  if(!(info.flags&ASTER_PLAYER_F_CONNECTED))return "等待手机连接 Aster-S31 Audio";
  return info.state==ASTER_PLAYER_PLAYING?"手机播放 · SBC":"手机已连接 · 已暂停";
 }
 return info.state==ASTER_PLAYER_PLAYING?((info.flags&ASTER_PLAYER_F_BT_OUTPUT)?"本地播放 · 蓝牙音箱输出":"本地播放 · 原采样率输出"):info.state==ASTER_PLAYER_PAUSED?"本地播放 · 已暂停":
  info.state==ASTER_PLAYER_OPENING?"正在打开音乐...":info.state==ASTER_PLAYER_ENDED?"播放结束":"选择一首音乐开始";
}
static void draw(void){
 char line[160];aster_ge_clear(&ge,bg);
 button(1,18,14,64,42,"返回",0);
 center(88,22,284,18,white,"Aster 音乐");
 if(mode==ASTER_PLAYER_LOCAL)button(2,372,14,70,42,library?"播放":"曲库",0);
 rect(18,VOLUME_TOP,424,VOLUME_HEIGHT,panel,12);
 text(30,72,14,muted,"音量",60);
 rect(VOLUME_X,80,VOLUME_WIDTH,4,0x2948,2);
 int fill=volume*VOLUME_WIDTH/100;
 if(fill>0)rect(VOLUME_X,80,fill,4,accent,2);
 rect(VOLUME_X-7+fill,74,14,16,accent,7);
 aster_snprintf(line,sizeof(line),"%u%%",volume);
 text(369,71,16,white,line,61);
 if(library && mode==ASTER_PLAYER_LOCAL){
  text(22,110,14,muted,"SD / music · WAV / FLAC",310);button(5,338,104,106,36,"试听示例",0);
  for(int i=0;i<4;i++){
   int index=page*4+i;if(index>=files)break;
   rect(16,150+i*51,428,44,pressed==20+i?0x2948:panel,8);
   text(30,159+i*51,16,white,names[index],400);
  }
  if(!files)center(20,230,420,16,muted,scanning?"正在读取音乐目录...":"曲库暂无音乐，可先试听示例");
  button(6,16,374,104,42,"上一页",0);button(7,340,374,104,42,"下一页",0);
  aster_snprintf(line,sizeof(line),"%u / %u · %u 首",page+1,(files+3)/4?(files+3)/4:1,files);center(120,385,220,14,muted,line);
 } else {
  text(24,112,14,muted,state_text(),412);
  const char *title=info.title;
  if(mode==ASTER_PLAYER_LOCAL){
   title=info.path;
   for(const char *p=title;*p;p++)if(*p=='/')title=p+1;
   if(!*title)title="未选择曲目";
  } else if(!*title)title="等待手机提供曲目信息";
  text(24,134,22,white,title,412);
  const char *artist=info.artist[0]?info.artist:info.album;
  if(*artist)text(24,164,15,muted,artist,412);
  rect(18,190,424,108,panel,15);
  center(24,232,412,16,muted,"暂无歌词");
  if(mode==ASTER_PLAYER_LOCAL && info.duration_ms){
   uint32_t position=seeking?seek_preview_ms:info.position_ms;
   rect(24,324,412,4,panel,2);int n=(int)((uint64_t)position*412/info.duration_ms);if(n>412)n=412;if(n>0)rect(24,324,n,4,accent,2);
   rect(17+n,318,14,18,accent,7);
   if(seeking){
    rect(102,292,256,26,panel,9);aster_snprintf(line,sizeof(line),"跳转 %u 秒 / %u 秒",position/1000,info.duration_ms/1000);center(102,297,256,14,white,line);
   }
  }
  button(8,40,358,100,56,"上一首",0);
  button(9,160,352,140,64,info.state==ASTER_PLAYER_PLAYING?"暂停":"播放",1);
  button(10,320,358,100,56,"下一首",0);
  if(mode==ASTER_PLAYER_LOCAL){
   if((info.flags&ASTER_PLAYER_F_BT_OUTPUT))aster_snprintf(line,sizeof(line),"源 %u Hz / %u bit · SBC %u Hz / 16 bit",info.rate,info.bits,bluetooth_rate);
   else aster_snprintf(line,sizeof(line),"%u 秒 / %u 秒 · %u Hz / %u bit",info.position_ms/1000,info.duration_ms/1000,info.rate,info.bits);
   center(16,431,428,14,muted,line);
  } else center(16,431,428,14,muted,"退出界面后蓝牙音乐仍可继续播放");
 }
 if(message[0]){rect(12,365,436,45,0x2948,10);center(16,378,428,14,white,message);}
 int result=aster_ge_present(&ge);if(result<0)aster_errfmt("player render %d",result);
}
static int hit(int x,int y){
 if(y>=14 && y<56)return x>=18 && x<82?1:mode==ASTER_PLAYER_LOCAL && x>=372 && x<442?2:0;
 if(y>=VOLUME_TOP && y<VOLUME_TOP+VOLUME_HEIGHT)
  return x>=VOLUME_X-16 && x<=VOLUME_X+VOLUME_WIDTH+16?11:0;
 if(library && mode==ASTER_PLAYER_LOCAL){
  if(y>=104 && y<142 && x>=330)return 5;
  if(y>=150 && y<354)return 20+(y-150)/51;
  if(y>=374 && y<416)return x<125?6:x>=335?7:0;
 } else {
  if(mode==ASTER_PLAYER_LOCAL && info.duration_ms && y>=302 && y<346 && x>=10 && x<=450)return 12;
  if(y>=352 && y<416){
   if(x>=160 && x<300)return 9;
   if(y>=358 && y<414 && x>=40 && x<140)return 8;
   if(y>=358 && y<414 && x>=320 && x<420)return 10;
  }
 }
 return 0;
}
static uint32_t playback_output(void)
{
 aster_player_bt_list_t list={.size=sizeof(list),.version=1};
 if(!aster_player_bt_list(&list,sizeof(list)) &&
    list.state==ASTER_PLAYER_BT_CONNECTED && list.output_rate)
  return ASTER_PLAYER_OUTPUT_BLUETOOTH;
 return ASTER_PLAYER_OUTPUT_WIRED;
}

static void queue_file(const char *path){
 seeking=0;
 if(info.mode==ASTER_PLAYER_BLUETOOTH){notice("请先在系统蓝牙设置中断开手机");return;}
 if(path!=current_file)aster_snprintf(current_file,sizeof(current_file),"%s",path);
 if(info.state==ASTER_PLAYER_PLAYING || info.state==ASTER_PLAYER_PAUSED || info.state==ASTER_PLAYER_OPENING || info.state==ASTER_PLAYER_STOPPING){
  aster_snprintf(pending_file,sizeof(pending_file),"%s",path);request(ASTER_PLAYER_STOP,0,0);
 } else request(ASTER_PLAYER_PLAY_FILE,playback_output(),path);
 library=0;dirty=1;
}
static void play_index(int index){
 if(index<0 || index>=files)return;
 char path[256];aster_snprintf(path,sizeof(path),"/music/%s",names[index]);
 queue_file(path);
}
static void activate(int id){
 seeking=0;
 if(id==1){if(library){library=0;scan_close();dirty=1;}else aster_app_exit(0);return;}
 if(id==2){library=!library;if(library)scan_start();else scan_close();}
 else if(id==5){queue_file("/assets/audio/demo.flac");}
 else if(id==6 && page>0)page--;
 else if(id==7 && (page+1)*4<files)page++;
 else if(id>=20 && id<24)play_index(page*4+id-20);
 else if(id==9){
  if(mode==ASTER_PLAYER_BLUETOOTH && !(info.flags&ASTER_PLAYER_F_CONNECTED))notice("请先连接手机");
  else if(mode==ASTER_PLAYER_LOCAL && (info.state==ASTER_PLAYER_IDLE || info.state==ASTER_PLAYER_ENDED || info.state==ASTER_PLAYER_ERROR)){if(current_file[0])queue_file(current_file);else {library=1;scan_start();}}
  else request(info.state==ASTER_PLAYER_PLAYING?ASTER_PLAYER_PAUSE:ASTER_PLAYER_RESUME,0,0);
 } else if(id==8 || id==10){
  if(mode==ASTER_PLAYER_BLUETOOTH){if(info.mode==ASTER_PLAYER_BLUETOOTH)request(id==8?ASTER_PLAYER_PREVIOUS:ASTER_PLAYER_NEXT,0,0);else notice("请先开启蓝牙接收");}
  else {
   int found=-1;for(int i=0;i<files;i++){const char *p=info.path,*base=p;while(*p){if(*p=='/')base=p+1;p++;}if(!strcmp(base,names[i]))found=i;}
   if(files)play_index(found<0?0:(found+(id==8?files-1:1))%files);else {library=1;scan_start();}
  }
 }
 dirty=1;
}
static void drag(int id,int x){
 if(id==11){int value=(x-VOLUME_X)*100/VOLUME_WIDTH;if(value<0)value=0;if(value>100)value=100;
  if(value!=volume){int r=aster_audio_set_volume(value);if(r>=0)volume=value;dirty=1;}
 } else if(id==12 && info.duration_ms){
  int at=x-24;if(at<0)at=0;if(at>412)at=412;
  seek_preview_ms=(uint32_t)((uint64_t)info.duration_ms*at/412);seeking=1;dirty=1;
 }

}
ASTER_APP_EXPORT int aster_app_init(void){
 aster_display_info_t display={.size=sizeof(display),.version=1};int r=aster_display_get_info(0,&display,sizeof(display));if(r<0)return r;
 width=display.app_width;height=display.app_height;if(width<300 || height<300)return ASTER_ENOTSUP;
 info=(aster_player_info_t){.size=sizeof(info),.version=1};r=aster_player_get_info(&info,sizeof(info));if(r<0)return r;
 mode=info.mode;volume=aster_audio_get_volume();if(volume<0)volume=40;
 aster_surface_desc_t desc={.size=sizeof(desc),.version=1,.width=width,.height=height,.format=ASTER_PIXEL_RGB565};
 r=aster_surface_create(&desc,sizeof(desc),&surface);if(r<0)return r;
 aster_ge_init(&ge,surface,width,height,commands,160,arena,sizeof(arena));
 aster_app_set_framerate(20);dirty=1;aster_printf("HiFi init %ux%u caps=%u",width,height,info.capabilities);return 0;
}
ASTER_APP_EXPORT void aster_app_event(const aster_event_t *event){
 if(!event || !surface)return;
 if(event->type==ASTER_EVENT_FRAME){
  uint32_t now=aster_time_now_ms();if(aster_gfx_fonts_pending())dirty=1;scan_tick();
  if(now-last_poll>=300){
   last_poll=now;aster_player_info_t next={.size=sizeof(next),.version=1};
   if(!aster_player_get_info(&next,sizeof(next)) && memcmp(&next,&info,sizeof(info))){
    info=next;dirty=1;
    if(mode!=info.mode){
     mode=info.mode;library=0;scan_close();pending_file[0]=0;pressed=0;seeking=0;
    }
   }
   aster_player_bt_list_t route={.size=sizeof(route),.version=1};
   if(!aster_player_bt_list(&route,sizeof(route)) && route.output_rate!=bluetooth_rate){
    bluetooth_rate=route.output_rate;dirty=1;
   }
   if(seeking==2){
    uint32_t delta=info.position_ms>seek_preview_ms?info.position_ms-seek_preview_ms:seek_preview_ms-info.position_ms;
    if(delta<1000 || (int32_t)(now-seek_deadline)>=0 || info.state==ASTER_PLAYER_ERROR){seeking=0;dirty=1;}
   }
   int value=aster_audio_get_volume();if(value>=0 && value!=volume){volume=value;dirty=1;}
   if(pending_file[0] && info.mode==ASTER_PLAYER_LOCAL && (info.state==ASTER_PLAYER_IDLE || info.state==ASTER_PLAYER_ENDED || info.state==ASTER_PLAYER_ERROR)){
    request(ASTER_PLAYER_PLAY_FILE,playback_output(),pending_file);
    pending_file[0]=0;
   }
  }
  if(message[0] && (int32_t)(now-message_until)>=0){message[0]=0;dirty=1;}
  if(dirty){draw();dirty=0;}
 } else if(event->type==ASTER_EVENT_TOUCH && event->u.touch.slot==0){
  int x=event->u.touch.x*460/(int)width,y=event->u.touch.y*460/(int)height;
  if(event->u.touch.action==ASTER_TOUCH_DOWN){pressed=hit(x,y);touch_start_x=x;touch_start_y=y;if(pressed==11 || pressed==12)drag(pressed,x);dirty=1;}
  else if(event->u.touch.action==ASTER_TOUCH_MOVE){
   if(pressed==11 || pressed==12)drag(pressed,x);
   else if(pressed){int dx=x-touch_start_x,dy=y-touch_start_y;if(dx*dx+dy*dy>144){pressed=0;dirty=1;}}
  }
  else if(event->u.touch.action==ASTER_TOUCH_UP){int id=pressed;pressed=0;if(id==12){
    drag(id,x);
    if(!request(ASTER_PLAYER_SEEK,seek_preview_ms,0)){seeking=2;seek_deadline=aster_time_now_ms()+2000;}else seeking=0;
   }else if(id && id==hit(x,y))activate(id);dirty=1;}
 } else if(event->type==ASTER_EVENT_KEY && event->u.key.pressed && event->u.key.code==1)aster_app_exit(0);
}
ASTER_APP_EXPORT void aster_app_pause(void){pressed=0;seeking=0;}
ASTER_APP_EXPORT void aster_app_resume(void){pressed=0;seeking=0;dirty=1;last_poll=0;}
static void cleanup(void){scan_close();if(surface){aster_surface_destroy(surface);surface=0;}}
ASTER_APP_EXPORT void aster_app_stop(void){cleanup();}
ASTER_APP_EXPORT void aster_app_destroy(void){cleanup();}
