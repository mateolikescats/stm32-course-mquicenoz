/*
 * app.h
 * The two functions every stage provides. Each stage has its own app.c in
 * Core/app_N_<name>/, and the APP selector in CMakeLists.txt compiles one.
 * Author: Mateo Quiceno Zapata
 */

#ifndef APP_H
#define APP_H

void app_init(void); /* called once, after the clock and the blinky are running */
void app_loop(void); /* called forever from the while (1) loop in main() */

#endif /* APP_H */
