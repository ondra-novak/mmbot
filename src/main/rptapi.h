/*
 * rptapi.h
 *
 *  Created on: 7. 2. 2022
 *      Author: ondra
 */

#ifndef SRC_MAIN_RPTAPI_H_
#define SRC_MAIN_RPTAPI_H_
#include <imtjson/value.h>
#include <simpleServer/http_parser.h>
#include <imtjson/namedEnum.h>

#include "stats2report.h"

namespace simpleServer {
class QueryParser;
}

class RptApi {
public:

	RptApi(PPerfModule perfMod);


	bool handle(simpleServer::HTTPRequest req, const ondra_shared::StrViewA &vpath);
protected:
	PPerfModule perfMod;

	enum class Operation {
		query,
		options,
		deltrade,
		report,
		traders
	};

	static json::NamedEnum<Operation> strOperation;

	bool reqDirectory(simpleServer::HTTPRequest req);
	bool reqQuery(simpleServer::HTTPRequest req, const simpleServer::QueryParser &q);
	bool reqOptions(simpleServer::HTTPRequest req, const simpleServer::QueryParser &q);
	bool reqDelTrade(simpleServer::HTTPRequest req, const simpleServer::QueryParser &q);
	bool reqReport(simpleServer::HTTPRequest req, const simpleServer::QueryParser &q);
	bool reqTraders(simpleServer::HTTPRequest req, const simpleServer::QueryParser &q);



};




#endif /* SRC_MAIN_RPTAPI_H_ */
