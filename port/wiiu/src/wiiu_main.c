/*
WIIU_MAIN.C

The Wii U's entry point. The game's own main (source/shell/shell_xbox.c) is built as halo_game_main
(tools/wiiu_build.py), as the web build's is; the shell runs it.
*/

#include "wiiu_shell.h"

int halo_game_main(void);

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	return wiiu_shell_run(halo_game_main);
}
