#include "ws_online.h"
#include <stdint.h>
#include <stdio.h>
static uint32_t *active,*connected,*loaded;
static void unavailable(uintptr_t self,uintptr_t selector){
    (void)selector;
    /* Vita has no Game Center/Google Play account backend. Keep the original
     * game's offline flags; never report a successful account login. Local
     * progression and saves do not depend on these achievement-service flags. */
    if(self){*(uint8_t *)(self+*active)=0;*(uint8_t *)(self+*connected)=0;*(uint8_t *)(self+*loaded)=0;}
    fputs("Wayward: online achievements/account UI unavailable on Vita; continuing offline.\n",stderr);
}
int ws_online_attach(so_module *game){
    if(!game||so_symbol(game,"main")!=game->load_addr+0x358f24)return -1;
    active=(void *)so_symbol(game,"OBJC_IVAR_$_GameCenterSupport.active");
    connected=(void *)so_symbol(game,"OBJC_IVAR_$_GameCenterSupport.connected");
    loaded=(void *)so_symbol(game,"OBJC_IVAR_$_GameCenterSupport.achievementsLoaded");
    if(!active||!connected||!loaded)return -1;
    /* Exact Objective-C method starts verified in objc-methods.json. */
    hook_addr(game->load_addr+0x26bc68,(uintptr_t)unavailable);
    hook_addr(game->load_addr+0x26c158,(uintptr_t)unavailable);
    so_flush_caches(game);return 0;
}
