/*
 * ibrokercontrol.h
 *
 *  Created on: 20. 10. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_IBROKERCONTROL_H_
#define SRC_MAIN_IBROKERCONTROL_H_

class IBrokerControl {
public:

	virtual json::Value getSettings(const std::string_view &pairHint) const = 0;
	virtual void setSettings(json::Value v) = 0;
	virtual ~IBrokerControl() {}
};



#endif /* SRC_MAIN_IBROKERCONTROL_H_ */
