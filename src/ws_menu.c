/* Controller focus over the game's live GUI tree. No screen coordinates are
 * hardcoded for activation: original widgets receive their normal handlers. */
#include "ws_menu.h"
#include "ws_focus.h"
#include "ws_overlay.h"
#include "ws_help.h"
#include "ws_fps.h"
#include "ws_lifecycle.h"
#include "ws_item_text.h"
#include "ws_details_xml.h"
#include <string.h>
#include <math.h>
#include <stdio.h>
typedef uintptr_t object;
typedef struct {float x,y;} point;
static object (*send)(object,object,...),(*selector)(const char *);
static void (*send_stret)(uint32_t *,object,object,...);
static so_module *game_image,*runtime;
static object container_class,button_class,hud_button_class,list_class,scroll_class,title_class,mini_class;
static object touch_class,string_class,target_class,inventory_class,image_class;
static object session_class;
static object gui_class,xml_class,details_panel,details_return,details_source;
static unsigned char *game;
static int quick_items,quick_collect,new_run_tier,pending_run_tier;
static object quick_preview;
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
      SCROLL,CONTENT,SNAP,SELECTED_ITEM,TITLE_STEP,TOUCH_POS,TOUCH_START,TOUCH_LAST,TOUCH_EVENT,OWNER,IMAGE_SCALE,IMAGE,FRAME_WIDTH,FRAME_HEIGHT,LOCAL_X,SPELL_INDEX,RELATIVE,LEVEL_TUTORIAL,IVARS};
static uint32_t *offsets[IVARS];
static const char *ivars[IVARS]={"GUIObject.visible","GUIObject.state","GUIObject.width","GUIObject.height",
    "GUIObject.parent","GUIContainer.children","GUIContainer.modal","GUIManager.guis","GUIManager.state",
    "GUIButton.enabled","GUIListItem.enabled","GUIScrollPanel.scroll","GUIScrollPanel.content",
    "GUIScrollPanel.snapStep","GUIScrollPanel.selectedItem","GUITitle.step","TouchHandle.pos",
    "TouchHandle.startPos","TouchHandle.lastPos","TouchHandle.event","GUIObject.owner",
    "GUIImage.scale","GUIImage.image","FrameLayer.width","FrameLayer.height","GUIObject.x","GUIHUDInventory.selectedSpellIndex","GUI.relative","Level.tutorial"};
static uint32_t word(object o,int field){uint32_t v=0;if(o)memcpy(&v,(void *)(o+*offsets[field]),4);return v;}
static int byte(object o,int field){return o?*(unsigned char *)(o+*offsets[field]):0;}
static int half(object o,int field){return o?*(int16_t *)(o+*offsets[field]):0;}
static int kind(object o,object c){return o&&c&&send(o,sel[KIND],c);}
static int shown(object o){return byte(o,VISIBLE)&&word(o,STATE)!=0;}
static object string(const char *s){return send(string_class,sel[STRING],s);}
static uint32_t global(unsigned offset){uint32_t v=0;if(game)memcpy(&v,game+offset,4);return v;}
static object widget(object panel,const char *id){return send(panel,sel[OBJECT],string(id));}
static int spell_slot(const char *id){return !strncmp(id,"spell:",6)&&id[6]>='0'&&id[6]<='3'&&!id[7]?id[6]-'0':-1;}
static int equipment_slot(const char *id){return !strncmp(id,"equipIcon:",10)&&id[10]>='0'&&id[10]<='2'&&!id[11]?id[10]-'0':-1;}
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
static void character_topbar(object panel){
    object bar=widget(panel,"topbar"),coins=widget(panel,"coinsText");
    if(!bar||!coins||word(coins,PARENT)!=bar)return;
    /* Keep the original money text, coin sprite and top-bar artwork. */
    send(coins,sel[SETX],40);
    object children=word(bar,CHILDREN);unsigned n=(unsigned)send(children,sel[COUNT]);
    if(n>256)n=256;
    for(unsigned i=0;i+1<n;++i)if(send(children,sel[INDEX],i)==coins){
        /* The pinned gui_charselect.xml places the unnamed coin image next. */
        object coin=send(children,sel[INDEX],i+1);
        if(kind(coin,image_class)&&!*identifier(coin)){send(coin,sel[SETX],24);send(coin,sel[SETY],28);}
        break;
    }
    object catalog=widget(panel,"minicat");
    if(catalog){send(catalog,sel[HIDE]);*(unsigned char *)(catalog+*offsets[VISIBLE])=0;}
}
/* Traverse front to back. A visible modal container blocks siblings behind
 * it, including while it animates; hidden/disabled descendants never focus. */
static int collect(object o,object group,unsigned index,unsigned depth){
    if(!o||depth>24||!shown(o))return 0;
    /* The Android promotion has no usable content/backend on Vita. */
    if(kind(o,mini_class)){send(o,sel[HIDE]);return 0;}
    int container=kind(o,container_class),modal=container&&byte(o,MODAL);
    if(word(o,STATE)!=1)return modal;
    if(container){
        object children=word(o,CHILDREN);unsigned n=(unsigned)send(children,sel[COUNT]);if(n>4096)n=4096;
        object next_group=kind(o,scroll_class)?o:group;
        while(n){--n;object child=send(children,sel[INDEX],n);if(collect(child,next_group,n,depth+1))break;}
    }
    int button=kind(o,button_class),list=kind(o,list_class);
    if((button&&!byte(o,ENABLED))||(list&&!byte(o,LIST_ENABLED)))return modal;
    const char *id=identifier(o);
    int equip=equipment_slot(id)>=0&&kind(word(o,OWNER),inventory_class)&&word(o,IMAGE);
    if(quick_collect&&!container&&spell_slot(id)<0&&strcmp(id,"spellCancel"))return modal;
    if((button||list||equip||kind(o,hud_button_class))&&count<256){
        float x,y;position(o,&x,&y);int w=half(o,WIDTH),h=half(o,HEIGHT);
        if(equip){w=h=40;x-=20;y-=20;}
        if(w>0&&h>0&&(group||(x+w>0&&y+h>0&&x<568&&y<320))){
            int preferred=!strncmp(id,"spell:",6)?5:(!strcmp(id,"closeSettings")||!strcmp(id,"spellCancel"))?3:
                (!strcmp(id,"continue")||!strcmp(id,"enterDungeon"))?2:!strcmp(id,"quitToMap")?-5:0;
            if(!strcmp(id,"shareScore")&&new_run_tier)preferred=4;
            if(group&&word(group,SELECTED_ITEM)==o)preferred=4;
            else if(x+w<=0||y+h<=0||x>=568||y>=320)preferred=-2;
            items[count++]=(ws_focus_item){o,group,x,y,(float)w,(float)h,index,preferred};
        }
    }
    return modal;
}
static void preview_quick_item(void){
    if(!quick_items||!kind(screen,inventory_class)||!focused||quick_preview==focused||word(screen,STATE)!=1)return;
    int slot=spell_slot(identifier(focused));if(slot<0)return;
    /* Original event 4 inspects on the first press and casts on the second.
     * Reset before inspection: moving focus must NEVER consume an item. */
    *(int32_t *)(screen+*offsets[SPELL_INDEX])=-1;
    send(screen,sel[EVENT],focused,4);quick_preview=focused;
}
static void complete_new_run(void){
    if(!pending_run_tier||global(0)!=8||word(manager,MANAGER_STATE)!=1)return;
    int tier=pending_run_tier;pending_run_tier=0;
    /* Let the native transition finish saving and unloading the dead run.
     * Keep progression alerts visible rather than skipping their handlers. */
    object chars=send(manager,selector("getGUI:"),string("charselect"));
    if(!chars||shown(widget(chars,"alertPopup")))return;
    if(global(128)||tier<2||tier>5)return;
    send(session_class,selector("beginNewSessionWithTierNum:"),tier);
    object next=global(128);
    if(next&&send(next,selector("nextLevel")))send(manager,selector("fadeToNextLevel"));
}
void ws_menu_frame(uintptr_t m,int gameplay){
    manager=m;count=0;title=targeting=0;
    complete_new_run();
    if(manager&&!defaults&&defaults_class){
        defaults=send(defaults_class,selector("standardUserDefaults"));
        if(defaults)ws_fps_set_visible((int)send(defaults,selector("boolForKey:"),string(fps_key)));
    }
    if(!manager||gameplay||word(manager,MANAGER_STATE)!=1){
        if(gameplay||!manager){quick_items=0;quick_preview=0;}
        aim_end();screen=0;focus(-1);return;
    }
    object inv=send(manager,selector("inv"));
    if(quick_items&&(!shown(inv)||global(0)!=11)){quick_items=0;quick_preview=0;}
    object guis=word(manager,GUIS);unsigned n=(unsigned)send(guis,sel[COUNT]);if(n>256)n=256;
    object active=0;
    while(n){object gui=send(guis,sel[INDEX],--n);if(!shown(gui))continue;
        if(!strcmp(identifier(gui),"charselect"))character_topbar(gui);
        if(kind(gui,title_class)){active=gui;title=1;break;}
        if(kind(gui,target_class)){active=gui;targeting=1;break;}
        quick_collect=quick_items&&gui==inv;
        int modal=collect(gui,0,0,0);
        if(count||modal){active=gui;break;}
    }
    if(active!=screen){focus(-1);screen=active;}
    if(targeting){quick_items=0;quick_preview=0;focus(-1);if(word(screen,STATE)==1)aim_begin(screen);return;}
    aim_end();
    int next=-1;for(unsigned i=0;i<count;++i)if(items[i].object==focused){next=(int)i;break;}
    if(next<0)next=ws_focus_initial(items,count);
    focus(next);
    preview_quick_item();
}
int ws_menu_quick_items(void){
    if(!manager||word(manager,MANAGER_STATE)!=1||global(0)!=11)return 0;
    if(quick_items){quick_items=0;quick_preview=0;return 2;}
    if(global(52)!=0)return 0;
    object inv=send(manager,selector("inv"));if(!inv)return 0;
    send(manager,selector("fullPause"));
    if(!shown(inv))return 0;
    quick_items=1;quick_preview=0;focus(-1);
    int available=0;
    for(unsigned i=0;i<4;++i){char id[16];snprintf(id,sizeof(id),"spell:%u",i);
        object button=widget(inv,id);available+=shown(button)&&byte(button,ENABLED);}
    send(widget(inv,"spellTip"),selector("show"));
    send(widget(inv,"spellTipText"),sel[SETTEXT],string(available?"Choose an item. X: use":"No items carried"));
    return 1;
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
    if(screen==details_panel){
        if(direction!=1&&direction!=3)return;
        const char *panels[]={"leftScroll","rightScroll"},*labels[]={"leftBody","rightBody"};
        for(unsigned i=0;i<2;++i){
            object panel=widget(details_panel,panels[i]),label=widget(details_panel,labels[i]);
            if(!panel||!label)continue;
            float *content=(void *)(panel+*offsets[CONTENT]);
            float limit=fmaxf(0,half(label,HEIGHT)-half(panel,HEIGHT));
            content[1]=fminf(0,fmaxf(-limit,content[1]+(direction==1?20:-20)));
        }
        return;
    }
    int next=ws_focus_next(items,count,selected,direction);focus(next);
    if(selected>=0)ensure_visible(&items[selected]);
    preview_quick_item();
    if(!strcmp(identifier(screen),"forge")&&focused&&!strncmp(identifier(focused),"forgeList:",10))
        send(screen,sel[EVENT],focused,4); /* Selection only; never doForge. */
}
void ws_menu_confirm(void *unused){
    (void)unused;if(!manager||word(manager,MANAGER_STATE)!=1)return;
    if(details_panel&&screen==details_panel){ws_menu_close_details();return;}
    if(title){if(screen&&word(screen,TITLE_STEP)==1)send(screen,sel[DOWN],0);return;}
    if(targeting){if(aim_touch&&word(screen,STATE)==1){send(screen,sel[UP],aim_touch);aim_end();}return;}
    object target=focused;point p={284,160};
    if(target&&equipment_slot(identifier(target))>=0){ws_menu_details(NULL);return;}
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
static const char *utf8(object text){const char *s=text?(const char *)send(text,sel[UTF8]):NULL;return s?s:"";}
int ws_menu_close_details(void){
    if(!details_panel||!shown(details_panel))return 0;
    send(details_panel,sel[HIDE]);focus(-1);focused=details_return;details_return=0;
    /* Preserve the retained originating widget across the next tree rebuild. */
    screen=details_source;details_source=0;return 1;
}
static void show_details(const char *title_text,const char *left_title,const char *left_body,
                         const char *right_title,const char *right_body){
    if(!details_panel){
        object error=0,doc=send(xml_class,selector("alloc"));
        doc=send(doc,selector("initWithXMLString:options:error:"),string(ws_details_xml),0,&error);
        object root=doc?send(doc,selector("rootElement")):0;
        if(!root){if(doc)send(doc,sel[RELEASE]);return;}
        object panel=send(gui_class,selector("new"));
        if(!panel){send(doc,sel[RELEASE]);return;}
        *(uint32_t *)(panel+*offsets[RELATIVE])=1;
        send(panel,selector("parseFromXML:Parent:Owner:Last:"),root,0,panel,0);
        send(doc,sel[RELEASE]);
        if(!widget(panel,"detailsBack")||!widget(panel,"leftBody")){
            send(panel,sel[RELEASE]);return;
        }
        send(word(manager,GUIS),selector("addObject:"),panel);
        details_panel=panel;send(panel,sel[RELEASE]); /* Manager now owns it. */
    }
    int two=right_title&&*right_title;
    send(widget(details_panel,"detailsRight"),selector(two?"show":"hide"));
    send(widget(details_panel,"detailsLeft"),selector("setWidth:"),two?262:532);
    const char *wide[]={"leftName","leftScroll","leftBody"};
    for(unsigned i=0;i<3;++i)send(widget(details_panel,wide[i]),selector("setWidth:"),two?238:508);
    const char *ids[]={"detailsTitle","leftName","leftBody","rightName","rightBody"};
    const char *text[]={title_text,left_title,left_body,right_title,right_body};
    for(unsigned i=0;i<5;++i){
        object label=widget(details_panel,ids[i]);send(label,sel[SETTEXT],string(text[i]?text[i]:""));
        send(label,selector("wrap"));
    }
    const char *scrolls[]={"leftScroll","rightScroll"};
    for(unsigned i=0;i<2;++i){object panel=widget(details_panel,scrolls[i]);
        if(panel)memset((void *)(panel+*offsets[CONTENT]),0,2*sizeof(float));}
    if(details_return)send(details_return,sel[RELEASE]);
    details_return=focused;details_source=screen;
    if(details_return)send(details_return,sel[RETAIN]);
    send(details_panel,selector("show"));
}
static void describe_item(object item,int tier,char *out,size_t cap){
    if(!item){snprintf(out,cap,"No item equipped.");return;}
    uint32_t stats[9]={0};float values[3];
    send_stret(stats,item,selector("statsForTier:"),tier);
    memcpy(values,stats+1,sizeof(values));
    ws_item_text(out,cap,utf8(stats[7]),utf8(stats[8]),values,(int32_t)stats[4],
                 (int)send(item,selector("maxAmmo")),tier);
}
void ws_menu_details(void *unused){
    (void)unused;
    if(ws_menu_close_details())return;
    if(!manager||!screen||word(manager,MANAGER_STATE)!=1||word(screen,STATE)!=1)return;
    object player=global(132),equipment=player?send(player,selector("pequip")):0;
    if(!strcmp(identifier(screen),"forge")){
        /* Select through the original handler first, so all native cost/tier
         * checks and the expanded description labels stay authoritative. */
        if(focused&&!strncmp(identifier(focused),"forgeList:",10))send(screen,sel[EVENT],focused,4);
        object list=widget(screen,"forgeList"),chosen=send(list,selector("selectedItem"));
        if(!chosen)return;
        object item=send(global(124),selector("item:"),send(chosen,selector("value")));
        if(!item)return;
        int slot=(int)send(item,selector("slot"));if(slot<0||slot>4)return;
        object current=send(equipment,selector("getItemInSlotNum:"),slot);
        char left[2048],right[2048],lh[128],rh[128];
        describe_item(current,(int)send(equipment,selector("getTierForSlotNum:"),slot),left,sizeof(left));
        describe_item(item,(int)send(chosen,selector("tierNum")),right,sizeof(right));
        snprintf(lh,sizeof(lh),"Equipped: %s",utf8(send(current,selector("name"))));
        snprintf(rh,sizeof(rh),"Upgrade: %s",utf8(send(item,selector("name"))));
        show_details("Equipment comparison",lh,left,rh,right);return;
    }
    if(!kind(screen,inventory_class)||!focused)return;
    int slot=equipment_slot(identifier(focused));
    if(slot>=0){
        object item=send(equipment,selector("getItemInSlotNum:"),slot);
        char body[2048];describe_item(item,(int)send(equipment,selector("getTierForSlotNum:"),slot),body,sizeof(body));
        show_details("Equipped item",utf8(send(item,selector("name"))),body,"","");return;
    }
    if(spell_slot(identifier(focused))>=0){
        *(int32_t *)(screen+*offsets[SPELL_INDEX])=-1;
        send(screen,sel[EVENT],focused,4); /* Inspect only. */
        char name[128],body[2048];
        snprintf(name,sizeof(name),"%s",utf8(send(widget(screen,"infoTitle"),selector("text"))));
        snprintf(body,sizeof(body),"%s\n\n%s",utf8(send(widget(screen,"infoDesc"),selector("text"))),
                 utf8(send(widget(screen,"infoFlavor"),selector("text"))));
        show_details("Item details",name,body,"","");
    }
}
static void (*settings_refresh)(object,object),(*inventory_refresh)(object,object),(*label_text)(object,object,object);
static void (*death_reset)(object,object),(*death_event)(object,object,object,int);
static void (*settings_event)(object,object,object,int);
static void reset_death(object self,object selection){
    ws_lifecycle_discard_checkpoint();
    death_reset(self,selection);
    /* reset disables the buttons; draw later enables them. Hide is independent
     * of enable, so Share stays absent throughout the score animation. */
    new_run_tier=0;pending_run_tier=0;
    object session=global(128),level=global(140);
    int tier=(int)send(send(session,selector("tier")),selector("num"));
    if(!send(self,selector("victory"))&&level&&!byte(level,LEVEL_TUTORIAL)&&tier>=2&&tier<=5)new_run_tier=tier;
    object fresh=widget(self,"shareScore");
    if(new_run_tier){send(fresh,sel[SETTEXT],string("New Run"));send(fresh,selector("show"));}
    else send(fresh,sel[HIDE]);
    object frame=send(self,sel[OBJECT],string("tombButtonsFrame"));
    if(frame){
        float fx,fy;position(frame,&fx,&fy);
        const char *buttons[]={"quitGame","continue","shareScore"};
        for(unsigned i=0;i<3;++i){
            object button=send(self,sel[OBJECT],string(buttons[i]));
            if(!button||half(frame,WIDTH)<=0||half(button,WIDTH)<=0)continue;
            float bx,by;position(button,&bx,&by);
            int inset=(half(frame,WIDTH)-half(button,WIDTH))/2;
            if(new_run_tier&&i!=1)inset=i==0?2:half(frame,WIDTH)-half(button,WIDTH)-2;
            int x=half(button,LOCAL_X)+(int)(fx-bx)+inset;
            send(button,sel[SETX],x);
        }
    }
}
static void event_death(object self,object selection,object sender,int event){
    /* The reused button never reaches the original social-service handler. */
    if(!strcmp(identifier(sender),"shareScore")){
        if(event==4&&new_run_tier&&!pending_run_tier&&shown(sender)&&byte(sender,ENABLED)){
            pending_run_tier=new_run_tier;new_run_tier=0;
            send(sender,selector("disable"));
            ws_lifecycle_discard_checkpoint();
            death_event(self,selection,widget(self,"quitGame"),4);
        }
        return;
    }
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
    quick_preview=0;
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
    object player_down=method(get_class("Player"),selector("actionDown"));
    object near_set=method(get_class("Player"),selector("setNearInteract:"));
    object item_stats=method(get_class("ItemInfo"),selector("statsForTier:"));
    if(!refresh||!text||imp(refresh)!=game_image->load_addr+0x33845c||
       imp(text)!=game_image->load_addr+0x29eb88||!inv||imp(inv)!=game_image->load_addr+0x2a5718||
       !dead||imp(dead)!=game_image->load_addr+0x2aa810||
       !event||imp(event)!=game_image->load_addr+0x2ab068||
       !settings_ev||imp(settings_ev)!=game_image->load_addr+0x338aec||
       !player_down||imp(player_down)!=game_image->load_addr+0x2596c4||
       !near_set||imp(near_set)!=game_image->load_addr+0x2689d0||
       !item_stats||imp(item_stats)!=game_image->load_addr+0x3253b0)return -1;
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
    send_stret=(void *)so_symbol(objc,"objc_msgSend_stret");
    game=(void *)so_symbol(image,"game");
    if(!send||!selector||!send_stret||(object)game!=image->load_addr+0x4ec998)return -1;
    for(unsigned i=0;i<SELCOUNT;++i)sel[i]=selector(names[i]);
    for(unsigned i=0;i<IVARS;++i){char name[128];
        strcpy(name,"OBJC_IVAR_$_");strcat(name,ivars[i]);offsets[i]=(void *)so_symbol(image,name);if(!offsets[i])return -1;}
#define CLASS(var,name) var=so_symbol(image,"OBJC_CLASS_$_" name);if(!var)return -1
    CLASS(container_class,"GUIContainer");CLASS(button_class,"GUIButton");CLASS(hud_button_class,"GUIHUDButton");
    CLASS(list_class,"GUIListItem");CLASS(scroll_class,"GUIScrollPanel");CLASS(title_class,"GUITitle");
    CLASS(mini_class,"GUIMiniCatalog");CLASS(touch_class,"TouchHandle");
    CLASS(target_class,"GUIHUDTarget");
    CLASS(inventory_class,"GUIHUDInventory");
    CLASS(session_class,"Session");
    CLASS(gui_class,"GUI");CLASS(xml_class,"GDataXMLDocument");
    CLASS(image_class,"GUIImage");
#undef CLASS
    string_class=so_symbol(objc,"OBJC_CLASS_$_NSString");
    /* NSString is defined in Foundation, so resolve it after class registration
     * through objc_getClass if its class symbol is not exported by libSystem. */
    return 0;
}
