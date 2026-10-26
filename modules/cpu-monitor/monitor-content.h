/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Low-overhead Linux hardware telemetry for independent native desktop cards. */
#include <dirent.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/statvfs.h>
#include <unistd.h>

typedef struct { guint64 total, idle; double load; int mhz; } CpuCore;
typedef struct { gboolean valid; guint gpu, mem; guint64 used, total; guint graphics_mhz, memory_mhz, temp, watts; char name[96]; } NvGpu;
typedef struct {
    CpuCore cores[256]; guint core_count; double cpu_load, cpu_mhz, cpu_temp;
    guint64 mem_total, mem_available, swap_total, swap_free;
    NvGpu nvidia; double intel_load; int intel_mhz, intel_temp;
    guint64 disk_read, disk_write, disk_prev_read, disk_prev_write; gint64 disk_time;
    struct { char *path; guint64 total, free; gboolean valid; } storage[8]; guint storage_count;
} MonData;
static MonData mon;
static void *nvml_lib;
static gboolean nvml_ready;
static void *nvml_device;

typedef struct { unsigned int gpu, memory; } NvUtil;
typedef struct { uint64_t total, free, used; } NvMemory;
static char *mopt(const char *name, const char *fallback) {
    char *key = g_strconcat(PREFIX "/monitor/", name, NULL);
    char *value = xfconf_channel_get_string(host->settings(), key, fallback);
    g_free(key); return value;
}
static char *aopt(const char *name,const char *fallback){char*key=g_strconcat(PREFIX "/appearance/",name,NULL);char*value=xfconf_channel_get_string(host->settings(),key,fallback);g_free(key);return value;}
static gboolean abool(const char*name,gboolean fallback){char*key=g_strconcat(PREFIX "/appearance/",name,NULL);gboolean value=xfconf_channel_get_bool(host->settings(),key,fallback);g_free(key);return value;}
static char *default_title(void){if(!strcmp(MONITOR_KIND,"cpu"))return g_strdup("CPU Monitor");if(!strcmp(MONITOR_KIND,"gpu"))return g_strdup("GPU Monitor");if(!strcmp(MONITOR_KIND,"memory"))return g_strdup("Memory Monitor");return g_strdup("Storage Monitor");}
static void draw_title(GtkWidget*v,cairo_t*cr,GdkRectangle r,GdkRGBA ink){if(!abool("title-visible",TRUE))return;char*base=default_title(),*title=aopt("title",base);g_free(base);char*align=aopt("title-alignment","center");PangoAlignment a=!strcmp(align,"left")?PANGO_ALIGN_LEFT:!strcmp(align,"right")?PANGO_ALIGN_RIGHT:PANGO_ALIGN_CENTER;double y=abool("title-bottom",FALSE)?r.y+r.height-26:r.y+10;text(v,cr,title,r.x+12,y,r.width-24,.95,TRUE,a,ink);g_free(align);g_free(title);}
static gboolean mread_u64(const char *path, guint64 *out) {
    char *data = NULL; gsize len = 0;
    if (!g_file_get_contents(path, &data, &len, NULL)) return FALSE;
    char *end = NULL; guint64 n = g_ascii_strtoull(data, &end, 10);
    gboolean ok = end != data; if (ok) *out = n;
    g_free(data); return ok;
}
static void sample_cpu(void) {
    FILE *f = fopen("/proc/stat", "r"); if (!f) return;
    char *line = NULL; size_t cap = 0; guint64 sum_total = 0, sum_idle = 0; guint n = 0;
    while (getline(&line, &cap, f) > 0) {
        if (strncmp(line, "cpu", 3) || !g_ascii_isdigit(line[3])) continue;
        char *p = line + 3; guint idx = (guint)strtoul(p, &p, 10);
        if (idx >= G_N_ELEMENTS(mon.cores)) continue;
        guint64 v[10] = {0}; for (int i=0;i<10 && *p;i++) { while (g_ascii_isspace(*p)) p++; if (!g_ascii_isdigit(*p)) break; v[i]=g_ascii_strtoull(p,&p,10); }
        guint64 total=0; for (int i=0;i<8;i++) total+=v[i]; guint64 idle=v[3]+v[4];
        CpuCore *c=&mon.cores[idx]; if(c->total && total>c->total && idle>=c->idle){guint64 dt=total-c->total,di=idle-c->idle;c->load=dt?(100.0*(dt-di)/dt):0;}
        c->total=total;c->idle=idle;
        char path[128];g_snprintf(path,sizeof(path),"/sys/devices/system/cpu/cpu%u/cpufreq/scaling_cur_freq",idx);guint64 mhz=0;
        if(mread_u64(path,&mhz))c->mhz=(int)(mhz/1000);
        sum_total+=total;sum_idle+=idle;n=MAX(n,idx+1);
    }
    free(line);fclose(f);mon.core_count=n;
    static guint64 prev_total,prev_idle;
    if(prev_total && sum_total>prev_total && sum_idle>=prev_idle){guint64 dt=sum_total-prev_total,di=sum_idle-prev_idle;mon.cpu_load=dt?CLAMP(100.0*(dt-di)/dt,0,100):0;}
    prev_total=sum_total;prev_idle=sum_idle;
    guint freq_count=0;double freq_sum=0;for(guint i=0;i<n;i++)if(mon.cores[i].mhz>0){freq_sum+=mon.cores[i].mhz;freq_count++;}
    mon.cpu_mhz=freq_count?freq_sum/freq_count:0;
}
static void sample_cpu_temp(void) {
    mon.cpu_temp=0;
    GDir *dir=g_dir_open("/sys/class/hwmon",0,NULL);if(!dir)return;const char *d;
    while((d=g_dir_read_name(dir))){char path[256];g_snprintf(path,sizeof(path),"/sys/class/hwmon/%s/name",d);char *name=NULL;if(!g_file_get_contents(path,&name,NULL,NULL))continue;
        gboolean coretemp=g_str_has_prefix(name,"coretemp");g_free(name);if(!coretemp)continue;
        for(int i=1;i<20;i++){char labelp[256],tempp[256];g_snprintf(labelp,sizeof(labelp),"/sys/class/hwmon/%s/temp%d_label",d,i);g_snprintf(tempp,sizeof(tempp),"/sys/class/hwmon/%s/temp%d_input",d,i);char *label=NULL;guint64 value=0;
            if(g_file_get_contents(labelp,&label,NULL,NULL)&&mread_u64(tempp,&value)){if(g_str_has_prefix(label,"Package id"))mon.cpu_temp=value/1000.0;else if(mon.cpu_temp==0)mon.cpu_temp=value/1000.0;}g_free(label);
        }
    }g_dir_close(dir);
}
static void sample_memory(void) {
    FILE*f=fopen("/proc/meminfo","r");if(!f)return;char *line=NULL;size_t cap=0;char key[64];guint64 value;
    while(getline(&line,&cap,f)>0){if(sscanf(line,"%63[^:]: %" SCNu64,key,&value)!=2)continue;if(!strcmp(key,"MemTotal"))mon.mem_total=value*1024;if(!strcmp(key,"MemAvailable"))mon.mem_available=value*1024;if(!strcmp(key,"SwapTotal"))mon.swap_total=value*1024;if(!strcmp(key,"SwapFree"))mon.swap_free=value*1024;}
    free(line);fclose(f);
}
static void init_nvml(void) {
    if(nvml_ready)return;nvml_ready=TRUE;
    nvml_lib=dlopen("libnvidia-ml.so.1",RTLD_LAZY|RTLD_LOCAL);if(!nvml_lib)return;
    int(*init)(void)=dlsym(nvml_lib,"nvmlInit_v2");int(*count)(unsigned int*)=dlsym(nvml_lib,"nvmlDeviceGetCount_v2");int(*handle)(unsigned int,void**)=dlsym(nvml_lib,"nvmlDeviceGetHandleByIndex_v2");
    if(!init||!count||!handle||init()!=0)return;unsigned int n=0;if(count(&n)==0&&n)handle(0,&nvml_device);
}
static void sample_nvidia(void) {
    init_nvml();mon.nvidia.valid=FALSE;if(!nvml_device)return;
    int(*util)(void*,NvUtil*)=dlsym(nvml_lib,"nvmlDeviceGetUtilizationRates");int(*memory)(void*,NvMemory*)=dlsym(nvml_lib,"nvmlDeviceGetMemoryInfo");int(*clock)(void*,int,unsigned int*)=dlsym(nvml_lib,"nvmlDeviceGetClockInfo");int(*temp)(void*,int,unsigned int*)=dlsym(nvml_lib,"nvmlDeviceGetTemperature");int(*power)(void*,unsigned int*)=dlsym(nvml_lib,"nvmlDeviceGetPowerUsage");int(*name)(void*,char*,unsigned int)=dlsym(nvml_lib,"nvmlDeviceGetName");
    NvUtil u={0};NvMemory m={0};unsigned int graphics=0,memclock=0,t=0,p=0;gboolean got=FALSE;
    if(util&&util(nvml_device,&u)==0){mon.nvidia.gpu=u.gpu;mon.nvidia.mem=u.memory;got=TRUE;}
    if(memory&&memory(nvml_device,&m)==0){mon.nvidia.total=m.total;mon.nvidia.used=m.used;got=TRUE;}
    if(clock)clock(nvml_device,0,&graphics);if(clock)clock(nvml_device,2,&memclock);if(temp)temp(nvml_device,0,&t);if(power&&power(nvml_device,&p)==0)mon.nvidia.watts=p/1000;
    if(name)name(nvml_device,mon.nvidia.name,sizeof(mon.nvidia.name));mon.nvidia.graphics_mhz=graphics;mon.nvidia.memory_mhz=memclock;mon.nvidia.temp=t;mon.nvidia.valid=got;
}
static void sample_intel_gpu(void) {
    mon.intel_load=-1;mon.intel_mhz=0;mon.intel_temp=0;GDir*d=g_dir_open("/sys/class/drm",0,NULL);if(!d)return;const char*n;
    while((n=g_dir_read_name(d))){if(!g_str_has_prefix(n,"card")||!g_ascii_isdigit(n[4]))continue;char path[256];g_snprintf(path,sizeof(path),"/sys/class/drm/%s/device/driver",n);char*target=realpath(path,NULL);if(!target||!g_str_has_suffix(target,"/i915")){free(target);continue;}free(target);
        guint64 freq=0;if(mread_u64("/sys/class/drm/card1/device/gt_cur_freq_mhz",&freq))mon.intel_mhz=(int)freq;else{g_snprintf(path,sizeof(path),"/sys/class/drm/%s/gt_cur_freq_mhz",n);if(mread_u64(path,&freq))mon.intel_mhz=(int)freq;}
        g_snprintf(path,sizeof(path),"/sys/class/drm/%s/device/hwmon",n);GDir*hd=g_dir_open(path,0,NULL);if(hd){const char*hn;while((hn=g_dir_read_name(hd))){char tp[384];g_snprintf(tp,sizeof(tp),"%s/%s/temp1_input",path,hn);guint64 milli=0;if(mread_u64(tp,&milli)&&milli>0){mon.intel_temp=(int)(milli/1000);break;}}g_dir_close(hd);}
        const char*busy_names[]={"gt_busy_percent","device/gt_busy_percent","gt/gt0/busy_percent"};for(guint i=0;i<G_N_ELEMENTS(busy_names);i++){g_snprintf(path,sizeof(path),"/sys/class/drm/%s/%s",n,busy_names[i]);if(mread_u64(path,&freq)){mon.intel_load=CLAMP(freq,0,100);break;}}
        break;
    }g_dir_close(d);
}
static void sample_disk(void) {
    FILE*f=fopen("/proc/diskstats","r");if(!f)return;guint64 read=0,write=0;char *line=NULL;size_t cap=0;
    while(getline(&line,&cap,f)>0){unsigned int major,minor;char name[128];guint64 reads,merged,rsect,rms,writes,wmerged,wsect,wms;
        if(sscanf(line,"%u %u %127s %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64,&major,&minor,name,&reads,&merged,&rsect,&rms,&writes,&wmerged,&wsect,&wms)!=11)continue;
        char path[256];g_snprintf(path,sizeof(path),"/sys/dev/block/%u:%u/partition",major,minor);if(!g_file_test(path,G_FILE_TEST_EXISTS)){
            if(g_str_has_prefix(name,"loop")||g_str_has_prefix(name,"ram")||g_str_has_prefix(name,"zram"))continue;
            read+=rsect*512;write+=wsect*512;
        }
    }free(line);fclose(f);gint64 now=g_get_monotonic_time();if(mon.disk_time){double seconds=(now-mon.disk_time)/1e6;if(seconds>0){mon.disk_read=read>=mon.disk_prev_read?(read-mon.disk_prev_read)/seconds:0;mon.disk_write=write>=mon.disk_prev_write?(write-mon.disk_prev_write)/seconds:0;}}
    mon.disk_prev_read=read;mon.disk_prev_write=write;mon.disk_time=now;
}
static void sample_storage(void) {
    for(guint i=0;i<mon.storage_count;i++)g_clear_pointer(&mon.storage[i].path,g_free);
    mon.storage_count=0;
    char *configured=mopt("mounts","");
    if(!*configured){g_free(configured);configured=mopt("mount","/");}
    char **paths=g_strsplit_set(configured,"\n,;",-1);
    for(guint i=0;paths[i]&&mon.storage_count<G_N_ELEMENTS(mon.storage);i++){
        char *path=g_strstrip(paths[i]);if(!*path)continue;gboolean duplicate=FALSE;
        for(guint j=0;j<mon.storage_count;j++)if(!strcmp(path,mon.storage[j].path)){duplicate=TRUE;break;}
        if(duplicate)continue;
        guint n=mon.storage_count++;mon.storage[n].path=g_strdup(path);struct statvfs st;
        mon.storage[n].valid=statvfs(path,&st)==0;
        if(mon.storage[n].valid){mon.storage[n].total=(guint64)st.f_blocks*st.f_frsize;mon.storage[n].free=(guint64)st.f_bavail*st.f_frsize;}
    }
    g_strfreev(paths);g_free(configured);
}
static void monitor_update(void) {
    if(!strcmp(MONITOR_KIND,"cpu")){sample_cpu();sample_cpu_temp();}
    else if(!strcmp(MONITOR_KIND,"gpu")){sample_nvidia();sample_intel_gpu();}
    else if(!strcmp(MONITOR_KIND,"memory"))sample_memory();
    else if(!strcmp(MONITOR_KIND,"storage")){sample_disk();sample_storage();}
}
static gboolean refresh(gpointer unused) {monitor_update();for(GList*l=views;l;l=l->next)gtk_widget_queue_draw(((Widget*)l->data)->view);return G_SOURCE_CONTINUE;}
static void monitor_shutdown(void) {if(nvml_lib){int(*shutdown)(void)=dlsym(nvml_lib,"nvmlShutdown");if(shutdown)shutdown();dlclose(nvml_lib);}nvml_lib=NULL;nvml_device=NULL;nvml_ready=FALSE;memset(&mon,0,sizeof(mon));}
static char *fmt_bytes(guint64 n) {static char slots[16][32];static guint at;char*out=slots[(at++)%G_N_ELEMENTS(slots)];const char*units[]={"B","KiB","MiB","GiB","TiB"};double v=n;guint i=0;while(v>=1024&&i<4){v/=1024;i++;}g_snprintf(out,32,i?"%.1f %s":"%.0f %s",v,units[i]);return out;}
static void line_text(GtkWidget*v,cairo_t*cr,GdkRectangle r,GdkRGBA ink,const char*s,int row,int rows) {int y=r.y+r.height-18-(rows-row)*20;text(v,cr,s,r.x+14,y,r.width-28,.82,FALSE,PANGO_ALIGN_LEFT,ink);}
static void bar(cairo_t*cr,double x,double y,double width,double height,double percent,GdkRGBA ink) {cairo_set_line_width(cr,height);gdk_cairo_set_source_rgba(cr,&ink);cairo_move_to(cr,x,y);cairo_line_to(cr,x+width,y);cairo_stroke(cr);GdkRGBA a=ink;a.alpha=.85;gdk_cairo_set_source_rgba(cr,&a);cairo_move_to(cr,x,y);cairo_line_to(cr,x+width*CLAMP(percent,0,100)/100.,y);cairo_stroke(cr);}
static void ring(cairo_t*cr,double cx,double cy,double radius,double percent,GdkRGBA ink) {GdkRGBA track=ink;track.alpha=.18;cairo_set_line_width(cr,7);cairo_set_line_cap(cr,CAIRO_LINE_CAP_ROUND);gdk_cairo_set_source_rgba(cr,&track);cairo_arc(cr,cx,cy,radius,-G_PI*.78,G_PI*.78);cairo_stroke(cr);gdk_cairo_set_source_rgba(cr,&ink);cairo_arc(cr,cx,cy,radius,-G_PI*.78,-G_PI*.78+G_PI*1.56*CLAMP(percent,0,100)/100);cairo_stroke(cr);}
static void monitor_draw(GtkWidget*view,cairo_t*cr) {
    Widget*w=g_object_get_data(G_OBJECT(view),WIDGET_ID);if(!w||(widget_hidden(w)&&!w->preview))return;
    GdkRectangle r=rect(w);GdkRGBA ink;gboolean custom;render_background(w,gtk_widget_get_style_context(view),cr,r,&ink,&custom);if(!custom)gtk_style_context_get_color(gtk_widget_get_style_context(view),GTK_STATE_FLAG_NORMAL,&ink);
    cairo_save(cr);cairo_rectangle(cr,r.x+6,r.y+6,MAX(0,r.width-12),MAX(0,r.height-12));cairo_clip(cr);
    GdkRGBA muted=ink;muted.alpha*=.72;const char*kind=MONITOR_KIND;int compact=r.width<300||r.height<230;double top=r.y+34;int text_width=r.width-24;draw_title(view,cr,r,ink);
    if(!strcmp(kind,"cpu")){
        char*main=g_strdup_printf("%.0f%%",mon.cpu_load);
        if(compact){text(view,cr,main,r.x+12,top,text_width,2,TRUE,PANGO_ALIGN_CENTER,ink);char*detail=mon.cpu_temp>0?g_strdup_printf("%u threads  ·  %.2f GHz  ·  Package %d°C",mon.core_count,mon.cpu_mhz/1000.,(int)mon.cpu_temp):g_strdup_printf("%u threads  ·  %.2f GHz  ·  Package temp unavailable",mon.core_count,mon.cpu_mhz/1000.);text(view,cr,detail,r.x+12,top+48,text_width,.78,FALSE,PANGO_ALIGN_CENTER,muted);g_free(detail);}
        else{double cx=r.x+r.width/2.,cy=r.y+82;ring(cr,cx,cy,39,mon.cpu_load,ink);text(view,cr,main,cx-45,cy-12,90,1.25,TRUE,PANGO_ALIGN_CENTER,ink);char*detail=mon.cpu_temp>0?g_strdup_printf("%u threads   %.2f GHz   Package %d°C",mon.core_count,mon.cpu_mhz/1000.,(int)mon.cpu_temp):g_strdup_printf("%u threads   %.2f GHz   Package temp unavailable",mon.core_count,mon.cpu_mhz/1000.);text(view,cr,detail,r.x+12,r.y+126,text_width,.78,FALSE,PANGO_ALIGN_CENTER,muted);g_free(detail);
            guint cols=r.width>500?8:r.width>380?6:4, rows=(mon.core_count+cols-1)/cols;double cellw=(r.width-24)/(double)cols,cellh=MIN(24.,(r.height-162)/(double)MAX(1,rows));
            for(guint i=0;i<mon.core_count;i++){guint row=i/cols,col=i%cols;double x=r.x+12+col*cellw,y=r.y+151+row*cellh;char*label=g_strdup_printf("%u",i);text(view,cr,label,x,y,cellw*.27,.65,FALSE,PANGO_ALIGN_LEFT,muted);g_free(label);bar(cr,x+cellw*.3,y+8,cellw*.62,3,mon.cores[i].load,ink);}
        }g_free(main);
    }else if(!strcmp(kind,"gpu")){
        char*selector=mopt("gpu","all");gboolean show_nv=strcmp(selector,"intel")!=0,show_intel=strcmp(selector,"nvidia")!=0;g_free(selector);
        if(compact){
            if(show_nv&&mon.nvidia.valid){text(view,cr,mon.nvidia.name,r.x+12,r.y+34,text_width,.7,FALSE,PANGO_ALIGN_CENTER,muted);char*usage=g_strdup_printf("GPU %u%%",mon.nvidia.gpu);text(view,cr,usage,r.x+12,r.y+53,text_width,1.2,TRUE,PANGO_ALIGN_CENTER,ink);g_free(usage);char*memory=g_strdup_printf("VRAM %s / %s",fmt_bytes(mon.nvidia.used),fmt_bytes(mon.nvidia.total));text(view,cr,memory,r.x+12,r.y+82,text_width,.78,FALSE,PANGO_ALIGN_CENTER,ink);g_free(memory);char*temp=mon.nvidia.temp?g_strdup_printf("GPU temperature  %u°C",mon.nvidia.temp):g_strdup("GPU temperature unavailable");text(view,cr,temp,r.x+12,r.y+101,text_width,.72,FALSE,PANGO_ALIGN_CENTER,muted);g_free(temp);}
            else if(show_intel){text(view,cr,"Intel Integrated GPU",r.x+12,r.y+38,text_width,.85,TRUE,PANGO_ALIGN_CENTER,ink);char*freq=mon.intel_mhz?g_strdup_printf("GT clock %d MHz  ·  GPU usage %s",mon.intel_mhz,mon.intel_load>=0?"available":"unavailable"):g_strdup("Intel GPU telemetry unavailable");text(view,cr,freq,r.x+12,r.y+66,text_width,.78,FALSE,PANGO_ALIGN_CENTER,muted);g_free(freq);char*temp=mon.intel_temp?g_strdup_printf("GPU temperature  %d°C",mon.intel_temp):g_strdup("GPU temperature unavailable");text(view,cr,temp,r.x+12,r.y+84,text_width,.72,FALSE,PANGO_ALIGN_CENTER,muted);g_free(temp);}
        }else{
            if(show_nv&&mon.nvidia.valid){char*name=g_strdup(mon.nvidia.name);text(view,cr,name,r.x+12,r.y+34,text_width,.78,FALSE,PANGO_ALIGN_CENTER,muted);g_free(name);char*usage=g_strdup_printf("%u%%",mon.nvidia.gpu);ring(cr,r.x+r.width/2.,r.y+80,39,mon.nvidia.gpu,ink);text(view,cr,usage,r.x+r.width/2.-40,r.y+68,80,1.2,TRUE,PANGO_ALIGN_CENTER,ink);g_free(usage);
                char*a=g_strdup_printf("VRAM  %s / %s  (%u%%)",fmt_bytes(mon.nvidia.used),fmt_bytes(mon.nvidia.total),mon.nvidia.total?(guint)(100.*mon.nvidia.used/mon.nvidia.total):0);text(view,cr,a,r.x+12,r.y+130,text_width,.82,FALSE,PANGO_ALIGN_CENTER,ink);g_free(a);
                char*b=g_strdup_printf("Core %u MHz  ·  Memory %u MHz",mon.nvidia.graphics_mhz,mon.nvidia.memory_mhz);text(view,cr,b,r.x+12,r.y+154,text_width,.78,FALSE,PANGO_ALIGN_CENTER,muted);g_free(b);
                char*c=mon.nvidia.temp?g_strdup_printf("GPU %u°C  ·  %u W  ·  VRAM load %u%%",mon.nvidia.temp,mon.nvidia.watts,mon.nvidia.mem):g_strdup_printf("GPU temp unavailable  ·  %u W  ·  VRAM load %u%%",mon.nvidia.watts,mon.nvidia.mem);text(view,cr,c,r.x+12,r.y+177,text_width,.78,FALSE,PANGO_ALIGN_CENTER,muted);g_free(c);
            }else if(show_nv){text(view,cr,"NVIDIA telemetry unavailable",r.x+12,top,text_width,.9,TRUE,PANGO_ALIGN_CENTER,muted);}
            if(show_intel){double iy=show_nv&&mon.nvidia.valid?r.y+r.height-64:top;text(view,cr,"Intel Integrated GPU",r.x+12,iy,text_width,.85,TRUE,PANGO_ALIGN_CENTER,ink);char*freq=mon.intel_mhz?g_strdup_printf("GT clock %d MHz  ·  GPU usage %s  ·  shared memory",mon.intel_mhz,mon.intel_load>=0?"available":"unavailable"):g_strdup("Intel GPU telemetry unavailable");text(view,cr,freq,r.x+12,iy+20,text_width,.75,FALSE,PANGO_ALIGN_CENTER,muted);g_free(freq);char*temp=mon.intel_temp?g_strdup_printf("GPU temperature %d°C",mon.intel_temp):g_strdup("GPU temperature unavailable");text(view,cr,temp,r.x+12,iy+38,text_width,.72,FALSE,PANGO_ALIGN_CENTER,muted);g_free(temp);}
        }
    }else if(!strcmp(kind,"memory")){
        guint64 used=mon.mem_total>mon.mem_available?mon.mem_total-mon.mem_available:0;double pct=mon.mem_total?100.*used/mon.mem_total:0;
        if(compact){char*big=g_strdup_printf("%.0f%% RAM",pct);text(view,cr,big,r.x+12,r.y+39,text_width,1.5,TRUE,PANGO_ALIGN_CENTER,ink);g_free(big);char*a=g_strdup_printf("%s used / %s total",fmt_bytes(used),fmt_bytes(mon.mem_total));text(view,cr,a,r.x+12,r.y+70,text_width,.8,FALSE,PANGO_ALIGN_CENTER,ink);g_free(a);char*b=g_strdup_printf("%s available  ·  swap %s",fmt_bytes(mon.mem_available),fmt_bytes(mon.swap_total-mon.swap_free));text(view,cr,b,r.x+12,r.y+92,text_width,.68,FALSE,PANGO_ALIGN_CENTER,muted);g_free(b);}
        else{char*big=g_strdup_printf("%.0f%%",pct);ring(cr,r.x+r.width/2.,r.y+94,48,pct,ink);text(view,cr,big,r.x+r.width/2.-50,r.y+80,100,1.5,TRUE,PANGO_ALIGN_CENTER,ink);g_free(big);char*a=g_strdup_printf("Used  %s    /    Total  %s",fmt_bytes(used),fmt_bytes(mon.mem_total));line_text(view,cr,r,ink,a,0,4);g_free(a);char*b=g_strdup_printf("Available  %s",fmt_bytes(mon.mem_available));line_text(view,cr,r,muted,b,1,4);g_free(b);char*c=g_strdup_printf("Swap  %s used  /  %s total",fmt_bytes(mon.swap_total-mon.swap_free),fmt_bytes(mon.swap_total));line_text(view,cr,r,muted,c,2,4);g_free(c);bar(cr,r.x+16,r.y+r.height-25,r.width-32,5,pct,ink);}
    }else{
        guint count=MAX(1,mon.storage_count);double available=r.height-(compact?54:68);double rowh=available/count;rowh=CLAMP(rowh,24,52);
        guint shown=MIN(mon.storage_count,(guint)MAX(1,(int)(available/rowh)));
        for(guint i=0;i<shown;i++){
            double y=r.y+(compact?36:44)+i*rowh;char *base=g_path_get_basename(mon.storage[i].path);if(!*base){g_free(base);base=g_strdup(mon.storage[i].path);}
            guint64 used=mon.storage[i].total>mon.storage[i].free?mon.storage[i].total-mon.storage[i].free:0;double pct=mon.storage[i].total?100.*used/mon.storage[i].total:0;
            char *label=mon.storage[i].valid?g_strdup_printf("%s   %.0f%%   %s free / %s",base,pct,fmt_bytes(mon.storage[i].free),fmt_bytes(mon.storage[i].total)):g_strdup_printf("%s   unavailable",base);
            text(view,cr,label,r.x+12,y,text_width,compact?.68:.78,FALSE,PANGO_ALIGN_LEFT,ink);g_free(label);g_free(base);
            if(rowh>30&&mon.storage[i].valid)bar(cr,r.x+14,y+16,r.width-28,3,pct,ink);
        }
        if(mon.storage_count>shown){char *more=g_strdup_printf("+ %u more drives",mon.storage_count-shown);text(view,cr,more,r.x+12,r.y+r.height-34,text_width,.65,FALSE,PANGO_ALIGN_CENTER,muted);g_free(more);}
        char *io=g_strdup_printf("Read %s/s  ·  Write %s/s",fmt_bytes(mon.disk_read),fmt_bytes(mon.disk_write));text(view,cr,io,r.x+12,r.y+r.height-20,text_width,.65,FALSE,PANGO_ALIGN_CENTER,muted);g_free(io);
    }
    cairo_restore(cr);
}
static void draw(GtkWidget *view, cairo_t *cr) { monitor_draw(view, cr); }
