/* SPDX-License-Identifier: GPL-2.0-only */
/* Private connector implementation. Included once by extra_decks.c, never by a renderer. */
#ifndef XZ_EXTRA_ROUTE_STATUS_PRIVATE_H
#define XZ_EXTRA_ROUTE_STATUS_PRIVATE_H
#include <fcntl.h>
#include <errno.h>
#include <limits.h>

struct route_reason {
 const char *code;
 enum xz_extra_route_state state;
 enum xz_extra_route_part part;
 const char *sentence;
};
#define RR(code,state,part,text) {code,XZ_EXTRA_ROUTE_##state,XZ_EXTRA_ROUTE_PART_##part,text}
static const struct route_reason route_reasons[] = {
 RR("WAIT_LOOPBACK",NOT_DETECTED,CABLE,"The loopback cable has not been detected."),
 RR("WAIT_NATIVE_PAUSED",WAITING_NATIVE_STOP,SELECTOR,"Stop or pause decks 1 and 2 so decks 3 and 4 can start."),
 RR("WAIT_SAFE_LOOPBACK_DISCONNECT",STARTING,CABLE,"Disconnect the loopback cable briefly so the audio route can start safely."),
 RR("WAIT_AUDIO_DEVICE",STARTING,SOCKET,"The cable was found and the audio input is starting."),
 RR("WAIT_MUSIC_HUB_PAUSED",WAITING_NATIVE_STOP,SELECTOR,"Stop or pause decks 1 and 2 so the music hub can connect."),
 RR("WAIT_MUSIC_HUB",STARTING,PORT,"The music hub was found and is connecting."),
 RR("MUSIC_HUB_BIND_FAILED",FAILED,PORT,"The music hub could not connect; unplug it briefly and reconnect it."),
 RR("UNSUPPORTED_MUSIC_PORT",FAILED,PORT,"Connect music drives to the top USB port opposite the loopback cable."),
 RR("UNSUPPORTED_MUSIC_TOPOLOGY",FAILED,PORT,"Use one music hub in the other top USB port; do not connect hubs together."),
 RR("UNSUPPORTED_MUSIC_INVENTORY",FAILED,PORT,"The connected music drives could not be checked safely."),
 RR("RACE_MUSIC_INVENTORY",STARTING,PORT,"The music connections changed during setup; checking them again."),
 RR("TERMINATED_ROUTE_IDENTITY",FAILED,NONE,"The USB connections or player session changed; restart the USB loader."),
 RR("TERMINATED_MUSIC_HUB_IDENTITY",FAILED,PORT,"The music hub could not be checked safely; restart the USB loader."),
 RR("WAIT_MUSIC_MOUNT",STARTING,PORT,"Waiting for the music drive in either top USB port to become available."),
 RR("WAIT_MUSIC_SERIAL_READ",STARTING,PORT,"Waiting to identify the music drive."),
 RR("WAIT_MUSIC_DESCRIPTORS",STARTING,PORT,"Waiting for the music drive to finish connecting."),
 RR("UNSUPPORTED_MUSIC_MOUNT",FAILED,PORT,"Connect music drives to the top USB port opposite the loopback cable."),
 RR("UNSUPPORTED_UNIT_OR_USB2_TOPOLOGY",FAILED,PORT,"The music drive connection does not support this audio route."),
 RR("UNSUPPORTED_MUSIC_SERIAL_LENGTH",FAILED,PORT,"The music drive could not be identified safely."),
 RR("UNSUPPORTED_USB2_BLOCK_ANCESTRY",FAILED,PORT,"The music drive could not be confirmed on its top USB port."),
 RR("UNSUPPORTED_MUSIC_FILESYSTEM",FAILED,PORT,"The music drive format does not support this audio route."),
 RR("RACE_MUSIC_DESCRIPTORS",STARTING,PORT,"The music drive changed while being checked; waiting to check it again."),
 RR("RACE_MUSIC_CAPTURE",STARTING,PORT,"The music drive changed while being checked; waiting to check it again."),
 RR("FATAL_BINDING_NOT_NEW",FAILED,NONE,"The four-deck helper could not start a new audio route."),
 RR("FATAL_PROCESS_SET_CHANGE",FAILED,NONE,"The running audio applications changed during setup."),
 RR("MUSIC_READY",STARTING,NONE,"The music drive is ready and the audio route is starting."),
 RR("READY_CH3_CH4",STARTING,NONE,"The audio route was found; waiting for decks 3 and 4 to connect."),
 RR("ROUTE_DISCONNECTED",NOT_DETECTED,CABLE,"The loopback cable is no longer detected."),
 RR("BIND_FAILED; disconnect loopback to retry",FAILED,CABLE,"The cable could not connect; disconnect it briefly before trying again."),
 RR("STOP_PENDING_DISCONNECT; module retained, no force unload",FAILED,CABLE,"The four-deck helper is stopping; disconnect the loopback cable."),
 RR("STOPPED",FAILED,NONE,"The four-deck helper has stopped."),
 RR("STOPPED_WITH_ERROR",FAILED,NONE,"The four-deck helper stopped before the audio route could start."),
 RR("CLEANUP_NODE_REMOVE_FAILURE",FAILED,NONE,"The four-deck helper could not finish closing the audio route."),
 RR("CLEANUP_MODULE_UNLOAD_FAILURE",FAILED,NONE,"The four-deck helper could not finish stopping."),
 RR("TERMINATED_SIGNAL",FAILED,NONE,"The four-deck helper was asked to stop."),
 RR("TERMINATED_STOP_MARKER",FAILED,NONE,"The four-deck helper was asked to stop."),
 RR("TERMINATED_NATIVE_END_OR_IDENTITY_CHANGE",FAILED,NONE,"The native audio application stopped or changed."),
 RR("TERMINATED_PROCESS_SET_CHANGE",FAILED,NONE,"The running audio applications changed during setup."),
 RR("TERMINATED_STOPPED_MEDIA_ADMISSION",FAILED,PORT,"The music drive could not be checked safely."),
 RR("TERMINATED_MUSIC_IDENTITY",FAILED,PORT,"The music drive was disconnected or changed."),
 RR("TERMINATED_FOREIGN_MODULE",FAILED,NONE,"Another audio route is already present; the helper stopped safely."),
 RR("TERMINATED_FOREIGN_NODE",FAILED,NONE,"Another audio route is already present; the helper stopped safely."),
 RR("TERMINATED_FOREIGN_MODULE_OR_NODE",FAILED,NONE,"Another audio route is already present; the helper stopped safely."),
 RR("TERMINATED_EVENT_SOCKET_FAILURE",FAILED,NONE,"The four-deck helper could not monitor cable connections."),
 RR("TERMINATED_PID_WRITE_FAILURE",FAILED,NONE,"The four-deck helper could not publish its running status."),
 RR("TERMINATED_PROBE_LOAD_FAILURE",FAILED,NONE,"The four-deck helper could not check audio route support."),
 RR("TERMINATED_PROBE_UNLOAD_FAILURE",FAILED,NONE,"The four-deck helper could not finish checking audio route support."),
 RR("TERMINATED_ADMISSION_DEBOUNCE_FAILURE",FAILED,CABLE,"The connection changed while the audio route was being checked."),
 RR("TERMINATED_APPLY_LOAD_FAILURE",FAILED,NONE,"The four-deck helper could not prepare the audio route."),
 RR("TERMINATED_SCAN_FAILURE",FAILED,CABLE,"The four-deck helper could not check the loopback connection."),
 RR("TERMINATED_NODE_REMOVE_FAILURE",FAILED,NONE,"The four-deck helper could not close the previous audio route."),
 RR("TERMINATED_BIND_DEBOUNCE_FAILURE",FAILED,CABLE,"The cable connection changed while the audio input was starting."),
 RR("TERMINATED_MODULE_ADMISSION_FAILURE",FAILED,NONE,"The audio route changed during setup; the helper stopped safely."),
 RR("TERMINATED_NODE_WRITE_FAILURE",FAILED,NONE,"The four-deck helper could not publish the audio route."),
 RR("TERMINATED_UNSPECIFIED",FAILED,NONE,"The four-deck helper stopped without reporting why."),
 /* Reserved diagnosis: the current helper cannot distinguish this from scan failure. */
 RR("LOOPBACK_ON_USB2",FAILED,PORT,"Move the loopback cable to the top USB port opposite the music drive.")
};
#undef RR

static struct xz_extra_route_status route_message(enum xz_extra_route_state state,
 enum xz_extra_route_part part,const char *text) {
 struct xz_extra_route_status s={0};s.state=state;s.fail_part=part;
 snprintf(s.reason,sizeof s.reason,"%s",text);return s;
}
static struct xz_extra_route_status route_reason_map(const char *reason,int terminal) {
 for(size_t i=0;i<sizeof route_reasons/sizeof *route_reasons;i++)
  if(!strcmp(reason,route_reasons[i].code)) {
   struct xz_extra_route_status s=route_message(route_reasons[i].state,route_reasons[i].part,route_reasons[i].sentence);
   if(terminal)s.state=XZ_EXTRA_ROUTE_FAILED;
   return s;
  }
 return route_message(terminal?XZ_EXTRA_ROUTE_FAILED:XZ_EXTRA_ROUTE_STARTING,XZ_EXTRA_ROUTE_PART_NONE,
  terminal?"The four-deck helper stopped without a recognized explanation.":
           "The four-deck helper is running; the audio route is not ready yet.");
}
struct route_bridge { int valid; char reason[160],terminal[160]; };

/* Bridge output is a bounded, fixed-schema JSON object, atomically renamed by its writer.
   Reject truncated, malformed, extra-field or future-schema records rather than guessing. */
static int route_literal(const char **p,const char *literal) {
 size_t n=strlen(literal);if(strncmp(*p,literal,n))return 0;*p+=n;return 1;
}
static int route_string(const char **p,char *out,size_t cap) {
 if(!route_literal(p,"\""))return 0;
 size_t n=0;
 while(**p&&**p!='"') {
  if(n+1>=cap||(unsigned char)**p<32u||**p=='\\')return 0;
  out[n++]=*(*p)++;
 }
 out[n]=0;return route_literal(p,"\"");
}
static int route_bool(const char **p) {
 return route_literal(p,"true")||route_literal(p,"false");
}
static int route_bridge_parse(const char *text,struct route_bridge *b) {
 char state[24];const char *p=text;memset(b,0,sizeof *b);
 if(!route_literal(&p,"{\"schema\":\"xz-bridge-status/1\",\"state\":")||
    !route_string(&p,state,sizeof state)||!route_literal(&p,",\"reason\":")||
    !route_string(&p,b->reason,sizeof b->reason)||!b->reason[0]||
    !route_literal(&p,",\"terminal_reason\":"))return 0;
 if(!route_literal(&p,"null")&&!route_string(&p,b->terminal,sizeof b->terminal))return 0;
 if(!route_literal(&p,",\"retryable\":")||!route_bool(&p)||!route_literal(&p,",\"attempt\":"))return 0;
 if(*p<'0'||*p>'9')return 0;
 char *end;errno=0;unsigned long attempt=strtoul(p,&end,10);
 if(errno||attempt>UINT_MAX)return 0;
 p=end;
 if(!route_literal(&p,",\"route_ready\":")||!route_bool(&p)||!route_literal(&p,"}"))return 0;
 while(*p==' '||*p=='\r'||*p=='\n')p++;
 if(*p||(strcmp(state,"ready")&&strcmp(state,"waiting")&&strcmp(state,"stopping")&&strcmp(state,"stopped")))return 0;
 b->valid=1;return 1;
}
static int route_read_file(const char *path,char *out,size_t cap,struct stat *st) {
 int fd=open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
 if(fd<0)return 0;
 int ok=!fstat(fd,st)&&S_ISREG(st->st_mode)&&st->st_size>=0&&(uint64_t)st->st_size<cap;
 memset(out,0,cap);
 ssize_t n=ok?read(fd,out,cap-1):-1;
 close(fd);
 if(n<0||(size_t)n>=cap-1)return 0;
 out[n]=0;return ok;
}
static int route_time_before(struct timespec a,struct timespec b) {
 return a.tv_sec<b.tv_sec||(a.tv_sec==b.tv_sec&&a.tv_nsec<b.tv_nsec);
}
/* No signal probe: verify a live, non-zombie process, its exact executable/arguments,
   and start time. Re-read identity after status IO to reject PID replacement races. */
static int route_bridge_process(const char *proc,long pid,unsigned long long *start) {
 char path[PATH_MAX],buf[2048],exe[2048];struct stat st;
 snprintf(path,sizeof path,"%s/%ld/cmdline",proc,pid);
 if(!route_read_file(path,buf,sizeof buf,&st))return 0;
 size_t len=strlen(buf);const char *base=strrchr(buf,'/');base=base?base+1:buf;
 if(len+1+sizeof "--run">=sizeof buf||strcmp(base,"xz-auto-bridge")||strcmp(buf+len+1,"--run"))return 0;
 snprintf(path,sizeof path,"%s/%ld/exe",proc,pid);
 ssize_t exe_len=readlink(path,exe,sizeof exe-1);
 if(exe_len<0||(size_t)exe_len>=sizeof exe-1)return 0;
 exe[exe_len]=0;if(strcmp(exe,buf))return 0;
 const char *native=buf+len+1+sizeof "--run";
 if(native>=buf+sizeof buf-1||!*native)return 0;
 for(const char *p=native;*p;p++)if(*p<'0'||*p>'9')return 0;
 if(strtol(native,NULL,10)<=0||native+strlen(native)+1>=buf+sizeof buf||native[strlen(native)+1])return 0;
 snprintf(path,sizeof path,"%s/%ld/stat",proc,pid);
 if(!route_read_file(path,buf,sizeof buf,&st))return 0;
 char *p=strrchr(buf,')');if(!p||p[1]!=' '||p[2]=='Z'||p[2]=='X')return 0;
 p+=2;
 for(int field=3;field<22;field++){p=strchr(p,' ');if(!p)return 0;p++;}
 char *end;errno=0;*start=strtoull(p,&end,10);
 return !errno&&end!=p&&*start>0;
}
static struct route_bridge route_bridge_read(const char *dir,const char *proc) {
 struct route_bridge b={0};char path[PATH_MAX],buf[2048],pid_text[32];
 struct stat pid_st,status_st,check_st;unsigned long long start,again;
 snprintf(path,sizeof path,"%s/bridge.pid",dir);
 if(!route_read_file(path,pid_text,sizeof pid_text,&pid_st))return b;
 char *end;errno=0;long pid=strtol(pid_text,&end,10);
 if(errno||pid<=0||(*end&&strcmp(end,"\n"))||!route_bridge_process(proc,pid,&start))return b;
 snprintf(path,sizeof path,"%s/status.json",dir);
 if(!route_read_file(path,buf,sizeof buf,&status_st)||route_time_before(status_st.st_mtim,pid_st.st_mtim))return b;
 /* btime + process start ticks rejects a PID file left by an older process. */
 char boot[256];unsigned long long seconds=0;long hz=sysconf(_SC_CLK_TCK);
 snprintf(path,sizeof path,"%s/stat",proc);
 FILE *f=fopen(path,"r");if(!f)return b;
 while(fgets(boot,sizeof boot,f))if(!strncmp(boot,"btime ",6)){seconds=strtoull(boot+6,&end,10);break;}
 fclose(f);if(!seconds||hz<=0)return b;
 struct timespec born={(time_t)(seconds+start/(unsigned long)hz),(long)((start%(unsigned long)hz)*1000000000u/(unsigned long)hz)};
 if(route_time_before(status_st.st_mtim,born))return b;
 if(!route_bridge_parse(buf,&b))return (struct route_bridge){0};
 snprintf(path,sizeof path,"%s/bridge.pid",dir);
 if(!route_read_file(path,buf,sizeof buf,&check_st)||strcmp(buf,pid_text)||
    check_st.st_ino!=pid_st.st_ino||route_time_before(pid_st.st_mtim,check_st.st_mtim)||
    !route_bridge_process(proc,pid,&again)||start!=again)return (struct route_bridge){0};
 return b;
}
static struct xz_extra_route_status route_observed(const struct route_bridge *b,int node,int up,
 int ever_up,unsigned faults,xz_usb_result refused) {
 if(up)return route_message(XZ_EXTRA_ROUTE_READY,XZ_EXTRA_ROUTE_PART_NONE,"Decks 3 and 4 are ready.");
 if(b->valid&&b->terminal[0])return route_reason_map(b->terminal,1);
 if(ever_up)return route_message(XZ_EXTRA_ROUTE_LOST,XZ_EXTRA_ROUTE_PART_CABLE,"The audio route was disconnected; check the loopback cable.");
 if(faults>=3)return route_message(XZ_EXTRA_ROUTE_FAILED,XZ_EXTRA_ROUTE_PART_SOCKET,"The audio connection has failed repeatedly; check the cable and audio input.");
 if(refused==XZ_USB_BUSY)return route_message(XZ_EXTRA_ROUTE_FAILED,XZ_EXTRA_ROUTE_PART_SOCKET,"The audio input is busy; close the application that is using it.");
 if(refused==XZ_USB_WRONG_DEVICE)return route_message(XZ_EXTRA_ROUTE_FAILED,XZ_EXTRA_ROUTE_PART_CABLE,"The connected device is not the expected loopback audio input.");
 if(node)return route_message(XZ_EXTRA_ROUTE_STARTING,XZ_EXTRA_ROUTE_PART_NONE,"The audio route was found and decks 3 and 4 are starting.");
 if(b->valid)return route_reason_map(b->reason,0);
 return route_message(XZ_EXTRA_ROUTE_NOT_DETECTED,XZ_EXTRA_ROUTE_PART_CABLE,"The loopback audio route has not been detected.");
}
#endif
