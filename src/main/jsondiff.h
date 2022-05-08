/*
 * jsondiff.h
 *
 *  Created on: 8. 5. 2022
 *      Author: ondra
 */

#ifndef SRC_MAIN_JSONDIFF_H_
#define SRC_MAIN_JSONDIFF_H_
#include <imtjson/value.h>


json::Value merge_JSON(json::Value src, json::Value diff);
json::Value make_JSON_diff(json::Value src, json::Value trg);



#endif /* SRC_MAIN_JSONDIFF_H_ */
