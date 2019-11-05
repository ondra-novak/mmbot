/*
 * apikeys.h
 *
 *  Created on: 14. 10. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_APIKEYS_H_
#define SRC_MAIN_APIKEYS_H_
#include <imtjson/value.h>


class IApiKey {
public:

	///sets api keys to the broker
	/** This method is called from WebAdmin when user sets new API keys. The broker should
	 * store the new keys in secure storage. It also activates the keys. The broker also must
	 * load the keys everytime it starts.
	 *
	 * @param keyData JSON object contains set of fields
	 * @exception any
	 */
	virtual void setApiKey(json::Value keyData) = 0;


	///Retrieves list of fields with format of each field.
	/**
	 * This function is called by WebAdmin to retrieve list of fields for creation of the form to let user to fill
	 * the secure data into it. The function must return an JSON array where each item contains an object with
	 * following items
	 *
	 * @code
	 * [
	 * 			{
			   "name":"string"
	 *         "type":"string|number|boolean|enum"
	 *         "label":"string",
	 *         "min":nn - for "number" the minimum
	 *         "max":nn - for "number" the maximum
	 *         "step":nn - for "number" change step
	 *
	 *         "options": { - for "enum"
	 *         			"value":"caption"
	 *         			 ....
	 *         			 }
	 *         "default":"string" - default value
	 *         }
	 *]
	 * @endcode
	 *
	 * @return
	 */
	virtual json::Value getApiKeyFields() const = 0;


};


#endif /* SRC_MAIN_APIKEYS_H_ */
