/*
 * isotime.h
 *
 *  Created on: 10. 11. 2019
 *      Author: ondra
 */

#ifndef SRC_BROKERS_ISOTIME_H_
#define SRC_BROKERS_ISOTIME_H_


enum class ParseTimeFormat {
	iso = 0,
	mysql = 1
};


template<typename String>
 static std::uint64_t parseTime(const String &date, ParseTimeFormat format) {
	static const char *formats[] = {
			"%d-%d-%dT%d:%d:%fZ",
			"%d-%d-%d %d:%d:%f",
	};
 	 int y,M,d,h,m;
 	 float s;
 	 sscanf(date.c_str(), formats[static_cast<int>(format)], &y, &M, &d, &h, &m, &s);
 	 float sec;
 	 float msec = std::modf(s,&sec)*1000;
 	 std::tm t={0};
 	 t.tm_year = y - 1900;
 	 t.tm_mon = M-1;
 	 t.tm_mday = d;
 	 t.tm_hour = h;
 	 t.tm_min = m;
 	 t.tm_sec = static_cast<int>(sec);
 	 std::uint64_t res = static_cast<std::uint64_t>(timegm(&t)) * 1000
 			 	 	   + static_cast<std::uint64_t>(msec);
 	 return res;
 }



#endif /* SRC_BROKERS_ISOTIME_H_ */
