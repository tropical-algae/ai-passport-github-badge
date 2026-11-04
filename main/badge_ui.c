#include "badge_ui.h"
#include "badge_ui_state.h"
#include "badge_ui_avatar.h"
#include "badge_sync_parse.h"
#include "lvgl.h"
#include "src/draw/lv_image_decoder_private.h"
#include "src/misc/cache/instance/lv_image_cache.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

LV_FONT_DECLARE(badge_font_8);
LV_FONT_DECLARE(badge_font_10);
LV_FONT_DECLARE(badge_font_14);
LV_FONT_DECLARE(badge_font_22);

/* The v20 reference is deliberately painted in one widget: selection is an
 * inset overlay, and changing focus never changes any row's geometry. */
typedef struct {
    const char *name;
    uint32_t accent, background, focus;
    uint32_t line, track, heat0, heat1, heat2, muted_accent, avatar, tab, muted;
    uint32_t palette[8];
} badge_theme_t;

static const badge_theme_t themes[] = {
    {"PHOSPHOR GREEN",0x2ee4a4,0x080f0b,0x194532,
     0x244235,0x1b3529,0x192b21,0x1c513d,0x248d66,0x27896b,0x163a2b,0x1b3b2c,0x91a89b,
     {0x2ee4a4,0x589eff,0xf6ca54,0x49d9d0,0x75d77c,0xf17b5c,0xa58bf4,0xa6b7c6}},
    {"INDUSTRIAL YELLOW",0xf5c84c,0x11100a,0x51431d,
     0x51452c,0x302919,0x211d13,0x4a3820,0x84602c,0xad863d,0x302719,0x3b321e,0xb3a88b,
     {0xf5c84c,0x8cb3cf,0xe88b46,0xe3d28a,0xe4a96a,0xcb6760,0xa99bc4,0xc0b9a4}},
    {"ICE CYAN",0x67dce7,0x081319,0x173d48,
     0x294750,0x183039,0x111f28,0x1a4b55,0x2a8999,0x3a9ca6,0x183640,0x1d3942,0x9aafba,
     {0x67dce7,0x799df1,0xa5c9ee,0x63b4ad,0xa9e5d3,0xf1a978,0xc6a3e8,0x94afc7}},
    {"AMETHYST",0xbc9aff,0x100c19,0x352448,
     0x443652,0x2a2237,0x1b1526,0x39254e,0x7253a0,0x8065ac,0x30243c,0x392a48,0xb4a6c6,
     {0xbc9aff,0x8aaff2,0xf0b1d8,0xa3a0df,0xd7b7ff,0xf0a48d,0x88bdb9,0xb8b1c8}},
    {"COPPER ROSE",0xe9a18b,0x160e10,0x4b2c2f,
     0x543b3d,0x332327,0x23171c,0x503039,0x98636b,0xad7272,0x38262a,0x443034,0xbfa8a5,
     {0xe9a18b,0xc1a6d7,0xe8bd8b,0xd58c98,0xf2c6a6,0xa6bcb1,0xbd8fae,0xc8b6ae}}
};

static const char *page_names[] = {"PROFILE","REPOS","LANGUAGES","ACTIVITY","PR/ISSUES","STARS/GISTS","TIMELINE"};
static const char *page_titles[] = {"PROFILE","REPOSITORIES","LANGUAGES","ACTIVITY","PR / ISSUES","STARS / GISTS","TIMELINE"};
static badge_ui_state_t state;
/* Application owns this stable storage and holds the LVGL lock for every
 * mutation, including snapshot publication and account replacement. */
static const badge_data_t *data;
static badge_config_t config;
static badge_status_t status;
static lv_obj_t *screen, *qr;
static lv_image_dsc_t avatar;
static bool avatar_valid, qr_valid;
static char qr_content[192];
static badge_modal_t qr_modal;
static uint32_t toast_until;
static char toast_text[128];
static const char *toast_detail;
static bool toast_error;

static const badge_theme_t *theme(void) { return &themes[config.theme < 5 ? config.theme : 0]; }
static uint32_t mixed(uint32_t a, uint32_t b, unsigned percent)
{
    uint32_t out = 0;
    for (unsigned shift = 0; shift < 24; shift += 8)
        out |= ((((a >> shift) & 255) * percent + ((b >> shift) & 255) * (100 - percent)) / 100) << shift;
    return out;
}

static void rect(lv_layer_t *layer, int x, int y, int w, int h, uint32_t color)
{
    if (w <= 0 || h <= 0) return;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_hex(color);
    d.bg_opa = LV_OPA_COVER;
    lv_area_t a = {x,y,x+w-1,y+h-1};
    lv_draw_rect(layer, &d, &a);
}

static void frame(lv_layer_t *layer, int x, int y, int w, int h, int thickness, uint32_t color)
{
    rect(layer,x,y,w,thickness,color);
    rect(layer,x,y+h-thickness,w,thickness,color);
    rect(layer,x,y,thickness,h,color);
    rect(layer,x+w-thickness,y,thickness,h,color);
}

static void focus(lv_layer_t *layer, int x, int y, int w, int h, uint32_t accent, uint32_t fill)
{
    rect(layer,x,y,w,h,fill);
    frame(layer,x,y,w,h,2,accent);
    rect(layer,x,y,5,5,accent); rect(layer,x+w-5,y,5,5,accent);
    rect(layer,x,y+h-5,5,5,accent); rect(layer,x+w-5,y+h-5,5,5,accent);
}

static size_t previous_utf8(const char *s, size_t length)
{
    if (!length) return 0;
    --length;
    while (length && ((unsigned char)s[length] & 0xc0) == 0x80) --length;
    return length;
}

static void text(lv_layer_t *layer, int x, int y, int width, const char *value,
                 const lv_font_t *font, uint32_t color, lv_text_align_t align)
{
    if (!value || width <= 0) return;
    char fitted[196];
    size_t input = strlen(value), length = input < sizeof(fitted)-4 ? input : sizeof(fitted)-4;
    if (length < input) while (length && ((unsigned char)value[length] & 0xc0) == 0x80) --length;
    memcpy(fitted,value,length);
    fitted[length] = 0;
    for (size_t i = 0; i < length; ++i) if (fitted[i] == '\n' || fitted[i] == '\r' || fitted[i] == '\t') fitted[i] = ' ';
    lv_point_t size;
    lv_text_get_size(&size,fitted,font,0,0,LV_COORD_MAX,LV_TEXT_FLAG_EXPAND);
    bool shortened = length < input;
    while (length && size.x > width) {
        length = previous_utf8(fitted,length);
        fitted[length] = 0;
        shortened = true;
        char trial[196];
        memcpy(trial,fitted,length);
        memcpy(trial+length,"...",4);
        lv_text_get_size(&size,trial,font,0,0,LV_COORD_MAX,LV_TEXT_FLAG_EXPAND);
    }
    if (shortened && length + 3 < sizeof(fitted)) memcpy(fitted+length,"...",4);
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.font = font; d.color = lv_color_hex(color); d.text = fitted;
    d.text_local = 1; /* draw tasks outlive this stack buffer */
    d.flag = LV_TEXT_FLAG_EXPAND; d.align = align;
    lv_area_t a = {x,y,x+width-1,y+(int)font->line_height+3};
    lv_draw_label(layer,&d,&a);
}

#define TXT(l,x,y,w,s,f,c) text((l),(x),(y),(w),(s),(f),(c),LV_TEXT_ALIGN_LEFT)
#define RIGHT(l,x,y,w,s,f,c) text((l),(x),(y),(w),(s),(f),(c),LV_TEXT_ALIGN_RIGHT)
#define CENTER(l,x,y,w,s,f,c) text((l),(x),(y),(w),(s),(f),(c),LV_TEXT_ALIGN_CENTER)

/* These short ASCII labels fit their boxes. Center their visible glyph bounds,
 * using the same baseline-to-bitmap placement as LVGL's label renderer. */
static void center_ascii(lv_layer_t *l, int x, int y, int width, int height,
                         const char *value, const lv_font_t *font, uint32_t color)
{
    int ink_top=0, ink_bottom=0;
    bool visible=false;
    for(const unsigned char *p=(const unsigned char *)value; *p; ++p) {
        if(*p==' ')continue;
        lv_font_glyph_dsc_t glyph;
        lv_font_get_glyph_dsc(font,&glyph,*p,p[1]);
        if(!glyph.box_w || !glyph.box_h)continue;
        int top=(int)font->line_height-font->base_line-glyph.box_h-glyph.ofs_y;
        int bottom=top+glyph.box_h;
        if(!visible || top<ink_top)ink_top=top;
        if(!visible || bottom>ink_bottom)ink_bottom=bottom;
        visible=true;
    }
    if(visible)y+=(height-(ink_bottom-ink_top))/2-ink_top;
    CENTER(l,x,y,width,value,font,color);
}

static void section(lv_layer_t *l, int y, const char *name)
{
    const badge_theme_t *t = theme();
    rect(l,9,y+3,3,3,t->accent);
    lv_point_t size;
    lv_text_get_size(&size,name,&badge_font_8,0,0,LV_COORD_MAX,LV_TEXT_FLAG_EXPAND);
    TXT(l,17,y,209,name,&badge_font_8,t->muted);
    if (size.x < 200) rect(l,22+size.x,y+4,209-size.x,1,t->line);
}

static void number(uint32_t value, bool valid, char out[20])
{
    if (valid) badge_format_number(value,out,20);
    else snprintf(out,20,"--");
}

static void stats(lv_layer_t *l, int y, const uint32_t values[3],
                  const bool valid[3], const char *const labels[3])
{
    const badge_theme_t *t = theme();
    rect(l,9,y,222,1,t->line); rect(l,9,y+43,222,1,t->line);
    for (unsigned i=0; i<3; ++i) {
        char value[20]; number(values[i],valid[i],value);
        int x = 13 + 74*(int)i;
        TXT(l,x,y+7,66,value,&badge_font_14,0xffffff);
        TXT(l,x,y+28,66,labels[i],&badge_font_8,t->muted);
        if (i<2) rect(l,82+74*(int)i,y+1,1,42,t->line);
    }
}

static void micros(lv_layer_t *l, const char *const values[3], const char *const labels[3],
                   const uint32_t colors[3])
{
    for (unsigned i=0; i<3; ++i) {
        int x=9+76*(int)i;
        TXT(l,x,119,69,values[i],&badge_font_14,colors[i]);
        lv_point_t size;
        lv_text_get_size(&size,labels[i],&badge_font_8,0,0,LV_COORD_MAX,LV_TEXT_FLAG_EXPAND);
        const char *space=strrchr(labels[i],' ');
        if(size.x>69 && space) {
            char first[32];size_t length=(size_t)(space-labels[i]);
            if(length>=sizeof(first))length=sizeof(first)-1;
            memcpy(first,labels[i],length);first[length]=0;
            TXT(l,x,142,69,first,&badge_font_8,theme()->muted);
            TXT(l,x,153,69,space+1,&badge_font_8,theme()->muted);
        } else TXT(l,x,142,69,labels[i],&badge_font_8,theme()->muted);
    }
}

static void tabs(lv_layer_t *l, const char *first, const char *second, uint8_t active)
{
    const badge_theme_t *t=theme();
    const char *names[2]={first,second};
    for (unsigned i=0; i<2; ++i) {
        int x=9+(int)i*111;
        rect(l,x,60,111,28,i==active?t->accent:t->tab);
        CENTER(l,x+3,69,105,names[i],&badge_font_8,i==active?t->background:0xffffff);
        if (i==state.focus && i!=active) {
            for (int dx=4; dx<107; dx+=6) {
                rect(l,x+dx,63,3,1,t->accent); rect(l,x+dx,84,3,1,t->accent);
            }
            for (int dy=4; dy<24; dy+=6) {
                rect(l,x+3,60+dy,1,3,t->accent); rect(l,x+107,60+dy,1,3,t->accent);
            }
        }
    }
    frame(l,9,60,222,28,1,t->accent);
}

static void draw_avatar(lv_layer_t *l)
{
    const badge_theme_t *t=theme();
    rect(l,13,126,54,54,t->avatar);
    if (avatar_valid) {
        lv_draw_image_dsc_t d;
        lv_draw_image_dsc_init(&d);
        d.src=&avatar; d.antialias=0;
        lv_area_t a={13,126,66,179};
        lv_draw_image(l,&d,&a);
    } else {
        char initial[2]={(char)toupper((unsigned char)(config.username[0]?config.username[0]:'?')),0};
        center_ascii(l,13,126,54,54,initial,&badge_font_22,t->accent);
    }
    for (int side=0;side<2;++side) for (int bottom=0;bottom<2;++bottom) {
        int x=9+side*51, y=122+bottom*60;
        rect(l,x,y,11,2,t->accent);
        rect(l,9+side*60,122+bottom*51,2,11,t->accent);
    }
}

static void profile(lv_layer_t *l)
{
    const badge_theme_t *t=theme();
    bool valid=(data->valid&BADGE_PROFILE)!=0;
    TXT(l,9,57,222,valid?data->username:(config.username[0]?config.username:"NO DATA"),&badge_font_22,0xffffff);
    TXT(l,9,87,222,data->name[0]?data->name:"GITHUB DEVELOPER",&badge_font_10,t->accent);
    section(l,108,"IDENTITY"); draw_avatar(l);
    char account[44]; snprintf(account,sizeof(account),"@%.39s",data->username[0]?data->username:"username");
    TXT(l,80,125,151,account,&badge_font_8,0xffffff);
    TXT(l,80,140,151,data->company[0]?data->company:"NO COMPANY",&badge_font_8,t->muted);
    TXT(l,80,155,151,data->location[0]?data->location:"LOCATION N/A",&badge_font_8,0xffffff);
    TXT(l,80,170,151,data->blog[0]?data->blog:"NO WEBSITE",&badge_font_8,t->accent);
    section(l,197,"OVERVIEW");
    const uint32_t values[]={data->public_repos,data->followers,data->following};
    const bool good[]={valid,valid,valid};
    const char *labels[]={"REPOS","FOLLOWERS","FOLLOWING"};
    stats(l,211,values,good,labels);
    TXT(l,9,263,70,"JOINED",&badge_font_8,t->muted);
    RIGHT(l,88,263,143,data->joined[0]?data->joined:"--",&badge_font_8,0xffffff);
}

static void repositories(lv_layer_t *l)
{
    const badge_theme_t *t=theme();
    bool valid=(data->valid&BADGE_REPOSITORIES)!=0 && data->repo_count;
    unsigned count=data->repo_count<3?data->repo_count:3;
    const badge_repo_t *repo=&data->repos[state.focus<count?state.focus:0];
    const uint32_t values[]={repo->stars,repo->forks,repo->watchers};
    const bool good[]={valid,valid,valid&&repo->watchers_valid};
    const char *labels[]={"STARS","FORKS","WATCHERS"};
    stats(l,61,values,good,labels);
    section(l,117,(data->complete&BADGE_REPOSITORIES)?"TOP 3 BY STARS":"TOP 3 / FETCHED REPOS");
    for (unsigned i=0;i<count;++i) {
        int y=133+(int)i*43;
        if (i==state.focus) focus(l,9,y,222,42,t->accent,t->focus);
        else rect(l,9,y+41,222,1,t->line);
        char name[110], license[44];
        snprintf(name,sizeof(name),"%u. %s",i+1,data->repos[i].name);
        snprintf(license,sizeof(license),"LICENSE: %s",data->repos[i].license[0]?data->repos[i].license:"NONE");
        TXT(l,15,y+8,151,name,&badge_font_10,0xffffff);
        const char *date=data->repos[i].pushed_at;
        RIGHT(l,167,y+9,58,strlen(date)==10?date+2:"--",&badge_font_8,t->muted);
        TXT(l,15,y+25,210,license,&badge_font_8,t->muted);
    }
    if (!count) TXT(l,9,143,222,valid?"NO REPOSITORIES":"WAITING FOR REPOSITORIES",&badge_font_8,t->muted);
}

static void donut(lv_layer_t *l, const badge_scope_t *scope)
{
    const badge_theme_t *t=theme();
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.center=(lv_point_t){50,227}; arc.radius=41; arc.width=16;
    arc.start_angle=0; arc.end_angle=360; arc.color=lv_color_hex(t->track);
    lv_draw_arc(l,&arc);
    uint64_t cumulative=0;
    int previous=270;
    for (unsigned i=0;i<scope->count && i<BADGE_LANGS;++i) {
        cumulative+=scope->languages[i].bytes;
        int end=270+(int)(scope->total_bytes?360*cumulative/scope->total_bytes:0);
        if (end>previous) {
            arc.start_angle=previous; arc.end_angle=end;
            arc.color=lv_color_hex(t->palette[i==5?7:i]);
            lv_draw_arc(l,&arc);
        }
        previous=end;
        frame(l,103,188+(int)i*14,9,9,2,t->palette[i==5?7:i]);
        TXT(l,117,188+(int)i*14,114,scope->languages[i].name,&badge_font_8,0xffffff);
    }
    char leading[16];
    if (scope->valid && scope->total_bytes && scope->count)
        snprintf(leading,sizeof(leading),"%u%%",(unsigned)((100*scope->languages[0].bytes+scope->total_bytes/2)/scope->total_bytes));
    else snprintf(leading,sizeof(leading),"--");
    center_ascii(l,28,215,44,15,leading,&badge_font_14,0xffffff);
    /* Keep both lines inside the 25px inner radius, including long names. */
    CENTER(l,33,228,34,scope->count?scope->languages[0].name:"NONE",&badge_font_8,t->accent);
}

static void languages(lv_layer_t *l)
{
    const badge_theme_t *t=theme();
    const badge_scope_t *scope=&data->scopes[state.language];
    unsigned color=state.language==0?0:state.language==1?1:6;
    uint32_t accent=t->palette[color];
    focus(l,9,60,222,27,accent,mixed(accent,t->background,24));
    const char *name=scope->title[0]?scope->title:state.language==0?"ORIGINAL ONLY":"ALL REPOS";
    TXT(l,16,69,177,name,&badge_font_8,accent);
    RIGHT(l,199,69,24,"< >",&badge_font_8,accent);
    section(l,98,"TOP LANGUAGES");
    for (unsigned i=0;i<3 && i<scope->count;++i) {
        int y=115+(int)i*18;
        TXT(l,9,y,64,scope->languages[i].name,&badge_font_8,0xffffff);
        rect(l,78,y+1,108,8,t->track);
        rect(l,78,y+1,scope->total_bytes?(int)(108*scope->languages[i].bytes/scope->total_bytes):0,8,t->palette[i]);
        char percent[20];
        uint64_t p=scope->total_bytes?1000*scope->languages[i].bytes/scope->total_bytes:0;
        snprintf(percent,sizeof(percent),"%u.%u%%",(unsigned)(p/10),(unsigned)(p%10));
        RIGHT(l,189,y,42,percent,&badge_font_8,t->muted);
    }
    if (!scope->count) TXT(l,9,121,222,scope->valid?"NO LANGUAGE DATA":"COLLECTING LANGUAGES",&badge_font_8,t->muted);
    section(l,172,"DISTRIBUTION"); donut(l,scope);
}

static void activity(lv_layer_t *l)
{
    const badge_theme_t *t=theme();
    bool valid=(data->valid&BADGE_ACTIVITY)!=0;
    const uint32_t values[]={data->events_fetched,data->push_events,data->push_days};
    const bool good[]={valid,valid,valid};
    const char *labels[]={"EVENTS","PUSH EVT","PUSH DAYS"};
    stats(l,61,values,good,labels);
    section(l,116,"24 WEEKS / SUN-SAT / OBSERVED");
    uint32_t weeks[12]={0};
    int32_t today=(int32_t)(status.now/86400);
    for (unsigned i=0;i<BADGE_DAYS;++i) {
        const badge_day_t *day=&data->days[i];
        int x=9+(int)(i/7)*9, y=132+(int)(i%7)*10;
        uint32_t color=day->pushes==0?t->heat0:day->pushes==1?t->heat1:day->pushes==2?t->heat2:t->accent;
        if (!day->day || day->day>today) rect(l,x,y,7,8,mixed(t->heat0,t->background,25));
        else if (day->coverage==BADGE_UNKNOWN) {
            frame(l,x,y,7,8,1,t->line); rect(l,x+3,y+3,1,2,t->muted);
        } else {
            /* Keep the reference's solid heat cells. Partial coverage uses a
             * dimmer fill instead of a pale underline that looks like damage. */
            if (day->coverage==BADGE_PARTIAL) color=mixed(color,t->background,60);
            rect(l,x,y,7,8,color);
        }
        if (i>=84 && day->day<=today) weeks[(i-84)/7]+=day->pushes;
    }
    TXT(l,9,205,125,". UNKNOWN  DIM PARTIAL",&badge_font_8,t->muted);
    RIGHT(l,160,205,71,"NOW",&badge_font_8,t->muted);
    section(l,221,"RECENT 12 WEEKS / PUSH EVT");
    uint32_t maximum=1;
    for (unsigned i=0;i<12;++i) if (weeks[i]>maximum) maximum=weeks[i];
    rect(l,9,274,222,1,t->line);
    for (unsigned i=0;i<12;++i) {
        int h=(int)(39*weeks[i]/maximum); if(h<1)h=1;
        rect(l,9+(int)i*18,274-h,15,h,t->palette[i%3==0?1:0]);
    }
}

static void pulls_issues(lv_layer_t *l)
{
    const badge_theme_t *t=theme();
    bool pr=state.pr_tab==0, valid=(data->valid&(pr?BADGE_PULLS:BADGE_ISSUES))!=0;
    tabs(l,"PULL REQUESTS","ISSUES",state.pr_tab);
    section(l,101,"CREATED BY USER");
    uint32_t total=pr?data->pr_total:data->issue_total, open=pr?data->pr_open:data->issue_open;
    uint32_t closed=pr?(data->pr_closed>data->pr_merged?data->pr_closed-data->pr_merged:0):data->issue_closed;
    uint32_t primary=pr?data->pr_merged:closed;
    char a[20], b[20], c[20]; number(total,valid,a); number(open,valid,c);
    if(valid&&total&&primary<=total)snprintf(b,sizeof(b),"%u%%",(unsigned)((uint64_t)100*primary/total)); else snprintf(b,sizeof(b),"--");
    const char *values[]={a,b,c};
    const char *labels[]={pr?"TOTAL PRS":"TOTAL ISSUES",pr?"MERGE RATE":"CLOSE RATE","CURRENTLY OPEN"};
    const uint32_t colors[]={0xffffff,t->palette[0],t->palette[1]};
    micros(l,values,labels,colors);
    section(l,174,"STATUS DISTRIBUTION");
    uint32_t counts[]={pr?data->pr_merged:closed,pr?closed:open,open};
    unsigned count=pr?3:2;
    uint32_t palette[]={t->palette[0],t->palette[pr?5:1],t->palette[1]};
    const char *names[]={pr?"MERGED":"CLOSED",pr?"CLOSED":"OPEN","OPEN"};
    rect(l,9,190,222,13,t->track);
    if(valid&&total) {
        int x=9;
        for(unsigned i=0;i<count;++i) {
            uint64_t scaled_width=(uint64_t)222*counts[i]/total;
            unsigned remaining=(unsigned)(231-x);
            int width=(int)(scaled_width>remaining?remaining:scaled_width);
            rect(l,x,190,width,13,palette[i]); x+=width;
            int y=215+(int)i*20;
            rect(l,9,y+1,8,8,palette[i]); TXT(l,23,y,91,names[i],&badge_font_8,0xffffff);
            char amount[20], formatted[44]; number(counts[i],true,amount);
            if(counts[i]<=total) {
                uint32_t percent=(uint32_t)((uint64_t)1000*counts[i]/total);
                snprintf(formatted,sizeof(formatted),"%s / %u.%u%%",amount,(unsigned)(percent/10),(unsigned)(percent%10));
            } else snprintf(formatted,sizeof(formatted),"%s / --",amount);
            RIGHT(l,112,y,119,formatted,&badge_font_8,0xffffff);
        }
    } else TXT(l,9,218,222,valid?"NO DATA":"WAITING FOR SEARCH DATA",&badge_font_8,t->muted);
}

static void stars_gists(lv_layer_t *l)
{
    const badge_theme_t *t=theme();
    bool star=state.star_tab==0, valid=(data->valid&(star?BADGE_STARS:BADGE_GISTS))!=0;
    tabs(l,"STARRED","GISTS",state.star_tab);
    section(l,101,star?"FETCHED PUBLIC STARS":"PUBLIC GISTS");
    char a[20],b[20],c[20];
    number(star?data->starred_fetched:data->public_gists,star?valid:(data->valid&BADGE_PROFILE)!=0,a);
    number(star?data->starred_with_language:data->gists_fetched,valid,b);
    number(star?data->starred_languages:data->gists_public,valid,c);
    const char *values[]={a,b,c};
    const char *labels[]={star?"STARRED":"TOTAL GISTS",star?"WITH LANGUAGE":"FETCHED GISTS",star?"LANGUAGES":"PUBLIC FETCHED"};
    const uint32_t colors[]={0xffffff,t->palette[2],t->palette[3]};
    micros(l,values,labels,colors);
    section(l,174,star?"STARRED REPOSITORIES":"RECENT GISTS");
    unsigned count=star?data->star_count:data->gist_count; if(count>2)count=2;
    for(unsigned i=0;i<count;++i) {
        int y=192+(int)i*38;
        TXT(l,9,y,222,star?data->stars[i].name:data->gists[i].description,&badge_font_10,t->palette[star?2:3]);
        char detail[80];
        if(star) {
            char n[20]; number(data->stars[i].stars,true,n);
            snprintf(detail,sizeof(detail),"%s / *%s",data->stars[i].language[0]?data->stars[i].language:"UNKNOWN",n);
        } else snprintf(detail,sizeof(detail),"%s",data->gists[i].updated_at);
        TXT(l,9,y+16,222,detail,&badge_font_8,t->muted);
        rect(l,9,y+31,222,1,t->line);
    }
    if(!count) TXT(l,9,197,222,valid?"NO DATA":"WAITING FOR DATA",&badge_font_8,t->muted);
}

static unsigned event_color(const char *type)
{
    if(!strcmp(type,"PushEvent"))return 0;
    if(!strcmp(type,"PullRequestEvent"))return 4;
    if(!strcmp(type,"IssuesEvent"))return 5;
    if(!strcmp(type,"CreateEvent"))return 2;
    if(!strcmp(type,"WatchEvent"))return 1;
    if(!strcmp(type,"ForkEvent"))return 3;
    if(!strcmp(type,"ReleaseEvent"))return 6;
    return 7;
}

static void timeline(lv_layer_t *l)
{
    const badge_theme_t *t=theme();
    section(l,61,"RECENT PUBLIC EVENTS");
    for(unsigned i=0;i<data->event_count && i<BADGE_EVENTS;++i) {
        const badge_event_t *event=&data->events[i];
        int y=79+(int)i*32;
        uint32_t color=t->palette[event_color(event->type)];
        rect(l,15,y,1,32,color); rect(l,12,y+1,7,7,color);
        char type[32]; snprintf(type,sizeof(type),"%s",event->type);
        char *suffix=strstr(type,"Event"); if(suffix)*suffix=0;
        for(char *p=type;*p;++p)*p=(char)toupper((unsigned char)*p);
        TXT(l,27,y,158,type,&badge_font_8,color);
        RIGHT(l,190,y,41,strlen(event->date)==10?event->date+5:"--",&badge_font_8,color);
        TXT(l,27,y+14,204,event->repo,&badge_font_8,0xffffff);
    }
    if(!data->event_count) TXT(l,9,84,222,"NO RECENT PUBLIC EVENTS",&badge_font_8,t->muted);
}

static void settings(lv_layer_t *l)
{
    const badge_theme_t *t=theme();
    unsigned count=state.settings==BADGE_SETTINGS_MAIN || state.settings==BADGE_SETTINGS_THEME?5:6;
    for(unsigned i=0;i<count;++i) {
        int y=61+(int)i*31;
        uint32_t color=t->accent;
        if(i==state.focus)focus(l,9,y,222,30,t->accent,t->focus);
        else rect(l,9,y+29,222,1,t->line);
        char value[64];
        if(state.settings==BADGE_SETTINGS_MAIN) {
            const char *names[]={"BRIGHTNESS","AUTO SCREEN OFF","COLOR THEME","CONNECT WI-FI","REFETCH DATA"};
            if(i==0)snprintf(value,sizeof(value),"> %s  %u%%",names[i],config.brightness);
            else if(i==1) {
                char timeout[12];
                if(config.screen_timeout_min)snprintf(timeout,sizeof(timeout),"%uM",config.screen_timeout_min);
                else snprintf(timeout,sizeof(timeout),"NEVER");
                snprintf(value,sizeof(value),"> %s",names[i]);
                TXT(l,16,y+10,163,value,&badge_font_10,color);
                RIGHT(l,181,y+10,43,timeout,&badge_font_10,color);
                continue;
            }
            else snprintf(value,sizeof(value),"> %s",names[i]);
        } else if(state.settings==BADGE_SETTINGS_SCREEN_TIMEOUT) {
            const char *names[]={"1 MINUTE","5 MINUTES","10 MINUTES","30 MINUTES","1 HOUR","NEVER"};
            snprintf(value,sizeof(value),"%s %s",config.screen_timeout_min==badge_screen_timeout_minutes[i]?"[X]":"[ ]",names[i]);
        } else if(state.settings==BADGE_SETTINGS_BRIGHTNESS)snprintf(value,sizeof(value),"%s %u%%",config.brightness==badge_brightness_percent[i]?"[X]":"[ ]",badge_brightness_percent[i]);
        else {snprintf(value,sizeof(value),"%s %s",config.theme==i?"[X]":"[ ]",themes[i].name);color=themes[i].accent;}
        TXT(l,16,y+10,208,value,&badge_font_10,color);
    }
    if(state.settings==BADGE_SETTINGS_MAIN) {
        section(l,227,"DEVICE");
        TXT(l,9,243,222,"SYNC CONTINUES IN BACKGROUND",&badge_font_8,t->muted);
        TXT(l,9,256,222,"HOLD OK TO EXIT SETTINGS",&badge_font_8,t->muted);
    } else TXT(l,9,256,222,"OK SELECT / HOLD OK EXIT",&badge_font_8,t->muted);
}

static void clock_string(int64_t epoch,char *out,size_t size)
{
    if(epoch<1577836800 || epoch>253402300799LL) {snprintf(out,size,"--/-- --:--");return;}
    time_t value=(time_t)(epoch+(int64_t)config.utc_offset_min*60);
    struct tm tm={0};
    if(!gmtime_r(&value,&tm) || !strftime(out,size,"%m/%d %H:%M",&tm))
        snprintf(out,size,"--/-- --:--");
}

static void chrome(lv_layer_t *l)
{
    const badge_theme_t *t=theme();
    char heading[64],battery[8],clock[24];
    if(state.settings!=BADGE_SETTINGS_CLOSED)snprintf(heading,sizeof(heading),"GITHUB / SETTINGS");
    else snprintf(heading,sizeof(heading),"GITHUB / %s - %02u",page_names[state.page],state.page+1);
    rect(l,8,11,5,5,status.connected?t->accent:t->muted);
    TXT(l,18,9,161,heading,&badge_font_8,t->muted);
    int level=status.battery_percent;
    if(level>=0) {if(level>100)level=100;snprintf(battery,sizeof(battery),"%d%%",level);}
    else snprintf(battery,sizeof(battery),"--%%");
    lv_point_t battery_size;
    lv_text_get_size(&battery_size,battery,&badge_font_8,0,0,LV_COORD_MAX,LV_TEXT_FLAG_EXPAND);
    int battery_text_x=233-battery_size.x;
    int battery_icon_x=battery_text_x-3-19;
    frame(l,battery_icon_x,10,17,9,1,t->muted);rect(l,battery_icon_x+17,13,2,3,t->muted);
    if(level>=0)rect(l,battery_icon_x+2,12,13*level/100,5,t->accent);
    TXT(l,battery_text_x,9,battery_size.x,battery,&badge_font_8,0xffffff);
    rect(l,0,26,240,1,t->line); rect(l,0,293,240,1,t->line);
    rect(l,13,304,1,1,t->muted);rect(l,12,305,3,1,t->muted);rect(l,11,306,5,1,t->muted);
    TXT(l,20,303,43,"PREV",&badge_font_8,t->muted);
    RIGHT(l,177,303,43,"NEXT",&badge_font_8,t->muted);
    rect(l,224,304,5,1,t->muted);rect(l,225,305,3,1,t->muted);rect(l,226,306,1,1,t->muted);
    clock_string(status.now,clock,sizeof(clock));
    CENTER(l,66,303,108,clock,&badge_font_8,t->accent);
    const char *title=state.settings==BADGE_SETTINGS_CLOSED?page_titles[state.page]:state.settings==BADGE_SETTINGS_MAIN?"SETTINGS":state.settings==BADGE_SETTINGS_THEME?"COLOR THEME":state.settings==BADGE_SETTINGS_SCREEN_TIMEOUT?"AUTO SCREEN OFF":"BRIGHTNESS";
    char title_line[40];snprintf(title_line,sizeof(title_line),"> %s",title);
    TXT(l,9,35,222,title_line,&badge_font_14,t->accent);
}

static bool waiting_to_retry(int64_t now)
{
    return now>=1577836800 && data->retry_after>now;
}

static void status_line(lv_layer_t *l)
{
    const badge_theme_t *t=theme();
    char message[96];
    uint32_t section_bits[]={BADGE_PROFILE,BADGE_REPOSITORIES,BADGE_LANGUAGES,BADGE_ACTIVITY,
        state.pr_tab?BADGE_ISSUES:BADGE_PULLS,state.star_tab?BADGE_GISTS:BADGE_STARS,BADGE_ACTIVITY};
    uint32_t bit=section_bits[state.page];
    bool partial=(data->valid&bit) && !(data->complete&bit);
    if(status.syncing)snprintf(message,sizeof(message),"%s",data->sync_message[0]?data->sync_message:"SYNCING PUBLIC DATA...");
    else if(!status.connected)snprintf(message,sizeof(message),"OFFLINE / %s",data->valid?"CACHED DATA":"NO CACHED DATA");
    else if(status.now<1577836800)snprintf(message,sizeof(message),"WAITING FOR CLOCK / FIRST SYNC");
    else if(waiting_to_retry(status.now)) {
        char remaining[20];badge_format_wait(data->retry_after,status.now,remaining,sizeof(remaining));
        const char *reason=data->sync_message;
        while(!strncmp(reason,"PARTIAL / ",10))reason+=10;
        snprintf(message,sizeof(message),"%s %s / %.63s",strstr(reason,"RATE LIMIT")?"LIMIT":"RETRY",
                 remaining,reason[0]?reason:"AUTOMATIC RETRY");
    }
    else if(strstr(data->sync_message,"PARTIAL") || strstr(data->sync_message,"NOT SAVED") ||
            strstr(data->sync_message,"STORAGE ERROR"))snprintf(message,sizeof(message),"%s",data->sync_message);
    else if(state.page==2 && state.settings==BADGE_SETTINGS_CLOSED) {
        const badge_scope_t *scope=&data->scopes[state.language];
        snprintf(message,sizeof(message),"%s%u/%u REPOS / %s",status.connected?"":"OFFLINE / ",
                 (unsigned)scope->fetched_repos,(unsigned)scope->total_repos,scope->complete?"READY":"PARTIAL");
    } else if(partial)snprintf(message,sizeof(message),"PARTIAL PUBLIC DATA / CACHED");
    else if(data->last_success) {char date[24];clock_string(data->last_success,date,sizeof(date));snprintf(message,sizeof(message),"SYNC %s",date);}
    else snprintf(message,sizeof(message),"%s",status.message[0]?status.message:"WAITING FOR FIRST SYNC");
    TXT(l,9,281,222,message,&badge_font_8,t->muted);
}

static void modal(lv_layer_t *l)
{
    const badge_theme_t *t=theme();
    bool setup_waiting=state.modal==BADGE_MODAL_WIFI && !status.provisioning && status.syncing;
    rect(l,0,27,240,266,t->background);
    rect(l,12,46,222,240,0x000000);
    rect(l,9,43,222,240,t->background); frame(l,9,43,222,240,2,t->accent);
    TXT(l,20,57,200,state.modal==BADGE_MODAL_PROFILE?"GITHUB PROFILE":"WI-FI SETUP",&badge_font_14,t->accent);
    if(state.modal==BADGE_MODAL_PROFILE) {
        char account[44];snprintf(account,sizeof(account),"@%.39s",config.username);
        CENTER(l,19,230,202,account,&badge_font_10,t->accent);
        CENTER(l,19,249,202,"SCAN TO MEET ON GITHUB",&badge_font_8,t->muted);
    } else {
        CENTER(l,19,78,202,status.provisioning&&status.ap_ssid[0]?status.ap_ssid:setup_waiting?"FETCH IN PROGRESS":"STARTING HOTSPOT...",&badge_font_8,t->accent);
        CENTER(l,19,216,202,setup_waiting?"SYNC CONTINUES IN BACKGROUND":"01 SCAN TO JOIN WI-FI",&badge_font_8,t->muted);
        CENTER(l,19,233,202,setup_waiting?"HOTSPOT OPENS WHEN FINISHED":"02 OPEN 192.168.4.1",&badge_font_8,t->accent);
        CENTER(l,19,248,202,setup_waiting?"KEEP BROWSING OR WAIT HERE":"WI-FI / USERNAME / SYNC",&badge_font_8,t->muted);
    }
    if(!qr_valid)CENTER(l,28,151,184,state.modal==BADGE_MODAL_WIFI?(setup_waiting?"SETUP AFTER FETCH":"WAITING FOR HOTSPOT"):"QR UNAVAILABLE",&badge_font_8,t->muted);
    rect(l,19,263,202,12,t->accent);
    center_ascii(l,19,263,202,12,"[OK] CLOSE",&badge_font_8,t->background);
}

static void draw(lv_event_t *event)
{
    lv_layer_t *l=lv_event_get_layer(event);
    chrome(l);
    if(state.modal!=BADGE_MODAL_NONE)modal(l);
    else {
        if(state.settings!=BADGE_SETTINGS_CLOSED)settings(l);
        else switch(state.page) {
        case 0:profile(l);break; case 1:repositories(l);break;case 2:languages(l);break;
        case 3:activity(l);break;case 4:pulls_issues(l);break;case 5:stars_gists(l);break;
        default:timeline(l);break;
        }
        status_line(l);
    }
    if(toast_text[0] && (int32_t)(toast_until-lv_tick_get())>0) {
        uint32_t color=toast_error?theme()->palette[5]:theme()->accent;
        uint32_t background=toast_error?mixed(color,theme()->background,24):theme()->focus;
        int y=toast_detail?255:268, height=toast_detail?36:23;
        rect(l,8,y,224,height,background);frame(l,8,y,224,height,1,color);
        CENTER(l,14,toast_detail?262:275,212,toast_text,&badge_font_8,color);
        if(toast_detail)CENTER(l,14,275,212,toast_detail,&badge_font_8,color);
    }
}

static void update_qr(void)
{
    if(state.modal==BADGE_MODAL_NONE) {
        if(qr)lv_obj_add_flag(qr,LV_OBJ_FLAG_HIDDEN);
        qr_modal=BADGE_MODAL_NONE; return;
    }
    char content[192]={0};
    if(state.modal==BADGE_MODAL_PROFILE)snprintf(content,sizeof(content),"https://github.com/%s",config.username);
    else if(status.provisioning&&status.ap_ssid[0])snprintf(content,sizeof(content),"WIFI:T:WPA;S:%s;P:%s;;",status.ap_ssid,status.ap_password);
    if(!qr) {
        qr=lv_qrcode_create(screen);
        lv_qrcode_set_dark_color(qr,lv_color_black());lv_qrcode_set_light_color(qr,lv_color_white());
        lv_qrcode_set_quiet_zone(qr,true);
    }
    if(qr_modal!=state.modal || strcmp(qr_content,content)) {
        int side=state.modal==BADGE_MODAL_PROFILE?140:116;
        lv_qrcode_set_size(qr,side);lv_obj_set_pos(qr,(240-side)/2,state.modal==BADGE_MODAL_PROFILE?81:94);
        snprintf(qr_content,sizeof(qr_content),"%s",content);qr_modal=state.modal;
        qr_valid=content[0]&&lv_qrcode_update(qr,content,strlen(content))==LV_RESULT_OK;
    }
    if(qr_valid)lv_obj_remove_flag(qr,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(qr,LV_OBJ_FLAG_HIDDEN);
}

void badge_ui_init(const badge_data_t *new_data, const badge_config_t *new_config,
                   const badge_status_t *new_status)
{
    memset(&state,0,sizeof(state));data=new_data;config=*new_config;status=*new_status;
    toast_text[0]=0;toast_detail=NULL;toast_error=false;toast_until=0;
    screen=lv_obj_create(NULL);lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen,240,320);lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);
    lv_obj_set_style_bg_color(screen,lv_color_hex(theme()->background),0);
    lv_obj_remove_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(screen,draw,LV_EVENT_DRAW_MAIN,NULL);
    lv_screen_load(screen);
}

void badge_ui_update(const badge_data_t *new_data, const badge_config_t *new_config,
                     const badge_status_t *new_status)
{
    bool stopped_ap=status.provisioning&&!new_status->provisioning;
    data=new_data;config=*new_config;status=*new_status;
    if(stopped_ap&&state.modal==BADGE_MODAL_WIFI)state.modal=BADGE_MODAL_NONE;
    badge_ui_state_clamp(&state,data);
    lv_obj_set_style_bg_color(screen,lv_color_hex(theme()->background),0);
    update_qr();lv_obj_invalidate(screen);
}

void badge_ui_tick(const badge_status_t *new_status)
{
    bool changed=status.now/60!=new_status->now/60 || status.battery_percent!=new_status->battery_percent ||
                 status.connected!=new_status->connected || status.syncing!=new_status->syncing ||
                 status.provisioning!=new_status->provisioning || strcmp(status.message,new_status->message);
    bool ap_changed=status.provisioning!=new_status->provisioning ||
                    strcmp(status.ap_ssid,new_status->ap_ssid) || strcmp(status.ap_password,new_status->ap_password);
    /* Keep the visible countdown moving, including its transition to ready,
     * without redrawing idle pages on every application tick. */
    if(status.now!=new_status->now && new_status->connected && !new_status->syncing &&
       state.modal==BADGE_MODAL_NONE && (waiting_to_retry(status.now)||waiting_to_retry(new_status->now)))changed=true;
    if(status.provisioning&&!new_status->provisioning&&state.modal==BADGE_MODAL_WIFI)state.modal=BADGE_MODAL_NONE;
    status=*new_status;
    if(ap_changed) {update_qr();changed=true;}
    if(toast_text[0]&&(int32_t)(toast_until-lv_tick_get())<=0) {toast_text[0]=0;changed=true;}
    if(changed)lv_obj_invalidate(screen);
}

badge_action_t badge_ui_key(badge_key_t key, bool long_press)
{
    badge_action_t action=badge_ui_state_key(&state,key,long_press,data,&config);
    if(!long_press && key==BADGE_KEY_OK && state.settings==BADGE_SETTINGS_CLOSED && state.page==2 && state.modal==BADGE_MODAL_NONE) {
        badge_ui_notify("USE UP / DOWN TO CHANGE",false);
    }
    update_qr();lv_obj_invalidate(screen);return action;
}

void badge_ui_show_provision(void)
{
    state.modal=BADGE_MODAL_WIFI;update_qr();lv_obj_invalidate(screen);
}

void badge_ui_close_provision(void)
{
    if(state.modal!=BADGE_MODAL_WIFI)return;
    state.modal=BADGE_MODAL_NONE;update_qr();lv_obj_invalidate(screen);
}

void badge_ui_notify(const char *message,bool error)
{
    size_t length=message?strlen(message):0;
    if(length>=sizeof(toast_text)) {
        length=sizeof(toast_text)-1;
        while(length && ((unsigned char)message[length]&0xc0)==0x80)--length;
    }
    if(length)memcpy(toast_text,message,length);
    toast_text[length]=0;
    char *newline=strchr(toast_text,'\n');toast_detail=NULL;
    if(newline) {
        *newline=0;
        if(newline>toast_text && newline[-1]=='\r')newline[-1]=0;
        if(newline[1])toast_detail=newline+1;
    }
    toast_error=error;toast_until=lv_tick_get()+4000;
    if(screen)lv_obj_invalidate(screen);
}

enum { AVATAR_SIDE=54, AVATAR_PIXELS=54*54, AVATAR_RGB_BYTES=54*54*2 };
typedef struct {
    lv_image_dsc_t encoded;
    lv_fs_path_ex_t jpeg_path;
    lv_image_decoder_dsc_t decoded;
    uint8_t seen[(AVATAR_PIXELS+7)/8];
} avatar_decode_work_t;

/* TJPGD returns MCU blocks rather than one full image. Map samples using the
 * original image coordinates, never by transforming each block independently.
 * The optional alpha plane follows LVGL's RGB565A8 layout: RGB rows, then A8. */
static bool avatar_copy_region(const lv_draw_buf_t *decoded,const lv_area_t *part,
                               unsigned width,unsigned height,uint8_t **pixels,
                               bool *has_alpha,uint8_t *seen,unsigned *filled)
{
    if(!decoded || !decoded->data || part->x1<0 || part->y1<0 || part->x2<part->x1 ||
       part->y2<part->y1 || part->x2>=(int)width || part->y2>=(int)height)return false;
    unsigned region_width=(unsigned)lv_area_get_width(part),region_height=(unsigned)lv_area_get_height(part);
    unsigned bytes_per_pixel;
    switch(decoded->header.cf) {
    case LV_COLOR_FORMAT_RGB565: bytes_per_pixel=2;break;
    case LV_COLOR_FORMAT_RGB888: bytes_per_pixel=3;break;
    case LV_COLOR_FORMAT_ARGB8888: bytes_per_pixel=4;break;
    default:return false;
    }
    size_t stride=decoded->header.stride;
    if(decoded->header.w!=region_width || decoded->header.h!=region_height ||
       stride<(size_t)region_width*bytes_per_pixel ||
       decoded->data_size<stride*(region_height-1)+(size_t)region_width*bytes_per_pixel)return false;
    for(unsigned y=0;y<AVATAR_SIDE;++y) {
        unsigned sy=y*height/AVATAR_SIDE;
        if(sy<(unsigned)part->y1 || sy>(unsigned)part->y2)continue;
        const uint8_t *row=decoded->data+(sy-(unsigned)part->y1)*stride;
        for(unsigned x=0;x<AVATAR_SIDE;++x) {
            unsigned sx=x*width/AVATAR_SIDE;
            if(sx<(unsigned)part->x1 || sx>(unsigned)part->x2)continue;
            const uint8_t *source=row+(sx-(unsigned)part->x1)*bytes_per_pixel;
            unsigned at=y*AVATAR_SIDE+x;
            uint16_t color;
            uint8_t alpha=bytes_per_pixel==4?source[3]:255;
            if(bytes_per_pixel==2)memcpy(&color,source,sizeof(color));
            else color=(uint16_t)(((uint16_t)(source[2]>>3)<<11)|((uint16_t)(source[1]>>2)<<5)|(source[0]>>3));
            if(alpha!=255 && !*has_alpha) {
                uint8_t *expanded=lv_realloc(*pixels,AVATAR_RGB_BYTES+AVATAR_PIXELS);
                if(!expanded)return false;
                *pixels=expanded;*has_alpha=true;
                memset(expanded+AVATAR_RGB_BYTES,255,AVATAR_PIXELS);
            }
            memcpy(*pixels+at*2,&color,sizeof(color));
            if(*has_alpha)(*pixels)[AVATAR_RGB_BYTES+at]=alpha;
            uint8_t mask=(uint8_t)(1U<<(at%8));
            if(!(seen[at/8]&mask)) {seen[at/8]|=mask;++*filled;}
        }
    }
    return true;
}

bool badge_ui_set_avatar(const void *bytes,size_t size)
{
    if(!bytes) {
        lv_image_cache_drop(&avatar);lv_free((void *)avatar.data);
        memset(&avatar,0,sizeof(avatar));avatar_valid=false;
        if(screen)lv_obj_invalidate(screen);
        return false;
    }
    unsigned width=0,height=0;
    if(!badge_avatar_dimensions(bytes,size,&width,&height))return false;
    /* TJPGD's file-info callback already uses a 4KiB stack frame. Keep our
     * descriptors/coverage on the heap, outside the 6KiB application stack. */
    avatar_decode_work_t *work=lv_malloc_zeroed(sizeof(*work));
    if(!work)return false;
    work->encoded.header.magic=LV_IMAGE_HEADER_MAGIC;work->encoded.header.cf=LV_COLOR_FORMAT_RAW;
    work->encoded.header.w=width;work->encoded.header.h=height;
    work->encoded.data=bytes;work->encoded.data_size=(uint32_t)size;
    const void *source=&work->encoded;
    if(((const uint8_t *)bytes)[0]==0xff) {
        /* LVGL 9.5's variable-source JPEG detector only recognizes a JFIF APP0
         * prefix. GitHub also serves valid baseline JPEGs beginning with DQT.
         * Its file decoder accepts those streams through the public MEMFS API. */
        lv_fs_make_path_from_buffer(&work->jpeg_path,LV_FS_MEMFS_LETTER,bytes,(uint32_t)size,"jpg");
        source=&work->jpeg_path;
    }
    /* This temporary descriptor must never retain a header or decoded cache entry.
     * no_cache also releases LODEPNG's large intermediate buffer on close. */
    lv_image_cache_drop(source);
    lv_image_decoder_args_t args={.no_cache=true};
    lv_image_decoder_dsc_t *decoded=&work->decoded;
    bool opened=lv_image_decoder_open(decoded,source,&args)==LV_RESULT_OK;
    uint8_t *pixels=NULL;
    bool valid=false,has_alpha=false;
    unsigned filled=0;
    if(opened && decoded->header.w==width && decoded->header.h==height) {
        pixels=lv_malloc(AVATAR_RGB_BYTES);
        if(pixels) {
            lv_area_t full={0,0,(int)width-1,(int)height-1};
            if(decoded->decoded)valid=avatar_copy_region(decoded->decoded,&full,width,height,&pixels,&has_alpha,work->seen,&filled);
            else {
                lv_area_t part={LV_COORD_MIN,LV_COORD_MIN,LV_COORD_MIN,LV_COORD_MIN};
                for(unsigned blocks=0;blocks<4096;++blocks) {
                    if(lv_image_decoder_get_area(decoded,&full,&part)!=LV_RESULT_OK ||
                       !avatar_copy_region(decoded->decoded,&part,width,height,&pixels,&has_alpha,work->seen,&filled))break;
                    if(part.x2==full.x2 && part.y2==full.y2) {valid=true;break;}
                }
            }
        }
    }
    if(opened)lv_image_decoder_close(decoded);
    lv_image_cache_drop(source);
    lv_free(work);
    if(!valid || filled!=AVATAR_PIXELS) {lv_free(pixels);return false;}
    lv_image_cache_drop(&avatar);lv_free((void *)avatar.data);
    memset(&avatar,0,sizeof(avatar));
    avatar.header.magic=LV_IMAGE_HEADER_MAGIC;
    avatar.header.cf=has_alpha?LV_COLOR_FORMAT_RGB565A8:LV_COLOR_FORMAT_RGB565;
    avatar.header.w=AVATAR_SIDE;avatar.header.h=AVATAR_SIDE;avatar.header.stride=AVATAR_SIDE*2;
    avatar.data=pixels;avatar.data_size=AVATAR_RGB_BYTES+(has_alpha?AVATAR_PIXELS:0);
    avatar_valid=true;
    if(screen)lv_obj_invalidate(screen);
    return true;
}
