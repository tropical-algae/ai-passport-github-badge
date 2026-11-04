#include "badge_ui.h"
#include "badge_ui_avatar.h"
#include "bsp_display_rounding.h"
#include "lvgl.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint16_t framebuffer[240*320], drawbuffer[240*20];
static badge_data_t data;
static badge_config_t config;
static badge_status_t status;
static lv_display_t *display;
LV_FONT_DECLARE(badge_font_8);
LV_FONT_DECLARE(badge_font_10);
LV_FONT_DECLARE(badge_font_14);
static unsigned char avatar_bytes[24576];
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *pixels) {
    const uint16_t *p=(const uint16_t *)pixels;
    for(int y=a->y1;y<=a->y2;y++)for(int x=a->x1;x<=a->x2;x++) {
        uint16_t pixel=*p++;
        framebuffer[y*240+x]=bsp_display_pixel_outside_rounded_rect(x,y,240,320,30)?0:pixel;
    }
    lv_display_flush_ready(d);
}
static void save(const char *name) {
    lv_refr_now(display);
    char filename[100];snprintf(filename,sizeof(filename),"%s.ppm",name);
    FILE *f=fopen(filename,"wb");assert(f);fprintf(f,"P6\n240 320\n255\n");
    for(unsigned i=0;i<240*320;i++) {
        uint16_t v=framebuffer[i];unsigned char rgb[3]={((v>>11)&31)*255/31,((v>>5)&63)*255/63,(v&31)*255/31};
        fwrite(rgb,1,3,f);
    }
    fclose(f);
#if LV_USE_STDLIB_MALLOC == LV_STDLIB_BUILTIN
    lv_mem_monitor_t mon;lv_mem_monitor(&mon);
    printf("%s heap free=%zu maxused=%zu\n",name,mon.free_size,mon.max_used);
#else
    printf("%s rendered with shared C allocator\n",name);
#endif
}
static void assert_ink_centered(const char *name,int x,int y,int width,int height,uint32_t color) {
    uint16_t ink=lv_color_to_u16(lv_color_hex(color));
    int top=y+height,bottom=y-1;
    for(int row=y;row<y+height;row++)for(int col=x;col<x+width;col++) {
        if(framebuffer[row*240+col]!=ink)continue;
        if(row<top)top=row;
        if(row>bottom)bottom=row;
    }
    assert(top<=bottom);
    int above=top-y,below=y+height-1-bottom;
    printf("%s visible ink y=%d..%d, padding top=%d bottom=%d\n",name,top,bottom,above,below);
    assert(abs(above-below)<=1);
}
static uint32_t frame_hash(void) {
    uint32_t hash=2166136261u;
    for(unsigned i=0;i<240*320;i++)hash=(hash^framebuffer[i])*16777619u;
    return hash;
}
static void assert_icon_avatar(void) {
    /* Known 5x5 synthetic icon, sampled at cell centers. In particular its
     * bottom/right cells catch the old scaled JPEG MCU cropping defect. */
    const bool active[5][5]={
        {false,true,true,true,false},{true,false,true,false,true},
        {true,true,false,true,true},{false,true,false,true,false},
        {true,true,true,true,true}
    };
    for(unsigned y=0;y<5;++y)for(unsigned x=0;x<5;++x) {
        uint16_t color=framebuffer[(126+8+y*9)*240+13+8+x*9];
        assert((((color>>5)&63)>35)==active[y][x]);
    }
    puts("avatar full-image cell coverage passed");
}
static uint32_t area_hash(int x,int y,int width,int height) {
    uint32_t hash=2166136261u;
    for(int row=y;row<y+height;row++)for(int col=x;col<x+width;col++)
        hash=(hash^framebuffer[row*240+col])*16777619u;
    return hash;
}
static void assert_activity_cells_solid(void) {
    unsigned partial=0,known=0;
    int32_t today=(int32_t)(status.now/86400);
    for(unsigned i=0;i<BADGE_DAYS;++i) {
        const badge_day_t *day=&data.days[i];
        if(!day->day || day->day>today || day->coverage==BADGE_UNKNOWN)continue;
        int x=9+(int)(i/7)*9,y=132+(int)(i%7)*10;
        uint16_t fill=framebuffer[y*240+x];
        /* All eight rows must be one color, including the former white-line
         * row. Keep the original two-pixel gutters outside each cell. */
        for(int dy=0;dy<8;++dy)for(int dx=0;dx<7;++dx)
            assert(framebuffer[(y+dy)*240+x+dx]==fill);
        for(int dy=0;dy<8;++dy)for(int dx=7;dx<9;++dx)
            assert(framebuffer[(y+dy)*240+x+dx]==lv_color_to_u16(lv_color_hex(0x080f0b)));
        if(day->coverage==BADGE_PARTIAL) {
            ++partial;
            /* Coverage must remain distinguishable from a known day with
             * the same activity count, without adding an edge decoration. */
            bool compared=false;
            for(unsigned j=0;j<BADGE_DAYS;++j) {
                const badge_day_t *other=&data.days[j];
                if(other->day && other->day<=today && other->coverage==BADGE_KNOWN && other->pushes==day->pushes) {
                    int ox=9+(int)(j/7)*9,oy=132+(int)(j%7)*10;
                    assert(framebuffer[oy*240+ox]!=fill);
                    compared=true;break;
                }
            }
            assert(compared);
        } else ++known;
    }
    assert(partial && known);
    puts("activity cells are solid; partial coverage stays distinct");
}
static uint32_t ring_hash(void) {
    uint32_t hash=2166136261u;
    for(int y=185;y<=269;y++)for(int x=8;x<=92;x++) {
        int radius2=(x-50)*(x-50)+(y-227)*(y-227);
        if(radius2>=24*24 && radius2<=42*42)hash=(hash^framebuffer[y*240+x])*16777619u;
    }
    return hash;
}
static unsigned ascii_ink_count(const lv_font_t *font,const char *text) {
    unsigned count=0;
    for(const unsigned char *p=(const unsigned char *)text;*p;p++) {
        lv_font_glyph_dsc_t glyph;
        assert(lv_font_get_glyph_dsc(font,&glyph,*p,p[1]));
        assert(glyph.format==LV_FONT_GLYPH_FORMAT_A1 && glyph.stride==0);
        if(!glyph.box_w || !glyph.box_h)continue;
        glyph.req_raw_bitmap=1;
        const uint8_t *bitmap=glyph.resolved_font->get_glyph_bitmap(&glyph,NULL);assert(bitmap);
        for(unsigned bit=0;bit<(unsigned)glyph.box_w*glyph.box_h;bit++)
            if(bitmap[bit/8]&(0x80u>>(bit%8)))count++;
    }
    return count;
}
static void assert_font_text_ink(const char *text,int x,int y,int width,uint32_t color,const lv_font_t *font) {
    lv_point_t size;
    lv_text_get_size(&size,text,font,0,0,LV_COORD_MAX,LV_TEXT_FLAG_EXPAND);
    assert(size.x<=width);
    uint16_t ink=lv_color_to_u16(lv_color_hex(color));
    unsigned count=0;
    for(int row=y;row<y+font->line_height;row++)for(int col=x;col<x+width;col++)
        if(framebuffer[row*240+col]==ink)count++;
    printf("visible text '%s': %u pixels, width %d/%d\n",text,count,(int)size.x,width);
    assert(count==ascii_ink_count(font,text));
}
static void assert_text_ink(const char *text,int x,int y,int width,uint32_t color) {
    assert_font_text_ink(text,x,y,width,color,&badge_font_8);
}
static void assert_battery_gap(void) {
    uint16_t ink=lv_color_to_u16(lv_color_white()),outline=lv_color_to_u16(lv_color_hex(0x91a89b));
    int text_left=240,icon_right=0;
    for(int y=9;y<20;y++)for(int x=180;x<234;x++) {
        if(framebuffer[y*240+x]==ink && x<text_left)text_left=x;
        if(framebuffer[y*240+x]==outline && x>icon_right)icon_right=x;
    }
    int gap=text_left-icon_right-1;
    printf("battery %d%% icon/text visible gap=%d px\n",status.battery_percent,gap);
    assert(text_left<233 && icon_right>=180 && gap>=3 && gap<=5);
}
static void fill_data(void) {
    badge_config_defaults(&config);strcpy(config.username,"pixeldev");config.utc_offset_min=480;
    const char *preview_username=getenv("BADGE_PREVIEW_USERNAME");
    if(preview_username && *preview_username) {
        assert(badge_username_valid(preview_username));
        snprintf(config.username,sizeof(config.username),"%s",preview_username);
    }
    badge_data_reset(&data,config.username);
    data.valid=0x1ff;data.complete=0x1ff;
    strcpy(data.name,"Pixel Developer");strcpy(data.company,"OPEN SOURCE");strcpy(data.location,"EARTH");strcpy(data.blog,"example.dev");strcpy(data.joined,"2020-04-08");
    if(preview_username && *preview_username)snprintf(data.name,sizeof(data.name),"%s",preview_username);
    data.public_repos=18;data.followers=1284;data.following=76;data.public_gists=7;
    data.repo_count=5;data.repos_fetched=18;
    const char *names[]={"pixel-terminal","retro-dashboard","tiny-os","api-client","color-kit"};
    unsigned stars[]={1380,826,341,210,98},forks[]={210,88,37,22,9};
    for(unsigned i=0;i<5;i++) {badge_repo_t *r=&data.repos[i];strcpy(r->name,names[i]);r->stars=stars[i];r->forks=forks[i];r->watchers=30-i*4;r->watchers_valid=true;strcpy(r->license,"MIT");strcpy(r->pushed_at,"2026-10-28");}
    data.scope_count=7;
    const char *languages[]={"TypeScript","Python","C++","Rust","CSS","OTHER"};
    const unsigned long values[]={7000,6500,6000,7000,4000,4300};
    for(unsigned i=0;i<7;i++) {badge_scope_t *s=&data.scopes[i];strcpy(s->title,i==0?"ORIGINAL ONLY":i==1?"ALL REPOS":names[i-2]);s->count=6;s->valid=true;s->complete=true;s->fetched_repos=18;s->total_repos=18;for(unsigned k=0;k<6;k++){strcpy(s->languages[k].name,languages[k]);s->languages[k].bytes=values[k];s->total_bytes+=values[k];}}
    status.now=1793620800;status.connected=true;status.battery_percent=87;strcpy(status.ap_ssid,"GitHub-Pixel-A1B2");strcpy(status.ap_password,"badge123456");
    badge_history_roll(&data,status.now/86400);
    for(unsigned i=0;i<168;i++){data.days[i].coverage=i<110?BADGE_UNKNOWN:i<130?BADGE_PARTIAL:BADGE_KNOWN;data.days[i].pushes=i<110?0:i%5;}
    data.events_fetched=65;data.push_events=48;data.push_days=42;
    data.pr_total=84;data.pr_open=12;data.pr_merged=57;data.pr_closed=72;data.issue_total=49;data.issue_open=8;data.issue_closed=41;
    data.starred_fetched=45;data.starred_with_language=38;data.starred_languages=12;data.star_count=2;
    strcpy(data.stars[0].name,"vercel/next.js");strcpy(data.stars[0].language,"TypeScript");data.stars[0].stars=123400;
    strcpy(data.stars[1].name,"lvgl/lvgl");strcpy(data.stars[1].language,"C");data.stars[1].stars=20000;
    data.gist_count=2;data.gists_fetched=7;data.gists_public=7;strcpy(data.gists[0].description,"Pixel display notes");strcpy(data.gists[0].updated_at,"2026-10-23");strcpy(data.gists[1].description,"API client setup");strcpy(data.gists[1].updated_at,"2026-10-21");
    data.event_count=6;
    const char *types[]={"PushEvent","PullRequestEvent","IssuesEvent","CreateEvent","WatchEvent","ForkEvent"};
    for(unsigned i=0;i<6;i++){strcpy(data.events[i].type,types[i]);snprintf(data.events[i].repo,sizeof(data.events[i].repo),"%s/%s",config.username,names[i%5]);strcpy(data.events[i].date,"2026-10-28");}
    data.last_success=status.now-3600;
}
int main(int argc,char **argv) {
    lv_init();display=lv_display_create(240,320);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,drawbuffer,NULL,sizeof(drawbuffer),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(display,flush);
    fill_data();badge_ui_init(&data,&config,&status);
    lv_font_glyph_dsc_t glyph;
    assert(lv_font_get_glyph_dsc(&badge_font_10,&glyph,0x4e2d,0));
    assert(lv_font_get_glyph_dsc(&badge_font_8,&glyph,0x6587,0));
    assert(lv_font_get_glyph_dsc(&badge_font_10,&glyph,0x9f8d,0));
    assert(lv_font_get_glyph_dsc(&badge_font_10,&glyph,0x3400,0));
    assert(!lv_font_get_glyph_dsc(&badge_font_10,&glyph,0x1f600,0));
    FILE *avatar_file=fopen(argc>1?argv[1]:"avatar.png","rb");
    assert(avatar_file);
    if(avatar_file) {
        size_t size=fread(avatar_bytes,1,sizeof(avatar_bytes),avatar_file);fclose(avatar_file);
        bool loaded=badge_ui_set_avatar(avatar_bytes,size);printf("avatar decoded: %s\n",loaded?"yes":"no");
        bool reject=argc>2 && !strcmp(argv[2],"reject");
        if(reject)assert(!loaded);else assert(loaded);
        if(loaded) {
            save("avatar-owned-pixels");uint32_t picture=frame_hash();
            if(argc>1 && strstr(argv[1],"/avatar64.jpg")) {
                /* Valid JPEG without the optional JFIF APP0 header: GitHub's
                 * CDN serves this form, which LVGL's variable detector misses. */
                assert(size>20 && avatar_bytes[2]==0xff && avatar_bytes[3]==0xe0);
                unsigned app0_size=((unsigned)avatar_bytes[4]<<8)|avatar_bytes[5];
                assert(app0_size>=2 && app0_size+4<size);
                memmove(avatar_bytes+2,avatar_bytes+app0_size+4,size-app0_size-4);
                size-=app0_size+2;
                assert(badge_ui_set_avatar(avatar_bytes,size));save("avatar-without-jfif");
                assert(frame_hash()==picture);assert_icon_avatar();
            }
#if LV_USE_STDLIB_MALLOC == LV_STDLIB_BUILTIN
            lv_mem_monitor_t memory;lv_mem_monitor(&memory);assert(memory.free_size>8192);
            void *pressure[128];unsigned held=0;
            while(memory.free_size>7168 && held<128) {
                size_t block=memory.free_biggest_size>1024?1024:memory.free_biggest_size;
                assert(block>0);pressure[held]=lv_malloc(block);assert(pressure[held]);++held;
                lv_mem_monitor(&memory);
            }
            assert(!badge_ui_set_avatar(avatar_bytes,size));
            while(held)lv_free(pressure[--held]);
            lv_refr_now(display);assert(frame_hash()==picture);
            /* The bounded 64KiB test allocator cannot keep both a transparent
             * old image and the maximum PNG decoder workspace; release the old
             * image before exercising recovery with exactly the same bytes. */
            assert(!badge_ui_set_avatar(NULL,0));
            assert(badge_ui_set_avatar(avatar_bytes,size));lv_refr_now(display);assert(frame_hash()==picture);
            puts("avatar low-memory replacement preserves pixels and recovers");
#endif
            if(argc>1 && strstr(argv[1],"/avatar64.png")) {
                /* This deterministic fixture pixel has alpha zero. Theme
                 * changes must show the new panel, not a flattened old one. */
                assert(framebuffer[131*240+56]==lv_color_to_u16(lv_color_hex(0x163a2b)));
                config.theme=1;badge_ui_update(&data,&config,&status);lv_refr_now(display);
                assert(framebuffer[131*240+56]==lv_color_to_u16(lv_color_hex(0x302719)));
                config.theme=0;badge_ui_update(&data,&config,&status);lv_refr_now(display);
                assert(frame_hash()==picture);
            }
            /* Rendering is independent of the encoded input's lifetime. */
            memset(avatar_bytes,0,size);badge_ui_update(&data,&config,&status);lv_refr_now(display);
            assert(frame_hash()==picture);
            /* Failed replacements preserve the already decoded avatar. */
            assert(!badge_ui_set_avatar(avatar_bytes,size));lv_refr_now(display);assert(frame_hash()==picture);
            if(argc>1 && (strstr(argv[1],"/avatar.png") || strstr(argv[1],"/avatar64.jpg")))assert_icon_avatar();
        }
    }
    if(argc>3 && !strcmp(argv[3],"avatar-only")) {
        puts("native avatar checks passed");
        return 0;
    }
    for(unsigned i=0;i<7;i++) {
        char name[32];snprintf(name,sizeof(name),"page-%u",i);save(name);
        if(i==3)assert_activity_cells_solid();
        badge_ui_key(BADGE_KEY_DOWN,true);
    }
    for(unsigned i=0;i<4;i++)badge_ui_key(BADGE_KEY_DOWN,true);
    badge_ui_key(BADGE_KEY_DOWN,false);badge_ui_key(BADGE_KEY_OK,false);save("issues");
    badge_ui_key(BADGE_KEY_DOWN,true);badge_ui_key(BADGE_KEY_DOWN,false);badge_ui_key(BADGE_KEY_OK,false);save("gists");
    badge_ui_key(BADGE_KEY_DOWN,true);badge_ui_key(BADGE_KEY_DOWN,true);
    badge_ui_key(BADGE_KEY_DOWN,true);badge_ui_key(BADGE_KEY_DOWN,true);
    badge_ui_key(BADGE_KEY_DOWN,false);save("languages-all");
    badge_ui_key(BADGE_KEY_DOWN,false);save("languages-repo");
    for(unsigned i=0;i<5;i++)badge_ui_key(BADGE_KEY_DOWN,true);
    badge_ui_key(BADGE_KEY_OK,false);save("profile-qr");
    assert_ink_centered("profile close",19,263,202,12,0x080f0b);badge_ui_key(BADGE_KEY_OK,false);
    badge_ui_key(BADGE_KEY_OK,true);save("settings");
    assert_font_text_ink("> AUTO SCREEN OFF",16,102,163,0x2ee4a4,&badge_font_10);
    assert_font_text_ink("10M",181,102,43,0x2ee4a4,&badge_font_10);
    badge_ui_key(BADGE_KEY_OK,false);save("brightness");
    config.brightness=10;badge_ui_update(&data,&config,&status);
    badge_ui_key(BADGE_KEY_UP,false);save("brightness-10");
    assert_font_text_ink("[X] 10%",16,226,208,0x2ee4a4,&badge_font_10);
    badge_action_t confirmed=badge_ui_key(BADGE_KEY_OK,false);
    assert(confirmed.kind==BADGE_ACTION_BRIGHTNESS&&confirmed.value==10);save("brightness-confirmed");
    assert_font_text_ink("> BRIGHTNESS  10%",16,71,208,0x2ee4a4,&badge_font_10);
    badge_ui_key(BADGE_KEY_OK,true);config.brightness=100;badge_ui_update(&data,&config,&status);
    const uint8_t timeout_values[]={1,5,10,30,60,0};
    const char *timeout_names[]={"[X] 1 MINUTE","[X] 5 MINUTES","[X] 10 MINUTES","[X] 30 MINUTES","[X] 1 HOUR","[X] NEVER"};
    for(unsigned i=0;i<6;i++) {
        config.screen_timeout_min=timeout_values[i];badge_ui_update(&data,&config,&status);
        badge_ui_key(BADGE_KEY_OK,true);badge_ui_key(BADGE_KEY_DOWN,false);badge_ui_key(BADGE_KEY_OK,false);
        char name[32];snprintf(name,sizeof(name),"screen-timeout-%u",timeout_values[i]);save(name);
        assert_font_text_ink(timeout_names[i],16,71+(int)i*31,208,0x2ee4a4,&badge_font_10);
        badge_action_t selected=badge_ui_key(BADGE_KEY_OK,false);
        assert(selected.kind==BADGE_ACTION_SCREEN_TIMEOUT&&selected.value==timeout_values[i]);
        if(!selected.value) {save("settings-never");assert_font_text_ink("NEVER",181,102,43,0x2ee4a4,&badge_font_10);}
        badge_ui_key(BADGE_KEY_OK,true);
    }
    config.screen_timeout_min=10;badge_ui_update(&data,&config,&status);
    badge_ui_key(BADGE_KEY_OK,true);badge_ui_key(BADGE_KEY_DOWN,false);badge_ui_key(BADGE_KEY_DOWN,false);badge_ui_key(BADGE_KEY_OK,false);save("theme-menu");
    for(int y=216;y<246;++y)for(int x=9;x<231;++x)
        assert(framebuffer[y*240+x]==lv_color_to_u16(lv_color_hex(0x080f0b))); /* No sixth BACK row. */
    confirmed=badge_ui_key(BADGE_KEY_OK,false);assert(confirmed.kind==BADGE_ACTION_THEME&&confirmed.value==0);save("theme-confirmed");
    badge_ui_key(BADGE_KEY_OK,true);
    status.provisioning=true;badge_ui_tick(&status);badge_ui_show_provision();save("wifi");
    assert_ink_centered("wifi close",19,263,202,12,0x080f0b);badge_ui_key(BADGE_KEY_OK,false);
    for(unsigned i=1;i<5;i++){config.theme=i;badge_ui_update(&data,&config,&status);char name[32];snprintf(name,sizeof(name),"theme-%u",i);save(name);}
    strcpy(data.company,"开源实验室");strcpy(data.location,"中国上海");strcpy(data.name,"中文开发者");badge_ui_update(&data,&config,&status);save("chinese");
    /* A background network status update must refresh first-boot AP QR. */
    status.provisioning=false;status.ap_ssid[0]=0;badge_ui_tick(&status);
    badge_ui_show_provision();save("wifi-starting");
    status.provisioning=true;strcpy(status.ap_ssid,"GitHub-Pixel-A1B2");badge_ui_tick(&status);save("wifi-ready");
    status.provisioning=false;badge_ui_tick(&status);save("wifi-closed");
    /* Exercise the maximum GitHub username QR and viewport clipping. */
    strcpy(config.username,"abcdefghijklmnopqrstuvwxyz0123456789abc");
    badge_ui_update(&data,&config,&status);badge_ui_key(BADGE_KEY_OK,false);save("long-profile-qr");
    badge_ui_key(BADGE_KEY_OK,false);
    for(unsigned n=0;n<35;n++){badge_ui_key(BADGE_KEY_DOWN,true);lv_refr_now(display);}
#if LV_USE_STDLIB_MALLOC == LV_STDLIB_BUILTIN
    lv_mem_monitor_t mon;lv_mem_monitor(&mon);assert(mon.free_size>10000);
#endif
    assert(!badge_ui_set_avatar(NULL,0));
    /* Keep the no-avatar states visible in the real LVGL rendering fixtures. */
    badge_config_defaults(&config);badge_data_reset(&data,"");
    status.connected=false;status.provisioning=false;
    badge_ui_update(&data,&config,&status);save("empty-profile");
    assert_ink_centered("question avatar",13,126,54,54,0x2ee4a4);
    strcpy(config.username,"pixeldev");badge_data_reset(&data,config.username);
    badge_ui_update(&data,&config,&status);save("initial-profile");
    assert_ink_centered("initial avatar",13,126,54,54,0x2ee4a4);

    fill_data();badge_ui_update(&data,&config,&status);
    const int battery_cases[]={0,9,99,100};
    for(unsigned i=0;i<sizeof(battery_cases)/sizeof(battery_cases[0]);i++) {
        char name[32];status.battery_percent=battery_cases[i];badge_ui_tick(&status);
        snprintf(name,sizeof(name),"battery-%d",status.battery_percent);save(name);assert_battery_gap();
    }
    status.battery_percent=87;badge_ui_tick(&status);
    data.scope_count=1;badge_ui_update(&data,&config,&status);
    badge_ui_key(BADGE_KEY_DOWN,true);badge_ui_key(BADGE_KEY_DOWN,true);save("languages-compact");
    uint32_t ring=ring_hash();
    strcpy(data.scopes[0].languages[0].name,"VeryLongLanguageNameForOverflow");
    badge_ui_update(&data,&config,&status);save("languages-long");assert(ring_hash()==ring);
    data.scopes[0].count=1;data.scopes[0].total_bytes=data.scopes[0].languages[0].bytes;
    badge_ui_update(&data,&config,&status);save("languages-100");
    unsigned percent_ink=0;
    for(int y=215;y<230;y++)for(int x=27;x<74;x++)
        if(framebuffer[y*240+x]==lv_color_to_u16(lv_color_white()))percent_ink++;
    /* Every pixel of "100%" must survive fitting and drawing inside the ring. */
    assert(percent_ink==ascii_ink_count(&badge_font_14,"100%"));
    badge_ui_key(BADGE_KEY_UP,true);badge_ui_key(BADGE_KEY_UP,true);

    /* Closing a successful Wi-Fi dialog keeps its page/settings and AP state. */
    status.provisioning=true;badge_ui_tick(&status);save("before-wifi-close");
    uint32_t baseline=frame_hash();
    badge_ui_show_provision();save("wifi-before-success");assert(frame_hash()!=baseline);
    badge_ui_close_provision();save("wifi-success-closed");assert(frame_hash()==baseline);
    assert(status.provisioning);badge_ui_tick(&status);lv_refr_now(display);assert(frame_hash()==baseline);
    badge_ui_key(BADGE_KEY_OK,false);save("profile-qr-before-close-request");baseline=frame_hash();
    badge_ui_close_provision();save("profile-qr-preserved");assert(frame_hash()==baseline);
    badge_ui_notify("SYNC COMPLETE",false);save("toast-over-profile-qr");assert(frame_hash()!=baseline);
    badge_ui_notify(NULL,false);lv_refr_now(display);assert(frame_hash()==baseline);
    uint32_t qr_pixels=area_hash(50,81,140,140),footer_pixels=area_hash(0,293,240,27);
    badge_ui_notify("RATE LIMIT / 4M 12S\nACTIVITY / HTTP 403",true);save("toast-two-lines-profile-qr");
    assert_text_ink("RATE LIMIT / 4M 12S",14,262,212,0xf17b5c);
    assert_text_ink("ACTIVITY / HTTP 403",14,275,212,0xf17b5c);
    assert(area_hash(50,81,140,140)==qr_pixels && area_hash(0,293,240,27)==footer_pixels);
    badge_ui_notify(NULL,false);lv_refr_now(display);assert(frame_hash()==baseline);
    badge_ui_key(BADGE_KEY_OK,false);badge_ui_key(BADGE_KEY_OK,true);
    badge_ui_key(BADGE_KEY_DOWN,false);badge_ui_key(BADGE_KEY_DOWN,false);badge_ui_key(BADGE_KEY_DOWN,false);
    save("settings-before-wifi-close");baseline=frame_hash();
    badge_ui_show_provision();badge_ui_close_provision();save("settings-wifi-closed");assert(frame_hash()==baseline);

    /* Refetch itself makes no success claim; only explicit application results do. */
    badge_ui_key(BADGE_KEY_DOWN,false);save("refetch-selected");baseline=frame_hash();
    badge_action_t action=badge_ui_key(BADGE_KEY_OK,false);lv_refr_now(display);
    assert(action.kind==BADGE_ACTION_SYNC && frame_hash()==baseline);
    char result[]="SYNC COMPLETE";badge_ui_notify(result,false);memset(result,'X',sizeof(result)-1);
    save("toast-success");uint32_t copied=frame_hash();assert(copied!=baseline);
    badge_ui_notify("SYNC COMPLETE",false);lv_refr_now(display);assert(frame_hash()==copied);
    badge_ui_notify("PARTIAL DATA / CHECK STATUS",true);save("toast-partial");
    assert(framebuffer[268*240+8]==lv_color_to_u16(lv_color_hex(0xf17b5c)));
    badge_ui_notify("FETCH FAILED / RETRY LATER",true);save("toast-failure");
    lv_tick_inc(4001);badge_ui_tick(&status);save("toast-expired");assert(frame_hash()==baseline);
    badge_ui_notify("SYNC COMPLETE",false);badge_ui_notify("",false);lv_refr_now(display);assert(frame_hash()==baseline);
    char two_lines[]="RATE LIMIT / 4M 12S\nACTIVITY / HTTP 403";
    badge_ui_notify(two_lines,true);memset(two_lines,'X',sizeof(two_lines)-1);save("toast-rate-limit");copied=frame_hash();
    assert_text_ink("RATE LIMIT / 4M 12S",14,262,212,0xf17b5c);
    assert_text_ink("ACTIVITY / HTTP 403",14,275,212,0xf17b5c);
    badge_ui_notify("RATE LIMIT / 4M 12S\nACTIVITY / HTTP 403",true);lv_refr_now(display);assert(frame_hash()==copied);
    badge_ui_notify("FETCH FAILED\nPROFILE / TLS CONNECT FAILED",true);save("toast-tls-failure");
    assert_text_ink("FETCH FAILED",14,262,212,0xf17b5c);
    assert_text_ink("PROFILE / TLS CONNECT FAILED",14,275,212,0xf17b5c);
    badge_ui_notify("PARTIAL / RETRY READY\nACTIVITY / HTTP 502",true);save("toast-http-failure");
    assert_text_ink("PARTIAL / RETRY READY",14,262,212,0xf17b5c);
    assert_text_ink("ACTIVITY / HTTP 502",14,275,212,0xf17b5c);
    lv_tick_inc(4001);badge_ui_tick(&status);lv_refr_now(display);assert(frame_hash()==baseline);
    badge_ui_notify("SYNC COMPLETE\n",false);lv_refr_now(display);copied=frame_hash();
    badge_ui_notify("SYNC COMPLETE",false);lv_refr_now(display);assert(frame_hash()==copied);
    badge_ui_notify(NULL,false);lv_refr_now(display);assert(frame_hash()==baseline);
    badge_ui_key(BADGE_KEY_OK,true);
    /* A real tick, without badge_ui_update, must advance and expire the wait. */
    const int64_t original_now=status.now;
    data.retry_after=status.now+252;strcpy(data.sync_message,"ACTIVITY: RATE LIMIT");
    badge_ui_update(&data,&config,&status);save("retry-rate-limit");
    assert_text_ink("LIMIT 4M 12S / ACTIVITY: RATE LIMIT",9,281,222,0x91a89b);
    uint32_t waiting=area_hash(9,281,222,9);
    status.now++;badge_ui_tick(&status);save("retry-countdown-tick");
    assert(area_hash(9,281,222,9)!=waiting);
    assert_text_ink("LIMIT 4M 11S / ACTIVITY: RATE LIMIT",9,281,222,0x91a89b);
    strcpy(data.sync_message,"PARTIAL / PARTIAL / PROFILE: TLS");
    badge_ui_update(&data,&config,&status);save("retry-tls");
    assert_text_ink("RETRY 4M 11S / PROFILE: TLS",9,281,222,0x91a89b);
    data.retry_after=status.now+1;badge_ui_update(&data,&config,&status);save("retry-last-second");
    assert_text_ink("RETRY 1S / PROFILE: TLS",9,281,222,0x91a89b);
    waiting=area_hash(9,281,222,9);status.now++;badge_ui_tick(&status);save("retry-expired");
    assert(area_hash(9,281,222,9)!=waiting);
    data.retry_after=INT64_MAX;badge_ui_update(&data,&config,&status);save("retry-extreme");
    assert_text_ink("RETRY 999D+ / PROFILE: TLS",9,281,222,0x91a89b);
    status.now=INT64_MIN;badge_ui_tick(&status);save("retry-invalid-clock");
    status.now=original_now;data.retry_after=0;data.sync_message[0]=0;
    badge_ui_update(&data,&config,&status);
    /* Page/settings changes never stop a running worker. A requested Wi-Fi
     * portal explains why it waits until the background fetch has finished. */
    status.syncing=true;strcpy(data.sync_message,"PROFILE / FETCHING");
    badge_ui_update(&data,&config,&status);badge_ui_key(BADGE_KEY_OK,true);
    for(unsigned i=0;i<4;i++)badge_ui_key(BADGE_KEY_DOWN,false);
    action=badge_ui_key(BADGE_KEY_OK,true);save("sync-after-exit-settings");
    assert(action.kind==BADGE_ACTION_NONE);
    assert_text_ink("PROFILE / FETCHING",9,281,222,0x91a89b);
    action=badge_ui_key(BADGE_KEY_DOWN,true);save("sync-while-browsing");assert(action.kind==BADGE_ACTION_NONE);
    badge_ui_key(BADGE_KEY_UP,true);
    status.provisioning=false;badge_ui_tick(&status);badge_ui_show_provision();save("wifi-after-fetch");
    assert_text_ink("FETCH IN PROGRESS",19,78,202,0x2ee4a4);
    assert_text_ink("SETUP AFTER FETCH",28,151,184,0x91a89b);
    badge_ui_close_provision();status.syncing=false;data.sync_message[0]=0;badge_ui_update(&data,&config,&status);
    /* A bad/inconsistent model cannot overflow a chart coordinate or timestamp. */
    data.pr_total=data.issue_total=1;
    data.pr_open=data.pr_merged=data.pr_closed=data.issue_open=data.issue_closed=UINT32_MAX;
    /* 222 * this count narrows to INT32_MAX-1 before an unsafe int clamp. */
    data.pr_merged=data.issue_closed=1692836209U;
    data.last_success=INT64_MAX;
    badge_ui_update(&data,&config,&status);
    for(unsigned i=0;i<4;i++)badge_ui_key(BADGE_KEY_DOWN,true);
    badge_ui_key(BADGE_KEY_OK,false);
    save("invalid-counts-pr");
    uint32_t invalid_clock=frame_hash();data.last_success=1;
    badge_ui_update(&data,&config,&status);lv_refr_now(display);assert(frame_hash()==invalid_clock);
    badge_ui_key(BADGE_KEY_DOWN,false);badge_ui_key(BADGE_KEY_OK,false);save("invalid-counts-issues");
    status.now=INT64_MAX;badge_ui_tick(&status);save("invalid-clock");
    puts("battery, language, notification, and provisioning render checks passed");
    puts("native LVGL render complete");return 0;
}
