/*
 * ibrokercontrol.h
 *
 *  Created on: 20. 10. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_IBROKERCONTROL_H_
#define SRC_MAIN_IBROKERCONTROL_H_
#include <imtjson/value.h>

class IBrokerControl {
public:

	virtual json::Value getSettings(const std::string_view &pairHint) const = 0;
	virtual void setSettings(json::Value v) = 0;
	virtual ~IBrokerControl() {}
};

class IBrokerIcon {
public:
	//saves image to disk to specified path
	virtual void saveIconToDisk(const std::string &path) const = 0;
	//retrieves name of saved image
	virtual std::string getIconName() const = 0;
};

#endif /* SRC_MAIN_IBROKERCONTROL_H_ */
