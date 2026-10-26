#include <string.h>
const char *ws_tutorial_text(const char *text){
    static const char *pairs[][2]={
        {"Left Thumb: MOVEMENT\nSwipe Screen and Hold","MOVEMENT\nUse the left stick or D-pad"},
        {"Right Thumb: ATTACK\nTap to Swing Weapon","ATTACK\nPress X to swing your weapon"},
        {"Right Thumb: POWER ATTACK\nHold Screen then Release","POWER ATTACK\nHold R, then release"},
    {"Select Item from Menu\nUse the Sun Flare","Press SELECT: inventory\nHighlight Sun Flare\nPress X twice to use"},
        {"Right Thumb: Offensive Ability\nSwipe Up\nShoot the Guard","OFFENSIVE ABILITY\nPress Square to shoot the guard"},
        {"Right Thumb: Defensive Ability\nSwipe Down","DEFENSIVE ABILITY\nPress Triangle"},
        {"Balanced class with early health bonuses.\n\n- Swipe Down: Burning lantern that repels magic \n- Swipe Up: Stunning magical amulet blast\n- Hold Attack for a lunging slash","Balanced class with early health bonuses.\n\n- Triangle: Burning lantern that repels magic \n- Square: Stunning magical amulet blast\n- Hold R for a lunging slash"},
        {"Large weapons and high health.\n\n- Swipe Down: Damage blocking shield\n- Swipe Up: Toss a throwing axe\n- Hold Attack for a lunging slash","Large weapons and high health.\n\n- Triangle: Damage blocking shield\n- Square: Toss a throwing axe\n- Hold R for a lunging slash"},
        {"Basic attacks require energy to fire.\n\n- Swipe Down: Gust of air that pushes foes, blocks projectiles\n- Swipe Up: Torrent of burning flames \n- Hold Attack for a piercing magical blast","Basic attacks require energy to fire.\n\n- Triangle: Gust of air that pushes foes, blocks projectiles\n- Square: Torrent of burning flames \n- Hold R for a piercing magical blast"},
        {"Extra damage on backstab and stunned foes.\n \n- Swipe: Dodge through enemies\n- Hold Attack for a stunning kick ","Extra damage on backstab and stunned foes.\n \n- Square / Triangle: Dodge through enemies\n- Hold R for a stunning kick "},
        {"Destructive scroll magic.\n\n- Swipe: Dodge through spells \n- Hold Attack for a stunning charge rush","Destructive scroll magic.\n\n- Square / Triangle: Dodge through spells \n- Hold R for a stunning charge rush"},
        {"Extra damage on backstab and stunned foes.\n \n- Swipe Down: Fade from sight \n- Swipe Up: Blind nearby enemies \n- Hold Attack for a dagger throw","Extra damage on backstab and stunned foes.\n \n- Triangle: Fade from sight \n- Square: Blind nearby enemies \n- Hold R for a dagger throw"}
    };
    if(text)for(unsigned i=0;i<sizeof(pairs)/sizeof(*pairs);++i)
        if(!strcmp(text,pairs[i][0]))return pairs[i][1];
    return text;
}
