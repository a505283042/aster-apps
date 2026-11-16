/* SPDX-License-Identifier: MIT */
#include <aster_imports.h>
#include <aster_gfx_engine.h>
static aster_handle_t surface,dir;
static aster_ge_t ge;
static aster_gfx_cmd_t commands[160];
static uint8_t arena[4096];
static aster_player_info_t info;
static aster_player_bt_list_t devices;
static int output=ASTER_PLAYER_OUTPUT_WIRED,pending_connect;
static uint32_t width,height,last_poll;
static int dirty=1,mode=ASTER_PLAYER_LOCAL,library,page,files,scanning,pressed,pending_mode;
static char names[64][160],message[128],pending_file[256],current_file[256];
static uint32_t message_until;
static int seeking;
static uint32_t seek_preview_ms,seek_deadline;
static int volume=40,touch_start_x,touch_start_y;
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
 int size=aster_text_for_display(px,width,height);
 int tw=aster_gfx_text_w(size,value,strlen(value));
 aster_ge_text(&ge,sx(x)+(sx(w)-tw)/2,sy(y)+aster_text_asc(size),size,color,ASTER_GFX_NOBG,value);
}
static void notice(const char *value){aster_snprintf(message,sizeof(message),"%s",value);message_until=aster_time_now_ms()+4000;dirty=1;}
static int request(uint32_t command,uint32_t value,const char *path){
 aster_player_request_t r={.size=sizeof(r),.version=1,.command=command,.value=value};
 if(path)aster_snprintf(r.path,sizeof(r.path),"%s",path);
 int result=aster_player_control(&r,sizeof(r));
 if(result<0){
  if(result==ASTER_EBUSY)notice("音频正忙，请稍后再试");
  else if(result==ASTER_ESTATE)notice(mode==ASTER_PLAYER_BLUETOOTH?"请先连接手机并打开音乐应用":"请先连接蓝牙耳机或音箱");
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
 if(info.state==ASTER_PLAYER_STOPPING || pending_mode)return "正在切换音频模式...";
 if(mode==ASTER_PLAYER_BLUETOOTH){
  if(info.mode!=ASTER_PLAYER_BLUETOOTH)return "蓝牙接收未开启";
  if(!(info.flags&ASTER_PLAYER_F_CONNECTED))return "等待手机连接 Aster-S31 Audio";
  return info.state==ASTER_PLAYER_PLAYING?"手机已连接 · SBC 蓝牙音频":"手机已连接 · 已暂停";
 }
 return info.state==ASTER_PLAYER_PLAYING?(output==ASTER_PLAYER_OUTPUT_BLUETOOTH?"本地播放 · 蓝牙音箱输出":"本地播放 · 原采样率输出"):info.state==ASTER_PLAYER_PAUSED?"本地播放 · 已暂停":
  info.state==ASTER_PLAYER_OPENING?"正在打开音乐...":info.state==ASTER_PLAYER_ENDED?"播放结束":"选择一首音乐开始";
}
static void draw(void){
 char line[160];aster_ge_clear(&ge,bg);
 button(1,16,12,56,36,"返回",0);text(86,16,20,white,"ASTER 音乐",165);
 button(13,260,12,66,36,"耳机",library==2);
 button(2,338,12,106,36,library==2?"搜索":mode==ASTER_PLAYER_BLUETOOTH?(info.mode==ASTER_PLAYER_BLUETOOTH?"关闭蓝牙":"开启蓝牙"):library?"播放页":"曲库",0);
 button(3,16,58,208,40,"本地音乐",mode==ASTER_PLAYER_LOCAL);
 button(4,236,58,208,40,"蓝牙接收",mode==ASTER_PLAYER_BLUETOOTH);
 if(library==2 && mode==ASTER_PLAYER_LOCAL){
  const char *status=devices.state==ASTER_PLAYER_BT_SCANNING?"正在搜索蓝牙音箱 / 耳机...":devices.state==ASTER_PLAYER_BT_CONNECTING?"正在连接，请稍候...":devices.state==ASTER_PLAYER_BT_CONNECTED?"已连接 · 本地音乐将通过蓝牙输出":devices.state==ASTER_PLAYER_BT_ERROR?"连接失败，请重新搜索":"选择音箱或耳机";
  text(22,111,14,muted,status,416);
  for(int i=0;i<4;i++){
   unsigned index=(unsigned)page*4+i;if(index>=devices.count)break;
   rect(16,148+i*53,428,48,pressed==30+i?0x2948:panel,8);
   text(28,151+i*53,16,devices.devices[index].flags?accent:white,devices.devices[index].name,400);
   text(28,176+i*53,12,muted,devices.devices[index].address,400);
  }
  if(!devices.count)center(20,228,420,16,muted,"请将音箱或耳机切到配对模式");
  button(6,16,374,90,42,"上页",0);button(14,120,374,220,42,"使用有线耳机",0);button(7,354,374,90,42,"下页",0);
 } else if(library==1 && mode==ASTER_PLAYER_LOCAL){
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
  center(16,108,428,14,muted,state_text());
  /* A small record motif; redraw only when state/metadata changes. */
  rect(174,140,112,58,panel,25);rect(197,153,66,32,0x2147,15);rect(218,160,24,18,accent,9);
  const char *title=info.title;
  if(mode==ASTER_PLAYER_LOCAL){
   title=info.path;const char *p=title;while(*p){if(*p=='/')title=p+1;p++;}
   if(!*title)title="让音乐留在此刻";
  } else if(!*title)title="等待手机提供曲目信息";
  text(24,211,20,white,title,412);
  text(24,244,16,muted,info.artist[0]?info.artist:mode==ASTER_PLAYER_LOCAL?"WAV / FLAC · 有线耳机":"手机音乐 → 有线耳机",412);
  text(24,270,14,muted,info.album,412);
  if(mode==ASTER_PLAYER_LOCAL && info.duration_ms){
   uint32_t position=seeking?seek_preview_ms:info.position_ms;
   rect(24,291,412,4,panel,2);int n=(int)((uint64_t)position*412/info.duration_ms);if(n>412)n=412;if(n>0)rect(24,291,n,4,accent,2);
   rect(17+n,285,14,18,accent,7);
   if(seeking){
    rect(102,258,256,26,panel,9);aster_snprintf(line,sizeof(line),"跳转 %u 秒 / %u 秒",position/1000,info.duration_ms/1000);center(102,263,256,14,white,line);
   }
  }
  button(8,40,340,100,56,"上一首",0);
  button(9,160,334,140,64,info.state==ASTER_PLAYER_PLAYING?"暂停":"播放",1);
  button(10,320,340,100,56,"下一首",0);
  if(mode==ASTER_PLAYER_LOCAL){
   if(output==ASTER_PLAYER_OUTPUT_BLUETOOTH)aster_snprintf(line,sizeof(line),"源 %u Hz / %u bit · SBC %u Hz / 16 bit",info.rate,info.bits,devices.output_rate);
   else aster_snprintf(line,sizeof(line),"%u 秒 / %u 秒 · %u Hz / %u bit",info.position_ms/1000,info.duration_ms/1000,info.rate,info.bits);
   center(16,402,428,14,muted,line);
  } else center(16,402,428,14,muted,"退出界面后蓝牙音乐仍可继续播放");
 }
 text(20,427,14,muted,"音量",70);rect(94,436,254,4,panel,2);if(volume>0)rect(94,436,volume*254/100,4,accent,2);
 rect(89+volume*254/100,430,14,16,accent,7);aster_snprintf(line,sizeof(line),"%u%%",volume);text(370,426,16,white,line,70);
 if(message[0]){rect(12,365,436,45,0x2948,10);center(16,378,428,14,white,message);}
 int result=aster_ge_present(&ge);if(result<0)aster_errfmt("player render %d",result);
}
static int hit(int x,int y){
 if(y>=12 && y<50)return x<80?1:x>=330?2:x>=252?13:0;
 if(y>=58 && y<98)return x<230?3:4;
 if(y>=418)return 11;
 if(library==2 && mode==ASTER_PLAYER_LOCAL){
  if(y>=148 && y<360)return 30+(y-148)/53;
  if(y>=374 && y<416)return x<110?6:x>=350?7:14;
 } else if(library==1 && mode==ASTER_PLAYER_LOCAL){
  if(y>=104 && y<142 && x>=330)return 5;
  if(y>=150 && y<354)return 20+(y-150)/51;
  if(y>=374 && y<416)return x<125?6:x>=335?7:0;
 } else {
  if(mode==ASTER_PLAYER_LOCAL && info.duration_ms && y>=272 && y<316)return 12;
  if(y>=334 && y<398)return x<150?8:x<310?9:10;
 }
 return 0;
}
static void queue_file(const char *path){
 seeking=0;
 if(info.mode==ASTER_PLAYER_BLUETOOTH){notice("请先切换到本地音乐");return;}
 if(path!=current_file)aster_snprintf(current_file,sizeof(current_file),"%s",path);
 if(info.state==ASTER_PLAYER_PLAYING || info.state==ASTER_PLAYER_PAUSED || info.state==ASTER_PLAYER_OPENING || info.state==ASTER_PLAYER_STOPPING){
  aster_snprintf(pending_file,sizeof(pending_file),"%s",path);request(ASTER_PLAYER_STOP,0,0);
 } else request(ASTER_PLAYER_PLAY_FILE,output,path);
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
 if(id==13){
  if(!(info.capabilities&ASTER_PLAYER_CAP_BT_SOURCE)){notice("当前设备不支持蓝牙耳机输出");return;}
  scan_close();page=0;library=2;
  if(devices.state!=ASTER_PLAYER_BT_CONNECTED && devices.state!=ASTER_PLAYER_BT_SCANNING && devices.state!=ASTER_PLAYER_BT_CONNECTING){
   pending_file[0]=0;pending_mode=3;
   if(info.mode==ASTER_PLAYER_BLUETOOTH)request(ASTER_PLAYER_BT_OFF,0,0);else request(ASTER_PLAYER_STOP,0,0);
  }
  mode=ASTER_PLAYER_LOCAL;dirty=1;return;
 }
 if(id==14){pending_file[0]=0;pending_mode=ASTER_PLAYER_LOCAL;output=ASTER_PLAYER_OUTPUT_WIRED;library=0;request(ASTER_PLAYER_BT_OFF,0,0);dirty=1;return;}
 if(id==3 || id==4){
  int selected=id==3?ASTER_PLAYER_LOCAL:ASTER_PLAYER_BLUETOOTH;if(selected==mode)return;
  scan_close();library=0;pending_file[0]=0;pending_mode=selected;
  if(info.mode==ASTER_PLAYER_BLUETOOTH || devices.state==ASTER_PLAYER_BT_CONNECTED || devices.state==ASTER_PLAYER_BT_SCANNING || devices.state==ASTER_PLAYER_BT_CONNECTING)request(ASTER_PLAYER_BT_OFF,0,0);else request(ASTER_PLAYER_STOP,0,0);
  dirty=1;return;
 }
 if(id==2){
  if(library==2){request(ASTER_PLAYER_BT_SCAN,0,0);page=0;}
  else if(mode==ASTER_PLAYER_LOCAL){library=!library;if(library)scan_start();else scan_close();}
  else if(!pending_mode)request(info.mode==ASTER_PLAYER_BLUETOOTH?ASTER_PLAYER_BT_OFF:ASTER_PLAYER_BT_ON,0,0);
 } else if(id==5){queue_file("/assets/audio/demo.flac");}
 else if(id==6 && page>0)page--;
 else if(id==7 && (page+1)*4<(library==2?(int)devices.count:files))page++;
 else if(id>=30 && id<34){
  unsigned index=(unsigned)page*4+id-30;
  if(index<devices.count){
   if(devices.devices[index].flags&ASTER_PLAYER_F_CONNECTED){output=ASTER_PLAYER_OUTPUT_BLUETOOTH;library=0;}
   else if(!request(ASTER_PLAYER_BT_CONNECT,0,devices.devices[index].address))pending_connect=1;
  }
 }
 else if(id>=20 && id<24)play_index(page*4+id-20);
 else if(id==9){
  if(mode==ASTER_PLAYER_BLUETOOTH && info.mode!=ASTER_PLAYER_BLUETOOTH)request(ASTER_PLAYER_BT_ON,0,0);
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
 if(id==11){int value=(x-94)*100/254;if(value<0)value=0;if(value>100)value=100;
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
 devices=(aster_player_bt_list_t){.size=sizeof(devices),.version=1};aster_player_bt_list(&devices,sizeof(devices));
 if(devices.state==ASTER_PLAYER_BT_CONNECTED)output=ASTER_PLAYER_OUTPUT_BLUETOOTH;
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
   if(!aster_player_get_info(&next,sizeof(next)) && memcmp(&next,&info,sizeof(info))){info=next;dirty=1;}
   aster_player_bt_list_t next_devices={.size=sizeof(next_devices),.version=1};
   if(!aster_player_bt_list(&next_devices,sizeof(next_devices)) && memcmp(&devices,&next_devices,sizeof(devices))){devices=next_devices;if(library==2)dirty=1;}
   if(pending_connect && devices.state==ASTER_PLAYER_BT_CONNECTED){pending_connect=0;output=ASTER_PLAYER_OUTPUT_BLUETOOTH;library=0;dirty=1;notice("已连接，本地音乐将发送到蓝牙设备");}
   if(seeking==2){
    uint32_t delta=info.position_ms>seek_preview_ms?info.position_ms-seek_preview_ms:seek_preview_ms-info.position_ms;
    if(delta<1000 || (int32_t)(now-seek_deadline)>=0 || info.state==ASTER_PLAYER_ERROR){seeking=0;dirty=1;}
   }
   int value=aster_audio_get_volume();if(value>=0 && value!=volume){volume=value;dirty=1;}
   if(pending_file[0] && info.mode==ASTER_PLAYER_LOCAL && (info.state==ASTER_PLAYER_IDLE || info.state==ASTER_PLAYER_ENDED || info.state==ASTER_PLAYER_ERROR)){
    if(!request(ASTER_PLAYER_PLAY_FILE,output,pending_file))pending_file[0]=0;
   }
   if(pending_mode && devices.state!=ASTER_PLAYER_BT_STOPPING && info.mode!=ASTER_PLAYER_BLUETOOTH && info.state!=ASTER_PLAYER_STOPPING && info.state!=ASTER_PLAYER_PLAYING && info.state!=ASTER_PLAYER_OPENING && info.state!=ASTER_PLAYER_PAUSED){
    int next_mode=pending_mode;mode=next_mode==3?ASTER_PLAYER_LOCAL:next_mode;pending_mode=0;
    if(next_mode==3){output=ASTER_PLAYER_OUTPUT_BLUETOOTH;library=2;request(ASTER_PLAYER_SOURCE_ON,0,0);}
    else if(mode==ASTER_PLAYER_BLUETOOTH){output=ASTER_PLAYER_OUTPUT_WIRED;request(ASTER_PLAYER_BT_ON,0,0);}
    else output=ASTER_PLAYER_OUTPUT_WIRED;dirty=1;
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
