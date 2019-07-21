/*
 * exec.cpp
 *
 *  Created on: 21. 7. 2019
 *      Author: ondra
 */
#include <unistd.h>
#include "spawn.h"

#include <cstdlib>


void spawn(const char *cmd, const  char *const *arglist) {
	if (::fork() == 0) {
		execvp(cmd, const_cast<char *const *>(arglist));
		exit(0);
	}
}
