/*
 * main.cpp
 *
 *  Created on: 11. 9. 2021
 *      Author: ondra
 */


#include "bybitbroker.h"


int main(int argc, char **argv) {
	using namespace json;

	if (argc < 2) {
		std::cerr << "Requires a single parameter" << std::endl;
		return 1;
	}

	ByBitBroker ifc(argv[1]);
	ifc.dispatch();

}
