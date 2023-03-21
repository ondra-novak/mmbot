/*
 * main.cpp
 *
 *  Created on: 11. 9. 2021
 *      Author: ondra
 */

#include <iostream>

#include "BybitBrokerV5.h"
#include "rsa_tools.h"

int main(int argc, char **argv) {
    using namespace json;

    if (argc < 2) {
        std::cerr << "Requires a single parameter" << std::endl;
        return 1;
    }

    ByBitBrokerV5 ifc(argv[1]);
    ifc.dispatch();

    /*auto res = generateKeysWithBits(4096);
    std::cout << "Priv" << res.private_key << std::endl;
    std::cout << "pub" << res.public_key << std::endl;*/
}
