#include "ws_quality.h"
#include "ws_text_cache.h"
#include "ws_graphics.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#ifdef __vita__
#include "ws_profile.h"
#endif
typedef uintptr_t object;
typedef struct {int16_t x,y;float alpha,animation;} draw_context;
_Static_assert(sizeof(draw_context)==12,"GUI draw: ARM struct ABI");
static object (*send)(object,object,...),(*selector)(const char *);
static object retain_sel,utf8_sel,string_class;
static object (*original_string)(object,object,int);
static int (*original_column)(object,object,object);
static int (*original_fetch)(object,object);
static void (*original_draw)(object,object,draw_context);
static const unsigned char *(*column_text)(void *,int);
static uint32_t *row_ok,*statement,*row_dbh,*allow_null,*allow_cache,*columns;
static uint32_t *tip_ivar,*mode_ivar,*free_time,*visible,*state;
static unsigned char *game_data;
static pthread_t owner;
static ws_text_cache text_cache;
typedef struct {object dictionary,key;int index;} column_entry;
enum {COLUMN_SLOTS=8192,COLUMN_LIMIT=4096};
static column_entry column_cache[COLUMN_SLOTS];
static unsigned column_count,column_hits,column_misses;
static unsigned frames;
static object read_object(object self,const uint32_t *offset){uint32_t v;memcpy(&v,(void *)(self+*offset),4);return v;}
static int column_for_key(object self,object sel,object key){
    if(!pthread_equal(owner,pthread_self())||!read_object(self,statement)||!key)
        return original_column(self,sel,key);
    object dbh=read_object(self,row_dbh),dictionary=read_object(self,columns);
    /* Only cache the immutable column maps built by STH.initWithQuery:DBH:
     * while the game's bulk asset-load cache is enabled. Keep both identities
     * retained so a later statement cannot reuse a freed dictionary address. */
    if(!dictionary||!dbh||!*(unsigned char *)(dbh+*allow_cache))return original_column(self,sel,key);
    unsigned hash=(unsigned)((dictionary>>2)*2654435761u^(key>>2)*2246822519u);
    column_entry *empty=NULL;
    for(unsigned i=0;i<32;++i){column_entry *entry=&column_cache[(hash+i)&(COLUMN_SLOTS-1)];
        if(!entry->dictionary){empty=entry;break;}
        if(entry->dictionary==dictionary&&entry->key==key){++column_hits;return entry->index;}
    }
    ++column_misses;
    int index=original_column(self,sel,key);
    if(empty&&column_count<COLUMN_LIMIT){
        send(dictionary,retain_sel);send(key,retain_sel);
        *empty=(column_entry){dictionary,key,index};++column_count;
    }
    return index;
}
static int fetch_row(object self,object sel){
    int result=original_fetch(self,sel);
    static unsigned rows;
    if(pthread_equal(owner,pthread_self())&&!(++rows&127))ws_graphics_loading_tick();
#ifdef __vita__
    if(!(rows&8191)){char phase[64];snprintf(phase,sizeof(phase),"database rows %u",rows);ws_profile_mark(phase);}
#endif
    return result;
}
static object string_for_column(object self,object sel,int column){
    if(!pthread_equal(owner,pthread_self()))return original_string(self,sel,column);
    if(!*(unsigned char *)(self+*row_ok))return 0;
    const char *text=(const char *)column_text((void *)read_object(self,statement),column);
    if(!text||(!*text&&*(unsigned char *)(self+*allow_null)))return 0;
    object dbh=read_object(self,row_dbh);
    int cache=dbh&&*(unsigned char *)(dbh+*allow_cache);
    if(cache){object found=ws_text_cache_find(&text_cache,text);if(found)return found;}
    const char *mapped=ws_tutorial_text(text);
    object result=mapped!=text?send(string_class,utf8_sel,mapped):original_string(self,sel,column);
    if(cache&&result&&ws_text_cache_insert(&text_cache,text,result))send(result,retain_sel);
    /* This cache never evicts. Its retained immutable objects remain valid
     * even after DBH clears its own cache or an autorelease pool drains.
     * At most WS_TEXT_LIMIT <=192-byte strings are retained; large data bypasses it. */
    return result;
}
static void quickchat_draw(object self,object sel,draw_context context){
    int tip=(int)read_object(self,tip_ivar),mode=(int)read_object(self,mode_ivar);
    int supported=tip==1||tip==3||tip==4||tip==5||tip==7||tip==8;
    uint32_t modal;memcpy(&modal,game_data+52,4);
    if(!supported||!mode||!*(unsigned char *)(self+*visible)||!read_object(self,state)||(tip==3&&modal==1)){
        original_draw(self,sel,context);return;
    }
    /* Let the original GUI draw/animate the translated text. Bypass only its
     * touch-gesture branch. Prevent the normal-chat timeout branch from
     * advancing a tutorial while quickchatTip is temporarily zero. */
    uint32_t saved_time;memcpy(&saved_time,(void *)(self+*free_time),4);
    *(uint32_t *)(self+*tip_ivar)=0;*(uint32_t *)(self+*free_time)=0;
    original_draw(self,sel,context);
    *(uint32_t *)(self+*tip_ivar)=(uint32_t)tip;
    memcpy((void *)(self+*free_time),&saved_time,4);
}
void ws_quality_present(void){
    if(++frames==120){
        fprintf(stderr,"Wayward startup text cache: hits=%u misses=%u strings=%u keys=%u bytes\n",
            text_cache.hits,text_cache.misses,text_cache.count,text_cache.used);
        fprintf(stderr,"Wayward startup column cache: hits=%u misses=%u entries=%u\n",
            column_hits,column_misses,column_count);
        fflush(stderr);
    }
}
int ws_quality_attach(so_module *image,so_module *objc,so_module *sqlite){
    if(!image||!objc||!sqlite||original_string)return -1;
    object (*get_class)(const char *)=(void *)so_symbol(objc,"objc_getClass");
    object (*get_method)(object,object)=(void *)so_symbol(objc,"class_getInstanceMethod");
    object (*get_imp)(object)=(void *)so_symbol(objc,"method_getImplementation");
    object (*set_imp)(object,object)=(void *)so_symbol(objc,"method_setImplementation");
    send=(void *)so_symbol(objc,"objc_msgSend");selector=(void *)so_symbol(objc,"sel_registerName");
    column_text=(void *)so_symbol(sqlite,"sqlite3_column_text");
    if(!get_class||!get_method||!get_imp||!set_imp||!send||!selector||!column_text)return -1;
#define IVAR(target,name) target=(void *)so_symbol(image,"OBJC_IVAR_$_" name);if(!target)return -1
    IVAR(row_ok,"STH.rowOk");IVAR(statement,"STH.sth");IVAR(row_dbh,"STH.dbh");
    IVAR(allow_null,"STH.allowNull");IVAR(allow_cache,"DBH.allowCache");
    IVAR(columns,"STH.columns");
    IVAR(tip_ivar,"GUIQuickChat.quickchatTip");IVAR(mode_ivar,"GUIQuickChat.quickchatMode");
    IVAR(free_time,"GUIQuickChat.freechatTime");IVAR(visible,"GUIObject.visible");IVAR(state,"GUIObject.state");
#undef IVAR
    game_data=(void *)so_symbol(image,"game");if(!game_data)return -1;
    object string_method=get_method(get_class("STH"),selector("stringForColumn:"));
    object draw_method=get_method(get_class("GUIQuickChat"),selector("draw:"));
    object column_method=get_method(get_class("STH"),selector("columnForKey:"));
    object fetch_method=get_method(get_class("STH"),selector("fetchrow"));
    if(!string_method||!draw_method||!column_method||get_imp(column_method)!=image->load_addr+0x26956c||
       get_imp(string_method)!=image->load_addr+0x2692fc||
       get_imp(draw_method)!=image->load_addr+0x2b0f58||!fetch_method||
       get_imp(fetch_method)!=image->load_addr+0x269098)return -1;
    retain_sel=selector("retain");utf8_sel=selector("stringWithUTF8String:");
    string_class=get_class("NSString");if(!string_class)return -1;
    owner=pthread_self();
    original_string=(void *)set_imp(string_method,(object)string_for_column);
    original_column=(void *)set_imp(column_method,(object)column_for_key);
    original_fetch=(void *)set_imp(fetch_method,(object)fetch_row);
    original_draw=(void *)set_imp(draw_method,(object)quickchat_draw);
    return original_string&&original_column&&original_fetch&&original_draw?0:-1;
}
