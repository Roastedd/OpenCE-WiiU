"""Menus/chat must consume and discard Remote aim state before returning."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'port/wiiu/src/xinput_wiiu.c').read_text()
def function(signature):
 if signature.endswith('(void)'):
  a=s.index(signature+'\n{')
 else:
  a=s.index('int halo_linux_mouse_look(short gamepad_index, float *yaw, float *pitch)\n{')
 start=s.index('{',a);depth=1;i=start+1
 while depth:
  depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]
fixture=r'''
#include <assert.h>
#include <string.h>
#define WIIU_GYRO_SPACE_TURN 1
#define WIIU_GYRO_SPACE_LEAN 2
#define WIIU_GYRO_SPACE_PLAYER 3
static unsigned long aim_polls=10,gyro_aimed_at=8,stick_aimed_at=7;
static int remote_clutch_enabled=1,remote_clutch_button=1;
static int menu,chat,chat_port,held=1,touched,remote_owner=1,pointer_owner=1;
static int selected=1,hardware=1,clutch=1,pointer_valid=1;
static int gyro_takes,remote_takes,pointer_reads,recentres;
unsigned long config_changes(void){return 1;}
const char *config_string(const char *n){return strstr(n,"pause")?"touch":strstr(n,"space")?"player":"off";}
double config_real(const char *n){(void)n;return 1;}
int config_boolean(const char *n){(void)n;return 0;}
void wiiu_input_set_gyro_space(int mode){(void)mode;}
void wiiu_input_gyro_take(float*x,float*y){gyro_takes++;*x=.1f;*y=.1f;}
int wiiu_input_remote_look_take(float*x,float*y){remote_takes++;*x=.2f;*y=.1f;return remote_owner;}
int wiiu_input_remote_pointer(float*x,float*y,int*v){pointer_reads++;*x=.7f;*y=-.3f;*v=1;return pointer_owner;}
int remote_free_aim_select(int owner){selected=owner!=0;return selected;}
void wiiu_input_set_remote_free_aim(int on){hardware=on;}
void remote_aim_clutch(int on){clutch=on;}
int wiiu_input_remote_clutch(unsigned button){assert(button==1);return held;}
void remote_free_aim_pointer_sample(float x,float y,int valid){(void)x;(void)y;pointer_valid=valid;}
int port_menus_active(void){return menu;}
int network_chat_keyboard_open(void){return chat;}
int network_chat_controller(void){return chat_port;}
short player_control_get_zoom_level(short p){(void)p;return -1;}
int wiiu_input_touch(float*x,float*y){*x=*y=.5f;return touched;}
void remote_free_aim_recentre(float*yaw,float*pitch){recentres++;*yaw=-.3f;*pitch=.2f;}
'''
code=fixture+function('static void remote_aim_discard(void)')+'\n'+function('int halo_linux_mouse_look(')+r'''
int main(void){float yaw=9,pitch=9;
 menu=1;assert(!halo_linux_mouse_look(0,&yaw,&pitch));
 assert(!selected&&!hardware&&!clutch&&!pointer_valid&&gyro_aimed_at==0);
 assert(yaw==0&&pitch==0&&gyro_takes==1&&remote_takes==1&&pointer_reads==1);
 menu=0;chat=1;chat_port=0;selected=hardware=clutch=pointer_valid=1;gyro_aimed_at=10;
 assert(!halo_linux_mouse_look(0,&yaw,&pitch));assert(!selected&&!hardware&&!clutch&&!pointer_valid&&!gyro_aimed_at);
 chat=0;assert(halo_linux_mouse_look(0,&yaw,&pitch));assert(selected&&hardware&&clutch&&pointer_valid);
 int before=gyro_takes;assert(!halo_linux_mouse_look(1,&yaw,&pitch));assert(gyro_takes==before&&selected&&hardware&&clutch&&pointer_valid);
 chat=1;chat_port=1;assert(halo_linux_mouse_look(0,&yaw,&pitch));assert(selected&&hardware&&clutch&&pointer_valid);
 chat=0;remote_clutch_enabled=0;touched=1;assert(halo_linux_mouse_look(0,&yaw,&pitch));
 assert(recentres==1&&selected&&hardware&&!clutch&&pointer_valid&&yaw==-.3f&&pitch==.2f);
 touched=0;pointer_owner=remote_owner=0;selected=hardware=clutch=pointer_valid=1;
 assert(!halo_linux_mouse_look(0,&yaw,&pitch));assert(!selected&&!hardware&&!clutch&&!pointer_valid);
 return 0;}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(code)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('Menu/chat suppression clears consumed Remote aim; other ports and touch pause preserved')
