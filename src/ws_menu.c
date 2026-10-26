/* Controller focus over the game's live GUI tree. No screen coordinates are
 * hardcoded for activation: original widgets receive their normal handlers. */
#include "ws_menu.h"
#include "ws_focus.h"
#include "ws_overlay.h"
#include "ws_help.h"
#include "ws_fps.h"
#include "ws_lifecycle.h"
#include <string.h>
#include <math.h>
typedef uintptr_t object;
typedef struct {float x,y;} point;
static object (*send)(object,object,...),(*selector)(const char *);
static so_module *game_image,*runtime;
static object container_class,button_class,hud_button_class,list_class,scroll_class,title_class,mini_class;
static object touch_class,string_class,target_class,inventory_class;
static object aim_panel,aim_touch;
static object defaults,defaults_class;
static const char fps_key[]="Vita.ShowFPS";
static point aim_position;
static object manager,screen,focused,focused_group;
static int title,targeting,selected=-1;static unsigned count;
static ws_focus_item items[256];
enum {RETAIN,RELEASE,KIND,COUNT,INDEX,POS,ID,UTF8,STRING,OBJECT,HIDE,SETY,SETTEXT,
      DOWN,UP,DISPOSE,BEGIN,DELEGATE,PAGE,MAIN_TOUCH,SET_DELEGATE,EVENT,SETX,SELCOUNT};
static object sel[SELCOUNT];
static const char *names[SELCOUNT]={"retain","release","isKindOfClass:","count","objectAtIndex:",
    "getAbsolutePos","ID","UTF8String","stringWithUTF8String:","object:","hide","setY:","setText:",
    "onDown:","onUp:","dispose","beganHandleWithUITouch:Event:","delegate","gotoPage:","setMainTouch:","setDelegate:","eventFired:Event:","setX:"};
enum {VISIBLE,STATE,WIDTH,HEIGHT,PARENT,CHILDREN,MODAL,GUIS,MANAGER_STATE,ENABLED,LIST_ENABLED,
      SCROLL,CONTENT,SNAP,SELECTED_ITEM,TITLE_STEP,TOUCH_POS,TOUCH_START,TOUCH_LAST,TOUCH_EVENT,OWNER,IMAGE_SCALE,IMAGE,FRAME_WIDTH,FRAME_HEIGHT,LOCAL_X,IVARS};
static uint32_t *offsets[IVARS];
static const char *ivars[IVARS]={"GUIObject.visible","GUIObject.state","GUIObject.width","GUIObject.height",
    "GUIObject.parent","GUIContainer.children","GUIContainer.modal","GUIManager.guis","GUIManager.state",
    "GUIButton.enabled","GUIListItem.enabled","GUIScrollPanel.scroll","GUIScrollPanel.content",
    "GUIScrollPanel.snapStep","GUIScrollPanel.selectedItem","GUITitle.step","TouchHandle.pos",
    "TouchHandle.startPos","TouchHandle.lastPos","TouchHandle.event","GUIObject.owner",
    "GUIImage.scale","GUIImage.image","FrameLayer.width","FrameLayer.height","GUIObject.x"};
static uint32_t word(object o,int field){uint32_t v=0;if(o)memcpy(&v,(void *)(o+*offsets[field]),4);return v;}
static int byte(object o,int field){return o?*(unsigned char *)(o+*offsets[field]):0;}
static int half(object o,int field){return o?*(int16_t *)(o+*offsets[field]):0;}
static int kind(object o,object c){return o&&c&&send(o,sel[KIND],c);}
static int shown(object o){return byte(o,VISIBLE)&&word(o,STATE)!=0;}
static object string(const char *s){return send(string_class,sel[STRING],s);}
static const char *identifier(object o){
    object id=o?send(o,sel[ID]):0;
    const char *value=id?(const char *)send(id,sel[UTF8]):NULL;
    return value?value:"";
}
static void position(object o,float *x,float *y){uint32_t packed=(uint32_t)send(o,sel[POS]);*x=(int16_t)packed;*y=(int16_t)(packed>>16);}
static void touch_position(object touch,point p){
    memcpy((void *)(touch+*offsets[TOUCH_POS]),&p,sizeof(p));
    memcpy((void *)(touch+*offsets[TOUCH_START]),&p,sizeof(p));
    memcpy((void *)(touch+*offsets[TOUCH_LAST]),&p,sizeof(p));
}
static void aim_end(void){
    if(aim_panel)send(aim_panel,sel[MAIN_TOUCH],0);
    if(aim_touch){send(aim_touch,sel[DISPOSE]);send(aim_touch,sel[RELEASE]);aim_touch=0;}
    if(aim_panel){send(aim_panel,sel[RELEASE]);aim_panel=0;}
}
static void aim_begin(object panel){
    if(aim_panel==panel)return;
    aim_end();
    aim_touch=send(touch_class,sel[BEGIN],0,1);if(!aim_touch)return;
    send(aim_touch,sel[RETAIN]);aim_panel=panel;send(panel,sel[RETAIN]);
    aim_position=(point){284,160};touch_position(aim_touch,aim_position);
    /* The original targeting renderer and release handler use mainTouch.
     * Bind directly so aiming over its Cancel button cannot activate it. */
    send(panel,sel[MAIN_TOUCH],aim_touch);send(aim_touch,sel[SET_DELEGATE],panel);
}
static void focus(int index){
    object next=index>=0?items[index].object:0,group=index>=0?items[index].group:0;
    if(next!=focused){if(next)send(next,sel[RETAIN]);if(focused)send(focused,sel[RELEASE]);focused=next;}
    if(group!=focused_group){if(group)send(group,sel[RETAIN]);if(focused_group)send(focused_group,sel[RELEASE]);focused_group=group;}
    selected=index;
}
static void ensure_visible(ws_focus_item *item){
    if(!item->group)return;
    object group=item->group;float gx,gy;position(group,&gx,&gy);
    int flags=byte(group,SCROLL),step=half(group,SNAP);
    if((flags&1)&&step>0){send(group,sel[PAGE],item->index);}
    else{
        float *content=(void *)(group+*offsets[CONTENT]);
        if(flags&1){if(item->x<gx)content[0]+=gx-item->x;else if(item->x+item->w>gx+half(group,WIDTH))content[0]-=item->x+item->w-gx-half(group,WIDTH);}
        if(flags&2){if(item->y<gy)content[1]+=gy-item->y;else if(item->y+item->h>gy+half(group,HEIGHT))content[1]-=item->y+item->h-gy-half(group,HEIGHT);}
    }
    position(item->object,&item->x,&item->y);
}
/* Traverse front to back. A visible modal container blocks siblings behind
 * it, including while it animates; hidden/disabled descendants never focus. */
static int collect(object o,object group,unsigned index,unsigned depth){
    if(!o||depth>24||!shown(o))return 0;
    int container=kind(o,container_class),modal=container&&byte(o,MODAL);
    if(word(o,STATE)!=1)return modal;
    if(container){
        object children=word(o,CHILDREN);unsigned n=(unsigned)send(children,sel[COUNT]);if(n>4096)n=4096;
        object next_group=kind(o,scroll_class)?o:group;
        while(n){--n;object child=send(children,sel[INDEX],n);if(collect(child,next_group,n,depth+1))break;}
    }
    int button=kind(o,button_class),list=kind(o,list_class);
    if((button&&!byte(o,ENABLED))||(list&&!byte(o,LIST_ENABLED)))return modal;
    if((button||list||kind(o,hud_button_class)||kind(o,mini_class))&&count<256){
        float x,y;position(o,&x,&y);int w=half(o,WIDTH),h=half(o,HEIGHT);
        if(w>0&&h>0&&(group||(x+w>0&&y+h>0&&x<568&&y<320))){
            const char *id=identifier(o);if(!id)id="";
            int preferred=!strncmp(id,"spell:",6)?5:(!strcmp(id,"closeSettings")||!strcmp(id,"spellCancel"))?3:
                (!strcmp(id,"continue")||!strcmp(id,"enterDungeon"))?2:!strcmp(id,"quitToMap")?-5:0;
            if(group&&word(group,SELECTED_ITEM)==o)preferred=4;
            else if(x+w<=0||y+h<=0||x>=568||y>=320)preferred=-2;
            items[count++]=(ws_focus_item){o,group,x,y,(float)w,(float)h,index,preferred};
        }
    }
    return modal;
}
void ws_menu_frame(uintptr_t m,int gameplay){
    manager=m;count=0;title=targeting=0;
    if(manager&&!defaults&&defaults_class){
        defaults=send(defaults_class,selector("standardUserDefaults"));
        if(defaults)ws_fps_set_visible((int)send(defaults,selector("boolForKey:"),string(fps_key)));
    }
    if(!manager||gameplay||word(manager,MANAGER_STATE)!=1){aim_end();screen=0;focus(-1);return;}
    object guis=word(manager,GUIS);unsigned n=(unsigned)send(guis,sel[COUNT]);if(n>256)n=256;
    object active=0;
    while(n){object gui=send(guis,sel[INDEX],--n);if(!shown(gui))continue;
        if(kind(gui,title_class)){active=gui;title=1;break;}
        if(kind(gui,target_class)){active=gui;targeting=1;break;}
        int modal=collect(gui,0,0,0);
        if(count||modal){active=gui;break;}
    }
    if(active!=screen){focus(-1);screen=active;}
    if(targeting){focus(-1);if(word(screen,STATE)==1)aim_begin(screen);return;}
    aim_end();
    int next=-1;for(unsigned i=0;i<count;++i)if(items[i].object==focused){next=(int)i;break;}
    if(next<0)next=ws_focus_initial(items,count);
    focus(next);
}
int ws_menu_title(void){return title;}
int ws_menu_targeting(void){return targeting;}
void ws_menu_aim(void *unused,float x,float y,float seconds){
    (void)unused;if(!aim_touch)return;
    aim_position.x=fminf(556,fmaxf(12,aim_position.x+x*180*seconds));
    aim_position.y=fminf(308,fmaxf(12,aim_position.y+y*180*seconds));
    memcpy((void *)(aim_touch+*offsets[TOUCH_LAST]),(void *)(aim_touch+*offsets[TOUCH_POS]),sizeof(point));
    memcpy((void *)(aim_touch+*offsets[TOUCH_POS]),&aim_position,sizeof(point));
}
void ws_menu_navigate(void *unused,int direction){
    (void)unused;if(title||!count)return;
    int next=ws_focus_next(items,count,selected,direction);focus(next);
    if(selected>=0)ensure_visible(&items[selected]);
}
void ws_menu_confirm(void *unused){
    (void)unused;if(!manager||word(manager,MANAGER_STATE)!=1)return;
    if(title){if(screen&&word(screen,TITLE_STEP)==1)send(screen,sel[DOWN],0);return;}
    if(targeting){if(aim_touch&&word(screen,STATE)==1){send(screen,sel[UP],aim_touch);aim_end();}return;}
    object target=focused;point p={284,160};
    if(target&&kind(word(target,OWNER),inventory_class)&&!strncmp(identifier(target),"spell:",6)){
        /* Inventory use is a release event, with first-X inspection and
         * second-X consumption/targeting enforced by the original game. */
        send(word(target,OWNER),sel[EVENT],target,4);
        count=0;selected=-1;return;
    }
    if(target&&selected>=0){ensure_visible(&items[selected]);p=(point){items[selected].x+items[selected].w/2,items[selected].y+items[selected].h/2};}
    object touch=send(touch_class,sel[BEGIN],0,1);if(!touch)return;send(touch,sel[RETAIN]);
    touch_position(touch,p);
    *(uint32_t *)(touch+*offsets[TOUCH_EVENT])=1;
    if(target)send(target,sel[RETAIN]);
    send(target?target:manager,sel[DOWN],touch);
    object delegate=send(touch,sel[DELEGATE]);
    *(uint32_t *)(touch+*offsets[TOUCH_EVENT])=4;
    if(delegate)send(delegate,sel[UP],touch);
    send(touch,sel[DISPOSE]);send(touch,sel[RELEASE]);
    if(target)send(target,sel[RELEASE]);
    /* Widget activation can replace a whole panel. Rebuild before drawing. */
    count=0;selected=-1;
}
void ws_menu_draw(void){
    if(targeting&&aim_touch){ws_overlay_focus(aim_position.x-4,aim_position.y-4,8,8);return;}
    if(selected<0||(unsigned)selected>=count||!focused||!shown(focused)||word(manager,MANAGER_STATE)!=1){
        ws_overlay_focus(0,0,0,0);return;
    }
    ws_focus_item *item=&items[selected];
    ws_overlay_focus(item->x,item->y,item->w,item->h);
}
static void (*settings_refresh)(object,object),(*inventory_refresh)(object,object),(*label_text)(object,object,object);
static void (*death_reset)(object,object),(*death_event)(object,object,object,int);
static void (*settings_event)(object,object,object,int);
static void reset_death(object self,object selection){
    ws_lifecycle_discard_checkpoint();
    death_reset(self,selection);
    /* reset disables the buttons; draw later enables them. Hide is independent
     * of enable, so Share stays absent throughout the score animation. */
    send(send(self,sel[OBJECT],string("shareScore")),sel[HIDE]);
    object frame=send(self,sel[OBJECT],string("tombButtonsFrame"));
    if(frame){
        float fx,fy;position(frame,&fx,&fy);
        const char *buttons[]={"quitGame","continue"};
        for(unsigned i=0;i<2;++i){
            object button=send(self,sel[OBJECT],string(buttons[i]));
            if(!button||half(frame,WIDTH)<=0||half(button,WIDTH)<=0)continue;
            float bx,by;position(button,&bx,&by);
            int x=half(button,LOCAL_X)+(int)(fx-bx)+(half(frame,WIDTH)-half(button,WIDTH))/2;
            send(button,sel[SETX],x);
        }
    }
}
static void event_death(object self,object selection,object sender,int event){
    /* Also reject a stale release queued before the panel was reset. */
    if(!strcmp(identifier(sender),"shareScore"))return;
    death_event(self,selection,sender,event);
}
static void refresh_inventory(object self,object selection){
    inventory_refresh(self,selection);
    static const char *icons[]={"equipIcon:0","equipIcon:1","equipIcon:2"};
    for(unsigned i=0;i<3;++i){object icon=send(self,sel[OBJECT],string(icons[i]));
        object frame=word(icon,IMAGE);if(!frame)continue;
        int w=half(frame,FRAME_WIDTH),h=half(frame,FRAME_HEIGHT);
        if(w>0&&h>0)*(unsigned char *)(icon+*offsets[IMAGE_SCALE])=(w*2<=48&&h*2<=48)?2:1;
    }
}
static void refresh_settings(object self,object selection){
    settings_refresh(self,selection);
    static const char *hide[]={"opt_walk","opt_swipe","opt_flip"};
    for(unsigned i=0;i<sizeof(hide)/sizeof(*hide);++i)send(send(self,sel[OBJECT],string(hide[i])),sel[HIDE]);
    object controls=send(self,sel[OBJECT],string("opt_controls"));send(controls,selector("show"));
    send(controls,sel[SETY],80);
    object help=send(self,sel[OBJECT],string("control:analog"));
    object fps=send(self,sel[OBJECT],string("control:pro"));
    send(help,sel[SETTEXT],string("View"));send(help,selector("setActive:"),0);
    send(fps,sel[SETTEXT],string(ws_fps_visible()?"FPS: ON":"FPS: OFF"));
    send(fps,selector("setActive:"),ws_fps_visible());
    object story=send(self,sel[OBJECT],string("opt_story"));send(story,sel[SETY],128);
}
static void event_settings(object self,object selection,object sender,int event){
    const char *id=identifier(sender);
    if(!strcmp(id,"control:analog")){if(event==4)ws_help_show(1);return;}
    if(!strcmp(id,"control:pro")){
        if(event==4){
            ws_fps_set_visible(!ws_fps_visible());
            send(defaults,selector("setBool:forKey:"),ws_fps_visible(),string(fps_key));
            send(defaults,selector("synchronize"));refresh_settings(self,selector("refresh"));
        }
        return;
    }
    if(event==4&&!strcmp(id,"quitToMap"))ws_lifecycle_discard_checkpoint();
    settings_event(self,selection,sender,event);
}
static void set_label(object self,object selection,object text){
    const char *s=kind(text,string_class)?(const char *)send(text,sel[UTF8]):NULL;
    if(s&&(!strcmp(s,"Tap Item Twice to Use")||!strcmp(s,"Tap Item Again to Use")))text=string("Press X again to use");
    if(s&&!strcmp(s,"Tap & Hold to Target"))text=string("Stick / D-pad: aim   X: cast   Circle: cancel");
    label_text(self,selection,text);
}
int ws_menu_install(void){
    object (*get_class)(const char *)=(void *)so_symbol(runtime,"objc_getClass");
    object (*method)(object,object)=(void *)so_symbol(runtime,"class_getInstanceMethod");
    object (*imp)(object)=(void *)so_symbol(runtime,"method_getImplementation");
    object (*replace)(object,object)=(void *)so_symbol(runtime,"method_setImplementation");
    if(!get_class||!method||!imp||!replace)return -1;
    string_class=get_class("NSString");if(!string_class)return -1;
    object refresh=method(get_class("GUISettings"),selector("refresh"));
    object text=method(get_class("GUILabel"),sel[SETTEXT]);
    object inv=method(get_class("GUIHUDInventory"),selector("refresh"));
    object dead=method(get_class("GUIDead"),selector("reset"));
    object event=method(get_class("GUIDead"),sel[EVENT]);
    object settings_ev=method(get_class("GUISettings"),sel[EVENT]);
    if(!refresh||!text||imp(refresh)!=game_image->load_addr+0x33845c||
       imp(text)!=game_image->load_addr+0x29eb88||!inv||imp(inv)!=game_image->load_addr+0x2a5718||
       !dead||imp(dead)!=game_image->load_addr+0x2aa810||
       !event||imp(event)!=game_image->load_addr+0x2ab068||
       !settings_ev||imp(settings_ev)!=game_image->load_addr+0x338aec)return -1;
    /* The constant-string object's structure and original content are checked
     * before replacing its immortal ASCII storage; original artwork remains. */
    uint32_t *prompt=(void *)(game_image->load_addr+0x4de798);
    if(prompt[3]!=19||memcmp((void *)(uintptr_t)prompt[2],"Tap Screen to Start",19))return -1;
    static const char start[]="Press START to begin";
    prompt[2]=(uint32_t)(uintptr_t)start;prompt[3]=sizeof(start)-1;
    settings_refresh=(void *)replace(refresh,(object)refresh_settings);
    label_text=(void *)replace(text,(object)set_label);
    inventory_refresh=(void *)replace(inv,(object)refresh_inventory);
    death_reset=(void *)replace(dead,(object)reset_death);
    death_event=(void *)replace(event,(object)event_death);
    settings_event=(void *)replace(settings_ev,(object)event_settings);
    defaults_class=get_class("NSUserDefaults");if(!defaults_class)return -1;
    return settings_refresh&&label_text&&inventory_refresh&&death_reset&&death_event&&settings_event?0:-1;
}
int ws_menu_bind(so_module *image,so_module *objc){
    game_image=image;runtime=objc;
    send=(void *)so_symbol(objc,"objc_msgSend");selector=(void *)so_symbol(objc,"sel_registerName");
    if(!send||!selector)return -1;
    for(unsigned i=0;i<SELCOUNT;++i)sel[i]=selector(names[i]);
    for(unsigned i=0;i<IVARS;++i){char name[128];
        strcpy(name,"OBJC_IVAR_$_");strcat(name,ivars[i]);offsets[i]=(void *)so_symbol(image,name);if(!offsets[i])return -1;}
#define CLASS(var,name) var=so_symbol(image,"OBJC_CLASS_$_" name);if(!var)return -1
    CLASS(container_class,"GUIContainer");CLASS(button_class,"GUIButton");CLASS(hud_button_class,"GUIHUDButton");
    CLASS(list_class,"GUIListItem");CLASS(scroll_class,"GUIScrollPanel");CLASS(title_class,"GUITitle");
    CLASS(mini_class,"GUIMiniCatalog");CLASS(touch_class,"TouchHandle");
    CLASS(target_class,"GUIHUDTarget");
    CLASS(inventory_class,"GUIHUDInventory");
#undef CLASS
    string_class=so_symbol(objc,"OBJC_CLASS_$_NSString");
    /* NSString is defined in Foundation, so resolve it after class registration
     * through objc_getClass if its class symbol is not exported by libSystem. */
    return 0;
}
