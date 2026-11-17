"""Verify Remote glyphs follow real button bindings and ignore idle noise."""
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
s=(root/"port/wiiu/src/wiiu_input.c").read_text()
a=s.index("static int prompt_activity(")
b=s.index("int wiiu_input_prompt_style(",a)
activity=s[a:b]
fixture=r"""
#include <assert.h>
#include <math.h>
#include "wiiu_wimote.h"
"""
main=r"""
int main(void) {
 assert(wimote_prompt_icon(WIIMOTE_LAYOUT_DEFAULT,0)==0);
 assert(wimote_prompt_icon(WIIMOTE_LAYOUT_DEFAULT,1)==3);
 assert(wimote_prompt_icon(WIIMOTE_LAYOUT_DEFAULT,2)==2);
 assert(wimote_prompt_icon(WIIMOTE_LAYOUT_DEFAULT,3)==8);
 assert(wimote_prompt_icon(WIIMOTE_LAYOUT_DEFAULT,6)==5);
 assert(wimote_prompt_icon(WIIMOTE_LAYOUT_DEFAULT,7)==1);
 assert(wimote_prompt_icon(WIIMOTE_LAYOUT_Z_FIRE,6)==1);
 assert(wimote_prompt_icon(WIIMOTE_LAYOUT_Z_FIRE,7)==5);
 assert(wimote_prompt_icon(WIIMOTE_LAYOUT_MENU,1)==1);
 assert(wimote_prompt_icon(WIIMOTE_LAYOUT_MENU,3)==3);
 assert(wimote_prompt_icon(WIIMOTE_LAYOUT_DEFAULT,14)==4);
 assert(wimote_prompt_icon(WIIMOTE_LAYOUT_DEFAULT,15)==9);
 assert(wimote_prompt_icon(WIIMOTE_LAYOUT_DEFAULT,16)==12);
 assert(wimote_prompt_icon(WIIMOTE_LAYOUT_DEFAULT,17)==13);
 struct wiiu_pad old={0}, now={0};
 now.left_x=0.03f;assert(!prompt_activity(&now,&old));
 now.buttons=WIIU_PAD_ZR;assert(prompt_activity(&now,&old));
 assert(!prompt_activity(&now,&old));
 now.buttons=0;assert(!prompt_activity(&now,&old));
 now.left_x=0.8f;assert(prompt_activity(&now,&old));
 return 0;
}
"""
with tempfile.TemporaryDirectory() as directory:
    p=Path(directory)
    (p/"test.c").write_text(fixture+activity+main)
    subprocess.run(["cc","-std=c99","-Wall","-Wextra","-Werror","-I",str(root/"port/wiiu/src"),
                    str(p/"test.c"),str(root/"port/wiiu/src/wiiu_wimote.c"),"-lm","-o",str(p/"test")],check=True)
    subprocess.run([str(p/"test")],check=True)
print("Remote button-glyph mapping and activity detection checks passed")
