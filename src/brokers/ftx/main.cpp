/*
 * main.cpp
 *
 *  Created on: 5. 5. 2020
 *      Author: ondra
 */


#include "../api.h"
#include "interface.h"
#include <imtjson/parser.h>



int main(int argc, char **argv) {
	using namespace json;
	json::enableParsePreciseNumbers = true;

	if (argc < 2) {
		std::cerr << "Requires a signle parametr" << std::endl;
		return 1;
	}

	Interface ifc(argv[1]);
	ifc.dispatch();

}

