/*
 * errhandler.cpp
 *
 *  Created on: 7. 4. 2022
 *      Author: ondra
 */

#include "errhandler.h"
#include <shared/logOutput.h>

using ondra_shared::logError;


void report_unhandled(const char *file, int line) {
	try {
		throw;
	} catch (std::exception &e) {
		logError("Unhandled exception at $1:$2 - $3", file, line, e.what());
	} catch (...) {
		logError("Unhandled exception at $1:$2 - (no exception detail)", file, line);
	}
}


