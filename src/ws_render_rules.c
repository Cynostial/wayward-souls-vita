#include "ws_render_rules.h"
#include <vitaGL.h>
int ws_pixel_filter(unsigned target,unsigned parameter,int value){
    /* The original tightly packed sprite atlases have no duplicated edge
     * texels. Linear sampling crosses into adjoining images at Vita scale. */
    if(target==GL_TEXTURE_2D&&(parameter==GL_TEXTURE_MIN_FILTER||parameter==GL_TEXTURE_MAG_FILTER)){
        if(value==GL_LINEAR||value==GL_LINEAR_MIPMAP_NEAREST||value==GL_NEAREST_MIPMAP_LINEAR||value==GL_LINEAR_MIPMAP_LINEAR)
            return GL_NEAREST;
    }
    return value;
}
void ws_end_multitex(void){
    /* Restore all original combiner defaults, then select the equivalent
     * dedicated MODULATE pipeline. Leaving COMBINE active after the title's
     * glow makes subsequent ordinary sprites depend on its shader state. */
    static const unsigned reset[][2]={
        {GL_COMBINE_RGB,GL_MODULATE},{GL_SRC0_RGB,GL_TEXTURE},{GL_OPERAND0_RGB,GL_SRC_COLOR},
        {GL_SRC1_RGB,GL_PRIMARY_COLOR},{GL_OPERAND1_RGB,GL_SRC_COLOR},{GL_SRC2_RGB,GL_TEXTURE},
        {GL_COMBINE_ALPHA,GL_MODULATE},{GL_SRC0_ALPHA,GL_TEXTURE},{GL_OPERAND0_ALPHA,GL_SRC_ALPHA},
        {GL_SRC1_ALPHA,GL_PRIMARY_COLOR},{GL_OPERAND1_ALPHA,GL_SRC_ALPHA}
    };
    for(unsigned i=0;i<sizeof(reset)/sizeof(*reset);++i)glTexEnvi(GL_TEXTURE_ENV,reset[i][0],reset[i][1]);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
}
