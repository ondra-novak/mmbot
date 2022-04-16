/*
 * strategy3.cpp
 *
 *  Created on: 17. 3. 2022
 *      Author: ondra
 */

#include "strategy3.h"

std::string_view ToolName<PStrategy3>::get()
{
	return "strategy";
}


json::NamedEnum<OrderRequestResult> Strategy3::strOrderRequestResult({
	{OrderRequestResult::accepted,"Accepted"},
	{OrderRequestResult::partially_accepted,"Partially accepted"},
	{OrderRequestResult::invalid_price,"Invalid price"},
	{OrderRequestResult::invalid_size,"Invalid size"},
	{OrderRequestResult::max_leverage,"Max leverage reached"},
	{OrderRequestResult::no_funds,"No funds"},
	{OrderRequestResult::too_small,"Too small order"},
	{OrderRequestResult::max_position,"Max position reached"},
	{OrderRequestResult::min_position,"Min position reached"},
	{OrderRequestResult::min_position,"Max costs reached"},

});

