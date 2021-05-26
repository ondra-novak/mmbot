/*
 * main.cpp
 *
 *  Created on: 22. 5. 2021
 *      Author: ondra
 */


#include "../api.h"
#include "interface.h"


int main(int argc, char **argv) {
	using namespace json;

	if (argc < 2) {
		std::cerr << "Requires a signle parameter" << std::endl;
		return 1;
	}

	okex::Interface ifc(argv[1]);
	ifc.dispatch();

}
